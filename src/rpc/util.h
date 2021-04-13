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

#include "../support/arena.h"
#include "../runtime/minrpc/rpc_reference.h"

namespace tvm {
namespace rpc {

class IPAddress {
 public:
  IPAddress() : host_{}, port_{-1} {}
  IPAddress(std::string host, int port) : host_{host}, port_{port} {}
  std::string host_;
  int port_;
  bool is_valid() { return !host_.empty() && port_ >= 0 && port_ < 0xFFFF; }
  operator bool() { return is_valid(); }
};

class StreamReader {
public:
  StreamReader(std::istream& is) : is_{is} {
  }
  template<typename T>
  void Read(T* value) {
    is_.read(reinterpret_cast<char*>(value), sizeof(T));
  }

  template<typename T>
  void ReadArray(T* data, int64_t count) {
    is_.read(reinterpret_cast<char*>(data), sizeof(T) * count);
  }

  void ThrowError(runtime::RPCServerStatus status) {
    std::cout << __FILE__ << " " << __LINE__ << " exception" << std::endl;
    LOG(FATAL) << "RPCServerError:" << status;
  }

  template <typename T>
  T* ArenaAlloc(int count) {
    static_assert(std::is_pod<T>::value, "must be POD");
    return arena_.template allocate_<T>(count);
  }

private:
  /*! \brief arena for dependency graph */
  support::Arena arena_;
  std::istream& is_;
};

}  // namespace rpc
}  // namespace tvm

#endif // SRC_RPC_UTIL_H_
