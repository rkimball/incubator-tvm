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

/*!
 * \file rpc_tracker.h
 * \brief RPC Tracker.
 */
#ifndef TVM_RUNTIME_RPC_RPC_TRACKER_H_
#define TVM_RUNTIME_RPC_RPC_TRACKER_H_

#include <future>
#include <map>
#include <memory>
#include <string>

#include "../../support/socket.h"

namespace tvm {
namespace runtime {
namespace rpc {
class RPCTracker {
 public:
  RPCTracker(std::string host, int port, int port_end, bool silent);
  ~RPCTracker();
  static int Start(std::string host, int port, int port_end, bool silent);

  static RPCTracker* GetTracker();
  int GetPort() const;

  enum class RPC_CODE : int {
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

  enum class TRACKER_CODE : int {
    FAIL = -1,
    SUCCESS = 0,
    PING = 1,
    STOP = 2,
    PUT = 3,
    REQUEST = 4,
    UPDATE_INFO = 5,
    SUMMARY = 6,
    GET_PENDING_MATCHKEYS = 7
  };

 private:
  class PriorityScheduler {
   public:
    PriorityScheduler(std::string key);
    void Put();
    void Request();
    void Remove();
    void Summary();

    std::string key_;
    size_t request_count_ = 0;
  };

  void ListenLoopEntry();
  void SendResponse(support::TCPSocket& conn, TRACKER_CODE value);

  std::string host_;
  int port_;
  int my_port_;
  int port_end_;
  bool silent_;
  support::TCPSocket listen_sock_;
  std::future<void> listener_task_;
  static std::unique_ptr<RPCTracker> rpc_tracker_;
  std::map<std::string, PriorityScheduler> scheduler_map_;
};
}  // namespace rpc
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_RPC_RPC_TRACKER_H_
