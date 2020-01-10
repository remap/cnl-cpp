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

/**
 * This tests using a namespace to automatically decrypt content using
 * Name-based Access Control (NAC), provided by test-nac-producer (which must be
 * running).
 */

#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <ndn-cpp/security/key-chain.hpp>
#include <ndn-cpp/security/validator-null.hpp>
#include <cnl-cpp/segmented-object-handler.hpp>

using namespace std;
using namespace ndn;
using namespace cnl_cpp;

// This is the same as MEMBER_PUBLIC_KEY in test-nac-producer.
static uint8_t MEMBER_PUBLIC_KEY[] = {
  0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01,
  0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01,
  0x00, 0xd2, 0x1c, 0x8d, 0x80, 0x78, 0xcc, 0x92, 0xb7, 0x6e, 0xfd, 0x28, 0xdc, 0xb4, 0xa7, 0x81,
  0x98, 0xa4, 0x31, 0x02, 0x01, 0x49, 0x58, 0xc4, 0x27, 0x0e, 0x7d, 0xe2, 0xa4, 0xca, 0xd0, 0x98,
  0x2a, 0xb6, 0x0d, 0xff, 0x14, 0x36, 0xbf, 0x3e, 0xb9, 0xa1, 0x9c, 0x8b, 0x5b, 0xd8, 0x47, 0x12,
  0x3e, 0xfe, 0x66, 0xb7, 0x73, 0x5f, 0x54, 0xc3, 0xe7, 0x3d, 0x03, 0xe5, 0xab, 0x56, 0x3f, 0xbf,
  0x25, 0xa6, 0xe6, 0x9b, 0x74, 0x83, 0xab, 0x30, 0xba, 0xab, 0xff, 0x20, 0xb6, 0xb7, 0xee, 0x4f,
  0x77, 0x9e, 0xc3, 0xfe, 0xb8, 0xef, 0x2a, 0x15, 0x2d, 0x24, 0x68, 0x49, 0x58, 0xe5, 0x3b, 0x50,
  0xbb, 0x4e, 0x72, 0x92, 0xbb, 0xfc, 0x98, 0x62, 0xe2, 0x58, 0xc7, 0x2d, 0xf6, 0x46, 0xb1, 0x07,
  0xbc, 0x68, 0x68, 0x29, 0xf6, 0x31, 0x79, 0x9c, 0xc6, 0x00, 0xc3, 0x5d, 0xce, 0x4a, 0xcf, 0x26,
  0xfb, 0xf6, 0x9b, 0x3b, 0x7a, 0xa6, 0xfa, 0x89, 0xaa, 0xc9, 0xc0, 0xf2, 0x08, 0x46, 0xcd, 0x45,
  0xf6, 0x38, 0xab, 0x90, 0x1e, 0xd6, 0xa1, 0x6e, 0x48, 0xa0, 0xe5, 0x5f, 0x59, 0x35, 0x2c, 0x0d,
  0xe9, 0x3d, 0x3c, 0x9f, 0x8d, 0x28, 0xec, 0x24, 0xbc, 0x63, 0x43, 0x75, 0x00, 0x07, 0xf3, 0x45,
  0xf4, 0x93, 0x35, 0x42, 0x4c, 0x90, 0xea, 0x4f, 0x0a, 0x44, 0x4e, 0xda, 0x7a, 0xd5, 0xad, 0x8d,
  0x12, 0x21, 0xc5, 0x63, 0x74, 0xc2, 0x80, 0x2f, 0xe6, 0x27, 0x54, 0xc3, 0xf8, 0xd6, 0x24, 0x7e,
  0x44, 0x95, 0xc8, 0x3e, 0x1f, 0x43, 0x8e, 0x56, 0x67, 0xbd, 0xb5, 0x9d, 0xfd, 0x9a, 0xad, 0x5a,
  0x0a, 0x88, 0xc3, 0x8c, 0xb9, 0xaf, 0x29, 0x71, 0x51, 0x70, 0x87, 0x6f, 0xee, 0x0e, 0xd0, 0x27,
  0x3b, 0x95, 0xeb, 0x66, 0xe1, 0x4a, 0x4b, 0x67, 0xab, 0x90, 0x43, 0xd5, 0x51, 0xad, 0x57, 0x08,
  0xd5, 0x02, 0x03, 0x01, 0x00, 0x01
};

// This matches MEMBER_PUBLIC_KEY in test-nac-producer.
static uint8_t MEMBER_PRIVATE_KEY[] = {
  0x30, 0x82, 0x04, 0xbd, 0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
  0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x04, 0x82, 0x04, 0xa7, 0x30, 0x82, 0x04, 0xa3, 0x02, 0x01,
  0x00, 0x02, 0x82, 0x01, 0x01, 0x00, 0xd2, 0x1c, 0x8d, 0x80, 0x78, 0xcc, 0x92, 0xb7, 0x6e, 0xfd,
  0x28, 0xdc, 0xb4, 0xa7, 0x81, 0x98, 0xa4, 0x31, 0x02, 0x01, 0x49, 0x58, 0xc4, 0x27, 0x0e, 0x7d,
  0xe2, 0xa4, 0xca, 0xd0, 0x98, 0x2a, 0xb6, 0x0d, 0xff, 0x14, 0x36, 0xbf, 0x3e, 0xb9, 0xa1, 0x9c,
  0x8b, 0x5b, 0xd8, 0x47, 0x12, 0x3e, 0xfe, 0x66, 0xb7, 0x73, 0x5f, 0x54, 0xc3, 0xe7, 0x3d, 0x03,
  0xe5, 0xab, 0x56, 0x3f, 0xbf, 0x25, 0xa6, 0xe6, 0x9b, 0x74, 0x83, 0xab, 0x30, 0xba, 0xab, 0xff,
  0x20, 0xb6, 0xb7, 0xee, 0x4f, 0x77, 0x9e, 0xc3, 0xfe, 0xb8, 0xef, 0x2a, 0x15, 0x2d, 0x24, 0x68,
  0x49, 0x58, 0xe5, 0x3b, 0x50, 0xbb, 0x4e, 0x72, 0x92, 0xbb, 0xfc, 0x98, 0x62, 0xe2, 0x58, 0xc7,
  0x2d, 0xf6, 0x46, 0xb1, 0x07, 0xbc, 0x68, 0x68, 0x29, 0xf6, 0x31, 0x79, 0x9c, 0xc6, 0x00, 0xc3,
  0x5d, 0xce, 0x4a, 0xcf, 0x26, 0xfb, 0xf6, 0x9b, 0x3b, 0x7a, 0xa6, 0xfa, 0x89, 0xaa, 0xc9, 0xc0,
  0xf2, 0x08, 0x46, 0xcd, 0x45, 0xf6, 0x38, 0xab, 0x90, 0x1e, 0xd6, 0xa1, 0x6e, 0x48, 0xa0, 0xe5,
  0x5f, 0x59, 0x35, 0x2c, 0x0d, 0xe9, 0x3d, 0x3c, 0x9f, 0x8d, 0x28, 0xec, 0x24, 0xbc, 0x63, 0x43,
  0x75, 0x00, 0x07, 0xf3, 0x45, 0xf4, 0x93, 0x35, 0x42, 0x4c, 0x90, 0xea, 0x4f, 0x0a, 0x44, 0x4e,
  0xda, 0x7a, 0xd5, 0xad, 0x8d, 0x12, 0x21, 0xc5, 0x63, 0x74, 0xc2, 0x80, 0x2f, 0xe6, 0x27, 0x54,
  0xc3, 0xf8, 0xd6, 0x24, 0x7e, 0x44, 0x95, 0xc8, 0x3e, 0x1f, 0x43, 0x8e, 0x56, 0x67, 0xbd, 0xb5,
  0x9d, 0xfd, 0x9a, 0xad, 0x5a, 0x0a, 0x88, 0xc3, 0x8c, 0xb9, 0xaf, 0x29, 0x71, 0x51, 0x70, 0x87,
  0x6f, 0xee, 0x0e, 0xd0, 0x27, 0x3b, 0x95, 0xeb, 0x66, 0xe1, 0x4a, 0x4b, 0x67, 0xab, 0x90, 0x43,
  0xd5, 0x51, 0xad, 0x57, 0x08, 0xd5, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02, 0x82, 0x01, 0x00, 0x1b,
  0xb6, 0x70, 0x63, 0x75, 0x8b, 0x65, 0xfe, 0x93, 0xc1, 0x08, 0x56, 0xb3, 0xed, 0x34, 0x06, 0xb2,
  0x01, 0x54, 0xc6, 0x5e, 0xaa, 0x3a, 0x94, 0xfc, 0x30, 0x56, 0x68, 0x6d, 0xe7, 0xbd, 0x6a, 0x44,
  0xc0, 0xeb, 0xd7, 0xfa, 0xb0, 0x96, 0xd1, 0x5d, 0x23, 0x8c, 0xad, 0xfc, 0x84, 0xc3, 0x3f, 0x53,
  0xc6, 0xb0, 0x83, 0xa3, 0x1b, 0x47, 0x0b, 0x84, 0xdc, 0xb2, 0xba, 0x3a, 0x92, 0x05, 0xe0, 0x2d,
  0x77, 0x55, 0x21, 0xb5, 0x0c, 0x2b, 0x4d, 0x33, 0xca, 0x5e, 0xc2, 0x3a, 0xd8, 0x4f, 0xda, 0xf3,
  0x31, 0xde, 0xb2, 0xfd, 0xb8, 0x8b, 0x3a, 0x7d, 0x06, 0xd4, 0x45, 0xc9, 0x56, 0x5d, 0x7b, 0x97,
  0x09, 0x3d, 0x99, 0x55, 0xe3, 0xb3, 0x87, 0x2f, 0x36, 0x76, 0x35, 0x79, 0x75, 0x78, 0x13, 0xbd,
  0xa1, 0x9f, 0x87, 0x3d, 0x57, 0x50, 0xfa, 0x3c, 0xb7, 0x87, 0xf3, 0xe1, 0xa2, 0x18, 0xfd, 0xfd,
  0x80, 0xbd, 0xb2, 0x44, 0x9e, 0x93, 0x0a, 0x65, 0x2e, 0x19, 0x07, 0xf4, 0x77, 0xfe, 0xd6, 0x05,
  0xfd, 0xab, 0x62, 0xfc, 0x26, 0x8f, 0x70, 0x10, 0x80, 0x8a, 0x56, 0xaa, 0x00, 0xc3, 0x68, 0xd8,
  0x77, 0x88, 0x3e, 0x8e, 0x4a, 0x35, 0x01, 0x38, 0x28, 0x61, 0xbb, 0x62, 0x31, 0x87, 0xd8, 0x9c,
  0xda, 0x9e, 0x4e, 0x08, 0xde, 0x1d, 0xc0, 0xcd, 0x7f, 0xbc, 0x75, 0xa7, 0x8d, 0xcd, 0x06, 0x48,
  0x3b, 0x81, 0x5d, 0xd5, 0xc1, 0xf3, 0x56, 0x07, 0x11, 0x17, 0xe0, 0xf8, 0x8e, 0x3c, 0xa1, 0x8e,
  0x88, 0x62, 0x82, 0x11, 0xe7, 0x78, 0xf7, 0xdf, 0xad, 0x7a, 0x94, 0xa6, 0x2b, 0xee, 0xc5, 0xc7,
  0xda, 0x2b, 0x0c, 0x58, 0x38, 0xf0, 0x7e, 0x8c, 0x6d, 0x32, 0xc7, 0x3b, 0xf8, 0x98, 0x12, 0xbc,
  0xa4, 0x71, 0xb3, 0xf0, 0x15, 0xa2, 0x44, 0x48, 0x64, 0x07, 0x04, 0xca, 0x87, 0x30, 0x01, 0x02,
  0x81, 0x81, 0x00, 0xee, 0x6e, 0x5b, 0xb0, 0xb7, 0xe9, 0x3b, 0x28, 0x5b, 0x21, 0x97, 0x53, 0x43,
  0xcd, 0x77, 0x67, 0x36, 0x97, 0xb1, 0x36, 0x2b, 0xe9, 0x08, 0x72, 0x9d, 0xbc, 0xcd, 0x99, 0xc1,
  0xd9, 0xe4, 0xbf, 0xea, 0xaa, 0x19, 0x8b, 0xef, 0xa0, 0xf5, 0xcd, 0xcd, 0x15, 0xe2, 0xa6, 0xfb,
  0x5a, 0x83, 0x60, 0xad, 0xb8, 0xbe, 0xfc, 0x49, 0x55, 0x04, 0x38, 0x82, 0x3f, 0xb7, 0x08, 0x97,
  0xc8, 0x14, 0x24, 0x83, 0x35, 0x45, 0xd9, 0xfe, 0xd0, 0xb4, 0x2a, 0x83, 0x6b, 0x69, 0x23, 0xcc,
  0x4b, 0x5a, 0xfb, 0xdc, 0x11, 0xeb, 0x65, 0x38, 0x2b, 0xd1, 0xe0, 0x4c, 0x34, 0xb2, 0x30, 0x3f,
  0x13, 0x92, 0x8f, 0xa8, 0x61, 0x38, 0x6b, 0x4e, 0xb2, 0xf4, 0xa7, 0xa7, 0x28, 0xa5, 0xb4, 0xfc,
  0x59, 0x56, 0x20, 0x5b, 0xba, 0xf6, 0x33, 0x44, 0x9e, 0x5d, 0x87, 0x18, 0x08, 0x8f, 0x64, 0x92,
  0xa2, 0x35, 0xd5, 0x02, 0x81, 0x81, 0x00, 0xe1, 0x97, 0xfd, 0x32, 0x77, 0xfc, 0x95, 0xea, 0x14,
  0xcf, 0xfa, 0x37, 0x83, 0xa6, 0xfa, 0xcd, 0xe3, 0x49, 0x41, 0xce, 0x80, 0x07, 0x11, 0xe2, 0xce,
  0xe5, 0xe8, 0x9a, 0xf4, 0x1b, 0xa0, 0x2b, 0x75, 0x63, 0xb5, 0x90, 0xba, 0x0a, 0x85, 0xe6, 0xbb,
  0xcd, 0x52, 0xbd, 0x63, 0xe0, 0x9b, 0x6b, 0xd5, 0xc0, 0xe4, 0x32, 0x91, 0xda, 0xfd, 0x60, 0x4a,
  0xc0, 0x10, 0xb0, 0x14, 0x54, 0xed, 0xf7, 0xc5, 0x21, 0x11, 0x5c, 0xf3, 0xc9, 0xbf, 0xf7, 0xfc,
  0x4f, 0xbd, 0x18, 0x8c, 0xc6, 0x26, 0x03, 0xcd, 0xe9, 0x77, 0xf8, 0x58, 0x1a, 0x86, 0x71, 0xa0,
  0x7e, 0x5d, 0xd1, 0xd6, 0x4f, 0xd4, 0x75, 0xd2, 0xc3, 0x25, 0x8e, 0x9b, 0x10, 0x77, 0xf0, 0x99,
  0xc1, 0x60, 0x11, 0x11, 0x85, 0x81, 0x48, 0x1a, 0x95, 0x6b, 0x93, 0x13, 0xf6, 0xa7, 0x86, 0x1f,
  0x9c, 0x8c, 0x9f, 0xf0, 0x4c, 0x07, 0x01, 0x02, 0x81, 0x80, 0x21, 0xc2, 0xfe, 0xb8, 0xc7, 0x51,
  0xff, 0x4e, 0x77, 0x99, 0x0a, 0x14, 0x80, 0x45, 0x57, 0xe3, 0x05, 0x97, 0xf5, 0x3f, 0xf6, 0x77,
  0xc8, 0xfa, 0x71, 0xdb, 0x8a, 0x41, 0x7b, 0x71, 0x9f, 0x32, 0x8d, 0xc8, 0x08, 0x56, 0x08, 0x58,
  0x82, 0x75, 0xe1, 0xd4, 0x77, 0x83, 0xad, 0x93, 0xe3, 0x86, 0x8d, 0x12, 0xdb, 0xf8, 0x5d, 0x69,
  0xec, 0x6f, 0x14, 0x02, 0x71, 0xa8, 0x85, 0xd5, 0x8f, 0x04, 0x9c, 0x8f, 0xae, 0x94, 0x6f, 0xc0,
  0x9d, 0xc2, 0x67, 0x59, 0x8e, 0x49, 0xc3, 0x63, 0xe8, 0x3e, 0x41, 0xab, 0x47, 0xe9, 0xcd, 0x4a,
  0x67, 0x2d, 0x9b, 0x9c, 0xda, 0x9e, 0x7a, 0x50, 0x0b, 0x30, 0xcc, 0x66, 0xf7, 0xd6, 0x3a, 0x0e,
  0x9d, 0x16, 0x20, 0x55, 0x61, 0x21, 0x7f, 0x9a, 0x26, 0xd7, 0xee, 0x25, 0x4b, 0x37, 0x77, 0x3f,
  0xf5, 0x7e, 0x6b, 0xa8, 0xca, 0xa5, 0x33, 0x1e, 0x45, 0x01, 0x02, 0x81, 0x81, 0x00, 0xae, 0x58,
  0x81, 0x54, 0xf4, 0xc8, 0x1f, 0xb0, 0x15, 0xbf, 0x9a, 0x18, 0x37, 0x45, 0xe0, 0x45, 0x28, 0x27,
  0xe0, 0x94, 0xcf, 0xfb, 0x26, 0xc6, 0x8b, 0xb1, 0xc1, 0x2f, 0xa8, 0x02, 0x85, 0xa9, 0xb0, 0x82,
  0x8b, 0xba, 0xbb, 0x1d, 0x10, 0xd8, 0xfe, 0x41, 0x33, 0x75, 0xac, 0xef, 0xd4, 0x0d, 0xe5, 0xd7,
  0xba, 0x44, 0x9e, 0xd6, 0x88, 0xc5, 0x57, 0x5f, 0xd2, 0x45, 0xd2, 0xa0, 0xc4, 0x7c, 0x9d, 0x3b,
  0xee, 0x28, 0x51, 0x3c, 0x95, 0x80, 0xf8, 0xdd, 0x43, 0x3d, 0xea, 0xe5, 0xe4, 0x51, 0x42, 0x5c,
  0xf1, 0xdb, 0xdb, 0x73, 0x3c, 0x7e, 0x2a, 0x54, 0x1e, 0xfb, 0xe1, 0xce, 0x36, 0x5b, 0x8c, 0xb4,
  0x46, 0x9d, 0x4c, 0x97, 0xd0, 0xaa, 0x00, 0x9a, 0x23, 0x3d, 0x6d, 0xb6, 0x28, 0xf0, 0xe9, 0xa2,
  0x9a, 0xcd, 0xc3, 0x3a, 0xf7, 0xc3, 0x3f, 0x41, 0x04, 0xa9, 0x42, 0xd3, 0xef, 0x01, 0x02, 0x81,
  0x80, 0x63, 0xff, 0xe8, 0x8a, 0x26, 0x81, 0xec, 0x43, 0xb4, 0x29, 0xdf, 0x9c, 0xdd, 0xaa, 0x96,
  0xa6, 0xf9, 0x37, 0x3e, 0x58, 0xf0, 0x65, 0xdc, 0x68, 0x61, 0xc6, 0x19, 0xd4, 0xf1, 0x78, 0x38,
  0x40, 0xfa, 0x95, 0x74, 0x0c, 0xe7, 0x93, 0xce, 0x89, 0xf3, 0x27, 0xc4, 0x50, 0x34, 0x09, 0x0a,
  0x29, 0x3b, 0xef, 0x5b, 0x52, 0x58, 0xca, 0xb4, 0x52, 0xff, 0x8f, 0x3d, 0xa8, 0x1c, 0x88, 0x7a,
  0x1b, 0xb9, 0x38, 0xba, 0x84, 0xeb, 0xa9, 0x35, 0xaa, 0xf0, 0x75, 0x3d, 0x91, 0xd7, 0x56, 0x54,
  0x22, 0x50, 0xd8, 0x91, 0xa1, 0x89, 0x79, 0x4e, 0xe9, 0xcf, 0xb4, 0x22, 0xc1, 0x93, 0x6f, 0x37,
  0xb2, 0x7b, 0xd1, 0xf1, 0x4d, 0x53, 0x3b, 0x08, 0x0e, 0xf7, 0xcb, 0x9e, 0x07, 0x6f, 0x99, 0xdd,
  0xc3, 0x0a, 0x7a, 0x7e, 0xd8, 0x47, 0x58, 0xd8, 0x23, 0x09, 0x22, 0xbe, 0x85, 0x53, 0x5f, 0x03,
  0x1d
};

int main(int argc, char** argv)
{
  try {
    // Silence the warning from Interest wire encode.
    Interest::setDefaultCanBePrefix(true);

    // The default Face will connect using a Unix socket, or to "localhost".
    Face face;

    Name memberName("/first/user");
    Name memberKeyName = Name(memberName).append(Name("/KEY/%0C%87%EB%E6U%27B%D6"));

    KeyChain memberKeyChain("pib-memory:", "tpm-memory:");
    memberKeyChain.importSafeBag(SafeBag
      (memberKeyName, Blob(MEMBER_PRIVATE_KEY, sizeof(MEMBER_PRIVATE_KEY)),
       Blob(MEMBER_PUBLIC_KEY, sizeof(MEMBER_PUBLIC_KEY))));
    // TODO: Use a real Validator.
    ValidatorNull validator;
    DecryptorV2 decryptor
      (memberKeyChain.getPib().getIdentity(memberName)->getDefaultKey().get(),
       &validator, &memberKeyChain, &face);

    Name contentPrefix("/testname/content");
    Namespace contentNamespace(contentPrefix);
    contentNamespace.setFace(&face);
    contentNamespace.setDecryptor(&decryptor);

    bool enabled = true;
    // This is called to print the content after it is decrypted and re-assembled
    // from segments.
    auto onObject = [&](Namespace& objectNamespace) {
      cout << "Got segmented content " <<
        objectNamespace.getBlobObject().toRawStr() << endl;
      enabled = false;
    };
    SegmentedObjectHandler(&contentNamespace, onObject).objectNeeded();

    while (enabled) {
      face.processEvents();
      // We need to sleep for a few milliseconds so we don't use 100% of the CPU.
      usleep(10000);
    }
  } catch (std::exception& e) {
    cout << "exception: " << e.what() << endl;
  }
  return 0;
}
