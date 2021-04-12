/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef SRC_RPC_UTIL_H_
#define SRC_RPC_UTIL_H_

#include <istream>
#include <ostream>
#include <streambuf>
#include <string>

class IPAddress {
 public:
  IPAddress() : host_{}, port_{-1} {}
  IPAddress(std::string host, int port) : host_{host}, port_{port} {}
  std::string host_;
  int port_;
  bool is_valid() { return !host_.empty() && port_ >= 0 && port_ < 0xFFFF; }
  operator bool() { return is_valid(); }
};

// class IMemBuf: std::streambuf
// {
// public:
// 	IMemBuf(const char* base, size_t size)
// 	{
// 		char* p(const_cast<char*>(base));
// 		this->setg(p, p, p + size);
// 	}
// };

// class IMemStream: virtual IMemBuf, std::istream
// {
// public:
// 	IMemStream(const char* mem, size_t size) :
// 		IMemBuf(mem, size),
// 		std::istream(static_cast<std::streambuf*>(this))
// 	{
// 	}
// };

// class OMemBuf: std::streambuf
// {
// public:
// 	OMemBuf(char* base, size_t size)
// 	{
// 		this->setp(base, base + size);
// 	}
// };

// class OMemStream: virtual OMemBuf, std::ostream
// {
// public:
// 	OMemStream(char* mem, size_t size) :
// 		OMemBuf(mem, size),
// 		std::ostream(static_cast<std::streambuf*>(this))
// 	{
// 	}
// };

#endif  // SRC_RPC_UTIL_H_
