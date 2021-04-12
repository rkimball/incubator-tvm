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

#ifndef SRC_RPC_BASE_H_
#define SRC_RPC_BASE_H_

#include <functional>
#include <sstream>

#include "../support/socket.h"

namespace tvm {
namespace rpc {

enum class RPC_TRANSPORT_CODE : int {
  // Magic header for RPC data plane
  RPC_MAGIC = 0xFF271,
  // magic header for RPC tracker(control plane)
  RPC_TRACKER_MAGIC = 0x2F271,
  // sucess response
  RPC_CODE_SUCCESS = RPC_MAGIC + 0,
  // duplicate key in proxy
  RPC_CODE_DUPLICATE = RPC_MAGIC + 1,
  // cannot found matched key in server
  RPC_CODE_MISMATCH = RPC_MAGIC + 2
};

class RPCBase {
 public:
  RPCBase(support::TCPSocket conn) : connection_{conn} {}
  int RecvAll(void* data, size_t length);
  int SendAll(const void* data, size_t length);
  std::string ReceiveJSON();
  void SendJSON(std::string data);
  std::stringstream ReceivePacket();
  void SendPacket(std::stringstream);

  bool MagicHandshake(RPC_TRANSPORT_CODE);

 protected:
  support::TCPSocket connection_;
};

support::TCPSocket AcceptWithTimeout(support::TCPSocket listen_sock, int timeout_ms,
                                     std::function<void()> timeout_callback);

}  // namespace rpc
}  // namespace tvm

#endif  // SRC_RPC_BASE_H_
