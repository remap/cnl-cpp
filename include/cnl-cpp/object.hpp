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

#ifndef NDN_OBJECT_HPP
#define NDN_OBJECT_HPP

#if defined(_WIN32)
#ifdef CNL_CPP_EXPORTS
#define cnl_cpp_dll __declspec(dllexport)
#else
#define cnl_cpp_dll __declspec(dllimport)
#endif
#else
#define cnl_cpp_dll
#endif

namespace cnl_cpp {

/**
 * Object is a base class for the object returned by Namespace::getObject().
 * This is necessary to provide a virtual destructor for derived classes such as
 * BlobObject.
 */
class cnl_cpp_dll Object {
public:
  virtual ~Object();
};

}

#endif
