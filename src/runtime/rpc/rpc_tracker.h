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

#include <deque>
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
  class ConnectionInfo : public std::enable_shared_from_this<ConnectionInfo> {
   public:
    ConnectionInfo(RPCTracker* tracker, std::string host, int port, support::TCPSocket connection);
    RPCTracker* tracker_;
    std::future<void> connection_task_;
    std::string host_;
    int port_;
    support::TCPSocket connection_;
    std::string key_;
    std::vector<std::string> pending_match_keys_;

    void ConnectionLoop();
    void SendStatus(std::string status);
    void SendResponse(TRACKER_CODE value);
  };
  friend std::ostream& operator<<(std::ostream& out, const ConnectionInfo& info) {
    out << "ConnectionInfo(" << info.host_ << ":" << info.port_ << " key=" << info.key_ << ")";
    return out;
  }
  using response_callback_t = std::function<bool(ConnectionInfo* conn)>;

  class RequestInfo {
   public:
    RequestInfo() = default;
    RequestInfo(const RequestInfo&) = default;
    RequestInfo(std::string user, int priority, int request_count,
                std::shared_ptr<ConnectionInfo> conn)
        : user_{user}, priority_{priority}, request_count_{request_count}, conn_{conn} {}

    friend std::ostream& operator<<(std::ostream& out, const RequestInfo& info) {
      out << "RequestInfo(" << info.priority_ << ", " << info.user_ << ", " << info.request_count_
          << ")";
      return out;
    }
    std::string user_;
    int priority_;
    int request_count_;
    std::shared_ptr<ConnectionInfo> conn_;
  };

  class PutInfo {
   public:
    PutInfo(std::string address, int port, std::string match_key,
            std::shared_ptr<ConnectionInfo> conn)
        : address_{address}, port_{port}, match_key_{match_key}, conn_{conn} {}
    std::string address_;
    int port_;
    std::string match_key_;
    std::shared_ptr<ConnectionInfo> conn_;

    bool operator==(const PutInfo& pi) { return pi.match_key_ == match_key_; }
  };

  class PriorityScheduler {
   public:
    PriorityScheduler(std::string key);
    void Put(std::string address, int port, std::string match_key,
             std::shared_ptr<ConnectionInfo> conn);
    void Request(std::string user, int priority, std::shared_ptr<ConnectionInfo> conn);
    void Remove(PutInfo value);
    void Summary();

    void Schedule();

    std::string key_;
    size_t request_count_ = 0;
    std::deque<PutInfo> values_;
    std::deque<RequestInfo> requests_;
  };

  void ListenLoopEntry();
  void Put(std::string key, std::string address, int port, std::string match_key,
           std::shared_ptr<ConnectionInfo> conn);
  void Request(std::string key, std::string user, int priority,
               std::shared_ptr<ConnectionInfo> conn);
  std::string Summary();
  void Stop();

  std::string host_;
  int port_;
  int my_port_;
  int port_end_;
  bool silent_;
  support::TCPSocket listen_sock_;
  std::future<void> listener_task_;
  static std::unique_ptr<RPCTracker> rpc_tracker_;
  std::map<std::string, PriorityScheduler> scheduler_map_;
  std::deque<std::shared_ptr<ConnectionInfo>> connection_list_;
};
}  // namespace rpc
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_RPC_RPC_TRACKER_H_
