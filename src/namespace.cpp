/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/**
 * Copyright (C) 2017-2020 Regents of the University of California.
 * @author: Jeff Thompson <jefft0@remap.ucla.edu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version, with the additional exemption that
 * compiling, linking, and/or using OpenSSL is allowed.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * A copy of the GNU Lesser General Public License is in the file COPYING.
 */

#include <sstream>
#include <ndn-ind/util/exponential-re-express.hpp>
#include <ndn-ind/util/logging.hpp>
#include "impl/pending-incoming-interest-table.hpp"
#include <cnl-cpp/namespace.hpp>

using namespace std;
using namespace ndn;
using namespace ndn::func_lib;

INIT_LOGGER("cnl_cpp.Namespace");

namespace cnl_cpp {

Namespace::Handler&
Namespace::Handler::setNamespace(Namespace* nameSpace)
{
  if (namespace_ && namespace_ != nameSpace)
    throw runtime_error
      ("This Handler is already attached to a different Namespace object");

  namespace_ = nameSpace;
  onNamespaceSet();

  return *this;
}

void
Namespace::Handler::objectNeeded(bool mustBeFresh)
{
  if (!namespace_)
    throw runtime_error("Handler::objectNeeded: The Namespace has not been set");

  namespace_->objectNeeded(mustBeFresh);
}

void
Namespace::Handler::onNamespaceSet()
{
}

uint64_t
Namespace::getNextCallbackId()
{
  // This is an atomic_uint64_t, so increment is thread-safe.
  return ++lastCallbackId_;
}

Namespace::Impl::Impl
  (Namespace& outerNamespace, const Name& name, KeyChain* keyChain,
   const ndn::ptr_lib::shared_ptr<bool>& isShutDown)
: outerNamespace_(outerNamespace), name_(name), keyChain_(keyChain), parent_(0),
  root_(this), state_(NamespaceState_NAME_EXISTS),
  validateState_(NamespaceValidateState_WAITING_FOR_DATA),
  freshnessExpiryTime_(chrono::system_clock::time_point::min()),
  face_(0), decryptor_(0),
  maxInterestLifetime_(-1), syncDepth_(-1), registeredPrefixId_(0),
  isShutDown_(isShutDown)
{
}

Namespace*
Namespace::Impl::getParent()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the parent of this Namespace node because it is shut down");

  return &parent_->outerNamespace_;
}

Namespace*
Namespace::Impl::getRoot()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the root of this Namespace node because it is shut down");

  return &root_->outerNamespace_;
}

bool
Namespace::Impl::hasChild(const Name& descendantName)
{
  if (!name_.isPrefixOf(descendantName))
    throw runtime_error
      ("The name of this node is not a prefix of the descendant name");

  if (descendantName.size() == name_.size())
    // A trivial case where it is already the name of this node.
    return true;

  // Find the child node whose name equals the descendantName. We know the name
  // of the descendant Namespace is a prefix, so we can just go by component count
  // instead of a full compare.
  Namespace::Impl* descendantImpl = this;
  while (true) {
    const Name::Component& nextComponent =
      descendantName[descendantImpl->name_.size()];
    if (!descendantImpl->hasChild(nextComponent))
      return false;

    if (descendantImpl->name_.size() + 1 == descendantName.size())
      // nextComponent is the final component.
      return true;
    descendantImpl = descendantImpl->children_[nextComponent]->impl_.get();
  }
}

Namespace::Impl&
Namespace::Impl::getChildImpl(const Name& descendantName)
{
  if (!name_.isPrefixOf(descendantName))
    throw runtime_error
      ("The name of this node is not a prefix of the descendant name");

  // Find or create the child node whose name equals the descendantName. We know
  // the name of the descendant Namespace is a prefix, so we can just go by
  // component count instead of a full compare.
  Namespace::Impl* descendantImpl = this;
  while (descendantImpl->name_.size() < descendantName.size()) {
    const Name::Component& nextComponent =
      descendantName[descendantImpl->name_.size()];

    map<Name::Component, ptr_lib::shared_ptr<Namespace>>::iterator
      child = descendantImpl->children_.find(nextComponent);
    if (child != descendantImpl->children_.end())
      descendantImpl = child->second->impl_.get();
    else {
      // Only fire the callbacks for the leaf node.
      bool isLeaf =
        (descendantImpl->name_.size() == descendantName.size() - 1);
      descendantImpl = descendantImpl->createChild
        (nextComponent, isLeaf).impl_.get();
    }
  }

  return *descendantImpl;
}

ptr_lib::shared_ptr<vector<Name::Component>>
Namespace::Impl::getChildComponents()
{
  ptr_lib::shared_ptr<vector<Name::Component>> result =
    ptr_lib::make_shared<vector<Name::Component>>();
  for (map<Name::Component, ptr_lib::shared_ptr<Namespace>>::iterator i = children_.begin();
       i != children_.end(); ++i)
    result->push_back(i->first);

  return result;
}

void
Namespace::Impl::serializeObject(const ptr_lib::shared_ptr<Object>& object)
{
  if (getIsShutDown())
    return;

  // TODO: What if this node already has a _data and/or _object?

  // TODO: Call handler canSerialize and set state SERIALIZING.
  // (Does this happen in a different place from onObjectNeeded?)
  // (If the handler can't serialize and this node has children, should abort?)

  BlobObject* blobObject = dynamic_cast<BlobObject*>(object.get());
  if (!blobObject)
      throw runtime_error
        ("serializeObject: For the default serialize, the object must be a Blob");

  KeyChain* keyChain = getKeyChain_();
  if (!keyChain)
    throw runtime_error
      ("serializeObject: There is no KeyChain, so can't serialize " +
       name_.toUri());

  // TODO: Encrypt and set state ENCRYPTING.

  // Prepare the Data packet.
  ptr_lib::shared_ptr<Data> data = ptr_lib::make_shared<Data>(name_);
  data->setContent(blobObject->getBlob());
  const MetaInfo* metaInfo = getNewDataMetaInfo_();
  if (metaInfo)
    data->setMetaInfo(*metaInfo);

  setState(NamespaceState_SIGNING);
  try {
    keyChain->sign(*data);
  } catch (const std::exception& ex) {
    signingError_ = string("Error signing the serialized Data: ") + ex.what();
    setState(NamespaceState_SIGNING_ERROR);
    return;
  }

  // This calls satisfyInterests.
  setData(data);

  setObject_(object);
}

bool
Namespace::Impl::setData(const ptr_lib::shared_ptr<Data>& data)
{
  if (getIsShutDown())
    return false;

  if (data_)
    // We already have an attached object.
    return false;
  if (!data->getName().equals(name_))
    throw runtime_error
      ("The Data packet name does not equal the name of this Namespace node");

  if (root_->pendingIncomingInterestTable_)
    // Quickly send the Data packet to satisfy interest, before calling callbacks.
    root_->pendingIncomingInterestTable_->satisfyInterests(*data);

  if (data->getMetaInfo().getFreshnessPeriod().count() >= 0.0)
    freshnessExpiryTime_ =
      chrono::system_clock::now() +
        chrono::duration_cast<chrono::milliseconds>(data->getMetaInfo().getFreshnessPeriod());
  else
    // Does not expire.
    freshnessExpiryTime_ = chrono::system_clock::time_point::min();
  data_ = data;

  return true;
}

void
Namespace::Impl::getAllData
  (std::vector<ndn::ptr_lib::shared_ptr<ndn::Data>>& dataList)
{
  if (data_)
    dataList.push_back(data_);

  if (children_.size() > 0) {
    for (map<Name::Component, ptr_lib::shared_ptr<Namespace>>::iterator i = children_.begin();
         i != children_.end(); ++i)
      i->second->impl_->getAllData(dataList);
  }
}

uint64_t
Namespace::Impl::addOnStateChanged(const OnStateChanged& onStateChanged)
{
  uint64_t callbackId = getNextCallbackId();
  onStateChangedCallbacks_[callbackId] = onStateChanged;
  return callbackId;
}

uint64_t
Namespace::Impl::addOnValidateStateChanged
  (const OnValidateStateChanged& onValidateStateChanged)
{
  uint64_t callbackId = getNextCallbackId();
  onValidateStateChangedCallbacks_[callbackId] = onValidateStateChanged;
  return callbackId;
}

uint64_t
Namespace::Impl::addOnObjectNeeded(const OnObjectNeeded& onObjectNeeded)
{
  uint64_t callbackId = getNextCallbackId();
  onObjectNeededCallbacks_[callbackId] = onObjectNeeded;
  return callbackId;
}

void
Namespace::Impl::setFace
  (Face* face, const OnRegisterFailed& onRegisterFailed,
   const OnRegisterSuccess& onRegisterSuccess)
{
  if (!face) {
    // Remove the Face if it is set.
    if (face_) {
      face_->removeRegisteredPrefix(registeredPrefixId_);
      registeredPrefixId_ = 0;
      // TODO: Remove the Face and callbacks from root_->fullPSync_.
      face_ = 0;
    }

    return;
  }

  if (getIsShutDown())
    return;

  face_ = face;

  if (onRegisterFailed) {
    if (!root_->pendingIncomingInterestTable_)
      // All onInterest callbacks share this in the root node. When we add a new
      // Data packet to a Namespace node, we will also check if it satisfies a
      // pending Interest.
      root_->pendingIncomingInterestTable_ =
        ptr_lib::make_shared<PendingIncomingInterestTable>();

    registeredPrefixId_ = face->registerPrefix
      (name_,
       bind(&Namespace::Impl::onInterest, shared_from_this(), _1, _2, _3, _4, _5),
       onRegisterFailed, onRegisterSuccess);
  }
}

void
Namespace::Impl::enableSync(int depth)
{
  if (getIsShutDown())
    return;

  if (!root_->fullPSync_) {
    Face* face = getFace_();
    if (!face)
      throw runtime_error("enableSync: You must first call setFace on this or a parent");

    root_->fullPSync_ = ptr_lib::make_shared<FullPSync2017>
      (275, *face, Name("/CNL-sync"),
       bind(&Namespace::Impl::onNamesUpdate, shared_from_this(), _1),
       *getKeyChain_(),
       chrono::milliseconds(1600), chrono::milliseconds(1600));
  }

  syncDepth_ = depth;
  // Debug: Add existing leaf nodes.
}

void
Namespace::Impl::objectNeeded(bool mustBeFresh)
{
  if (getIsShutDown())
    return;

  // Check if we already have the object.
  Interest interest(name_);
  // TODO: Make the lifetime configurable.
  interest.setInterestLifetimeMilliseconds(4000.0);
  interest.setMustBeFresh(mustBeFresh);
  // Debug: This requires a Data packet. Check for an object without one?
  Namespace::Impl* bestMatch = findBestMatchName
    (*this, interest, chrono::system_clock::now());
  if (bestMatch && bestMatch->object_) {
    // Set the state again to fire the callbacks.
    bestMatch->setState(NamespaceState_OBJECT_READY);
    return;
  }

  // Ask all OnObjectNeeded callbacks if they can produce.
  bool canProduce = false;
  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->fireOnObjectNeeded(outerNamespace_))
      canProduce = true;
    impl = impl->parent_;
  }

  // Debug: Check if the object has been set (even if onObjectNeeded returned false.)

  if (canProduce) {
    // Assume that the application will produce the object.
    setState(NamespaceState_PRODUCING_OBJECT);
    return;
  }

  // Express the interest.
  Face* face = getFace_();
  if (!face)
    throw runtime_error("A Face object has not been set for this or a parent");
  // TODO: What if the state is already INTEREST_EXPRESSED?
  setState(NamespaceState_INTEREST_EXPRESSED);
  face->expressInterest
    (interest,
     bind(&Namespace::Impl::onData, shared_from_this(), _1, _2),
     ExponentialReExpress::makeOnTimeout
       (face, bind(&Namespace::Impl::onData, shared_from_this(), _1, _2),
        bind(&Namespace::Impl::onTimeout, shared_from_this(), _1),
        getMaxInterestLifetime()),
     bind(&Namespace::Impl::onNetworkNack, shared_from_this(), _1, _2));
}

void
Namespace::Impl::removeCallback(uint64_t callbackId)
{
  onStateChangedCallbacks_.erase(callbackId);
  onValidateStateChangedCallbacks_.erase(callbackId);
}

void
Namespace::Impl::shutdown()
{
  if (parent_)
    throw runtime_error("Can only call shutdown() on the root Namespace node");

  *isShutDown_ = true;

  // This will unregister the face_ if needed.
  getIsShutDown();
}

bool
Namespace::Impl::getIsShutDown()
{
  if (*isShutDown_) {
    if (face_) {
      // We are shut down, so remove the Face and the callback.
      face_->removeRegisteredPrefix(registeredPrefixId_);
      registeredPrefixId_ = 0;
      face_ = 0;
    }

    return true;
  }
  else
    return false;
}

Face*
Namespace::Impl::getFace_()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the Face of this Namespace node because it is shut down");

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->face_)
      return impl->face_;
    impl = impl->parent_;
  }

  return 0;
}

KeyChain*
Namespace::Impl::getKeyChain_()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the KeyChain of this Namespace node because it is shut down");

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->keyChain_)
      return impl->keyChain_;
    impl = impl->parent_;
  }

  return 0;
}

Namespace::Impl*
Namespace::Impl::getSyncNode()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the Sync Node of this Namespace node because it is shut down");

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->syncDepth_ >= 0)
      return impl;
    impl = impl->parent_;
  }

  return 0;
}

std::chrono::nanoseconds
Namespace::Impl::getMaxInterestLifetime()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the MaxInterestLifetime of this Namespace node because it is shut down");

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->maxInterestLifetime_.count() >= 0)
      return impl->maxInterestLifetime_;
    impl =impl->parent_;
  }

  // Return the default.
  return std::chrono::nanoseconds(-1);
}

const MetaInfo*
Namespace::Impl::getNewDataMetaInfo_()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the NewDataMetaInfo of this Namespace node because it is shut down");

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->newDataMetaInfo_)
      return impl->newDataMetaInfo_.get();
    impl = impl->parent_;
  }

  return 0;
}

DecryptorV2*
Namespace::Impl::getDecryptor()
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot get the Decryptor of this Namespace node because it is shut down");

  Namespace::Impl* impl =this;
  while (impl) {
    if (impl->decryptor_)
      return impl->decryptor_;
    impl = impl->parent_;
  }

  return 0;
}

uint64_t
Namespace::Impl::addOnDeserializeNeeded_
  (const Handler::OnDeserializeNeeded& onDeserializeNeeded)
{
  uint64_t callbackId = getNextCallbackId();
  onDeserializeNeededCallbacks_[callbackId] = onDeserializeNeeded;
  return callbackId;
}

void
Namespace::Impl::deserialize_
  (const Blob& blob, const Handler::OnObjectSet& onObjectSet)
{
  if (getIsShutDown())
    return;

  Namespace::Impl* impl = this;
  while (impl) {
    if (impl->fireOnDeserializeNeeded(*this, blob, onObjectSet)) {
      // Wait for the Handler to set the object, if it hasn't yet.
      if (state_ < NamespaceState_DESERIALIZING)
        setState(NamespaceState_DESERIALIZING);
      return;
    }

    impl = impl->parent_;
  }

  // Debug: Check if the object has been set (even if canDeserialize returned false.)

  // Just call defaultOnDeserialized immediately.
  defaultOnDeserialized(ptr_lib::make_shared<BlobObject>(blob), onObjectSet);
}

Namespace&
Namespace::Impl::createChild(const Name::Component& component, bool fireCallbacks)
{
  if (getIsShutDown())
    throw runtime_error
      ("Cannot create a child of this Namespace node because it is shut down");

  // Every child has a shared_ptr to the same isShutDown_ flag.
  ptr_lib::shared_ptr<Namespace> child(new Namespace
    (Name(name_).append(component), (KeyChain *)0, isShutDown_));
  child->impl_->parent_ = this;
  child->impl_->root_ = root_;
  children_[component] = child;

  if (fireCallbacks) {
    child->impl_->setState(NamespaceState_NAME_EXISTS);

    // Sync this name under the same conditions that we report a NAME_EXISTS.
    if (root_->fullPSync_) {
      Namespace::Impl* syncNode = child->impl_->getSyncNode();
      if (syncNode) {
        // Only sync names to the specified depth.
        int depth = child->impl_->name_.size() - syncNode->name_.size();

        if (depth <= syncNode->syncDepth_)
          // If createChild is called when onNamesUpdate receives a name from
          //   fullPSync_, then publishName already has it and will ignore it.
          root_->fullPSync_->publishName(child->impl_->name_);
      }
    }
  }

  return *child;
}

void
Namespace::Impl::setState(NamespaceState state)
{
  if (getIsShutDown())
    return;

  state_ = state;

  // Fire callbacks.
  Namespace::Impl* impl = this;
  while (impl) {
    impl->fireOnStateChanged(outerNamespace_, state);
    impl = impl->parent_;
  }
}

void
Namespace::Impl::fireOnStateChanged
  (Namespace& changedNamespace, NamespaceState state)
{
  if (getIsShutDown())
    return;

  // Copy the keys before iterating since callbacks can change the list.
  vector<uint64_t> keys;
  keys.reserve(onStateChangedCallbacks_.size());
  for (map<uint64_t, OnStateChanged>::iterator i = onStateChangedCallbacks_.begin();
       i != onStateChangedCallbacks_.end(); ++i)
    keys.push_back(i->first);

  for (size_t i = 0; i < keys.size(); ++i) {
    // A callback on a previous pass may have removed this callback, so check.
    map<uint64_t, OnStateChanged>::iterator entry =
      onStateChangedCallbacks_.find(keys[i]);
    if (entry != onStateChangedCallbacks_.end()) {
      try {
        entry->second(outerNamespace_, changedNamespace, state, entry->first);
      } catch (const std::exception& ex) {
        _LOG_ERROR("Namespace::fireOnStateChanged: Error in onStateChanged: " <<
                   ex.what());
      } catch (...) {
        _LOG_ERROR("Namespace::fireOnStateChanged: Error in onStateChanged.");
      }
    }
  }
}

void
Namespace::Impl::setValidateState(NamespaceValidateState validateState)
{
  if (getIsShutDown())
    return;

  validateState_ = validateState;

  // Fire callbacks.
  Namespace::Impl* impl = this;
  while (impl) {
    impl->fireOnValidateStateChanged(outerNamespace_, validateState);
    impl = impl->parent_;
  }
}

void
Namespace::Impl::fireOnValidateStateChanged
  (Namespace& changedNamespace, NamespaceValidateState validateState)
{
  if (getIsShutDown())
    return;

  // Copy the keys before iterating since callbacks can change the list.
  vector<uint64_t> keys;
  keys.reserve(onValidateStateChangedCallbacks_.size());
  for (map<uint64_t, OnValidateStateChanged>::iterator i =
         onValidateStateChangedCallbacks_.begin();
       i != onValidateStateChangedCallbacks_.end(); ++i)
    keys.push_back(i->first);

  for (size_t i = 0; i < keys.size(); ++i) {
    // A callback on a previous pass may have removed this callback, so check.
    map<uint64_t, OnValidateStateChanged>::iterator entry =
      onValidateStateChangedCallbacks_.find(keys[i]);
    if (entry != onValidateStateChangedCallbacks_.end()) {
      try {
        entry->second
          (outerNamespace_, changedNamespace, validateState, entry->first);
      } catch (const std::exception& ex) {
        _LOG_ERROR
          ("Namespace::fireOnValidateStateChanged: Error in onValidateStateChanged: " <<
           ex.what());
      } catch (...) {
        _LOG_ERROR
          ("Namespace::fireOnValidateStateChanged: Error in onValidateStateChanged.");
      }
    }
  }
}

bool
Namespace::Impl::fireOnObjectNeeded(Namespace& neededNamespace)
{
  if (getIsShutDown())
    return false;

  // Copy the keys before iterating since callbacks can change the list.
  vector<uint64_t> keys;
  keys.reserve(onObjectNeededCallbacks_.size());
  for (map<uint64_t, OnObjectNeeded>::iterator i = onObjectNeededCallbacks_.begin();
       i != onObjectNeededCallbacks_.end(); ++i)
    keys.push_back(i->first);

  bool canProduce = false;
  for (size_t i = 0; i < keys.size(); ++i) {
    // A callback on a previous pass may have removed this callback, so check.
    map<uint64_t, OnObjectNeeded>::iterator entry =
      onObjectNeededCallbacks_.find(keys[i]);
    if (entry != onObjectNeededCallbacks_.end()) {
      try {
        if (entry->second(outerNamespace_, neededNamespace, entry->first))
          canProduce = true;
      } catch (const std::exception& ex) {
        _LOG_ERROR("Namespace::fireOnObjectNeeded: Error in onObjectNeeded: " <<
                   ex.what());
      } catch (...) {
        _LOG_ERROR("Namespace::fireOnObjectNeeded: Error in onObjectNeeded.");
      }
    }
  }

  return canProduce;
}

bool
Namespace::Impl::fireOnDeserializeNeeded
  (Namespace::Impl& blobNamespaceImpl, const ndn::Blob& blob,
   const Handler::OnObjectSet& onObjectSet)
{
  if (getIsShutDown())
    return false;

  Handler::OnDeserialized onDeserialized =
    bind(&Namespace::Impl::defaultOnDeserialized,
         blobNamespaceImpl.shared_from_this(), _1, onObjectSet);

  // Copy the keys before iterating since callbacks can change the list.
  vector<uint64_t> keys;
  keys.reserve(onDeserializeNeededCallbacks_.size());
  for (map<uint64_t, Handler::OnDeserializeNeeded>::iterator i = onDeserializeNeededCallbacks_.begin();
       i != onDeserializeNeededCallbacks_.end(); ++i)
    keys.push_back(i->first);

  for (size_t i = 0; i < keys.size(); ++i) {
    // A callback on a previous pass may have removed this callback, so check.
    map<uint64_t, Handler::OnDeserializeNeeded>::iterator entry =
      onDeserializeNeededCallbacks_.find(keys[i]);
    if (entry != onDeserializeNeededCallbacks_.end()) {
      try {
        if (entry->second
            (blobNamespaceImpl.outerNamespace_, blob, onDeserialized, entry->first))
          return true;
      } catch (const std::exception& ex) {
        _LOG_ERROR("Namespace::fireOnDeserializeNeeded: Error in onDeserializeNeeded: " <<
                   ex.what());
      } catch (...) {
        _LOG_ERROR("Namespace::fireOnDeserializeNeeded: Error in onDeserializeNeeded.");
      }
    }
  }

  return false;
}

void
Namespace::Impl::defaultOnDeserialized
  (const ptr_lib::shared_ptr<Object>& object,
   const Handler::OnObjectSet& onObjectSet)
{
  if (getIsShutDown())
    return;

  object_ = object;
  setState(NamespaceState_OBJECT_READY);

  if (onObjectSet)
    onObjectSet(outerNamespace_);
}

void
Namespace::Impl::onInterest
   (const ptr_lib::shared_ptr<const Name>& prefix,
    const ptr_lib::shared_ptr<const Interest>& interest, Face& face,
    uint64_t interestFilterId,
    const ptr_lib::shared_ptr<const InterestFilter>& filter)
{
  if (getIsShutDown())
    return;

  Name interestName = interest->getName();
  if (interestName.size() >= 1 && interestName[-1].isImplicitSha256Digest())
    // Strip the implicit digest.
    interestName = interestName.getPrefix(-1);

  if (!name_.isPrefixOf(interestName))
    // No match.
    return;

  // Check if the Namespace node exists and has a matching Data packet.
  Namespace::Impl& interestNamespaceImpl = getChildImpl(interestName);
  if (hasChild(interestName)) {
    Namespace::Impl* bestMatch = findBestMatchName
      (interestNamespaceImpl, *interest, chrono::system_clock::now());
    if (bestMatch) {
      // findBestMatchName makes sure there is a data_ packet.
      face.putData(*bestMatch->data_);
      return;
    }
  }

  // No Data packet found, so save the pending Interest.
  root_->pendingIncomingInterestTable_->add(interest, face);

  // Ask all OnObjectNeeded callbacks if they can produce.
  bool canProduce = false;
  Namespace::Impl* impl = &interestNamespaceImpl;
  while (impl) {
    if (impl->fireOnObjectNeeded(interestNamespaceImpl.outerNamespace_))
      canProduce = true;
    impl = impl->parent_;
  }
  if (canProduce)
    interestNamespaceImpl.setState(NamespaceState_PRODUCING_OBJECT);
}

Namespace::Impl*
Namespace::Impl::findBestMatchName
  (Namespace::Impl& nameSpace, const Interest& interest,
   chrono::system_clock::time_point nowTimePoint)
{
  Namespace::Impl *bestMatch = 0;

  // Search the children backwards which will result in a "less than" name
  // among names of the same length.
  for (map<Name::Component, ptr_lib::shared_ptr<Namespace>>::reverse_iterator
        i = nameSpace.children_.rbegin();
       i != nameSpace.children_.rend(); ++i) {
    Namespace::Impl& child = *i->second->impl_;
    Namespace::Impl* childBestMatch = findBestMatchName
      (child, interest, nowTimePoint);

    if (childBestMatch &&
        (!bestMatch ||
         childBestMatch->name_.size() >= bestMatch->name_.size()))
      bestMatch = childBestMatch;
  }

  if (bestMatch)
    // We have a child match, and it is longer than this name, so return it.
    return bestMatch;

  if (interest.getMustBeFresh() &&
      nameSpace.freshnessExpiryTime_ != chrono::system_clock::time_point::min() &&
      nowTimePoint >= nameSpace.freshnessExpiryTime_)
    // The Data packet is no longer fresh.
    // Debug: When to set the state to OBJECT_READY_BUT_STALE?
    return 0;

  if (nameSpace.data_ && interest.matchesData(*nameSpace.data_))
    return &nameSpace;

  return 0;
}

void
Namespace::Impl::onData
  (const ptr_lib::shared_ptr<const Interest>& interest,
   const ptr_lib::shared_ptr<Data>& data)
{
  if (getIsShutDown())
    return;

  Namespace::Impl& dataNamespaceImpl = getChildImpl(data->getName());
  if (!dataNamespaceImpl.setData(data))
    // A Data packet is already attached.
    return;
  setState(NamespaceState_DATA_RECEIVED);

  // TODO: Start the validator.
  dataNamespaceImpl.setValidateState(NamespaceValidateState_VALIDATING);

  DecryptorV2* decryptor = dataNamespaceImpl.getDecryptor();
  if (!decryptor) {
    dataNamespaceImpl.deserialize_(data->getContent());
    return;
  }

  // Decrypt, then deserialize.
  dataNamespaceImpl.setState(NamespaceState_DECRYPTING);
  ptr_lib::shared_ptr<EncryptedContent> encryptedContent =
   ptr_lib::make_shared<EncryptedContent>();
  try {
    encryptedContent->wireDecodeV2(data->getContent());
  } catch (const std::exception& ex) {
    dataNamespaceImpl.decryptionError_ =
      string("Error decoding the EncryptedContent: ") + ex.what();
    dataNamespaceImpl.setState(NamespaceState_DECRYPTION_ERROR);
    return;
  }

  decryptor->decrypt
    (encryptedContent,
     bind(&Namespace::Impl::deserialize_, shared_from_this(), _1,
          Handler::OnObjectSet()),
     bind(&Namespace::Impl::onDecryptionError, shared_from_this(), _1, _2));
}

void
Namespace::Impl::onTimeout(const ptr_lib::shared_ptr<const Interest>& interest)
{
  if (getIsShutDown())
    return;

  // TODO: Need to detect a timeout on a child node.
  setState(NamespaceState_INTEREST_TIMEOUT);
}

void
Namespace::Impl::onNetworkNack
  (const ptr_lib::shared_ptr<const Interest>& interest,
   const ptr_lib::shared_ptr<NetworkNack>& networkNack)
{
  if (getIsShutDown())
    return;

  // TODO: Need to detect a network nack on a child node.
  networkNack_ = networkNack;
  setState(NamespaceState_INTEREST_NETWORK_NACK);
}

void
Namespace::Impl::onDecryptionError
  (EncryptError::ErrorCode errorCode, const string& message)
{
  if (getIsShutDown())
    return;

  ostringstream decryptionErrorText;
  decryptionErrorText << "Decryptor error " << errorCode << ": " + message;
  decryptionError_ = decryptionErrorText.str();
  setState(NamespaceState_DECRYPTION_ERROR);
}

void
Namespace::Impl::onNamesUpdate
  (const ptr_lib::shared_ptr<std::vector<Name>>& names)
{
  if (getIsShutDown())
    return;

  for (vector<Name>::const_iterator name = names->begin(); name != names->end();
       ++name) {
    if (!name_.isPrefixOf(*name)) {
      _LOG_DEBUG("The Namespace root name is not a prefix of the sync update name " <<
                 *name);
      continue;
    }

    // This will create the name if it doesn't exist.
    getChildImpl(*name);
  }
}

#ifdef NDN_CPP_HAVE_BOOST_ASIO
boost::atomic_uint64_t Namespace::lastCallbackId_;
#else
uint64_t Namespace::lastCallbackId_;
#endif

}
