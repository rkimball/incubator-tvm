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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <functional>

#include "../../support/socket.h"

namespace tvm {
namespace runtime {
namespace rpc {

class PutInfo;

/*!
 * \brief The interface of all remote RPC sessions.
 *
 *  It contains all the necessary interface to implement
 *  remote call and resource management.
 *
 *  The interface is designed to allow easy proxy-chaining
 *  by forward requests to another RPCSession.
 */

/*!
 * \brief The main RPC Tracker class.
 */
class RPCTracker {
 public:
  RPCTracker(std::string host, int port, int port_end, bool silent = true);
  ~RPCTracker();
  static int Start(std::string host, int port, int port_end, bool silent);
  void Stop();
  void Terminate();

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
  /*!
   * \brief The ConnectionInfo class tracks each connection to the RPC Tracker.
   */
  class ConnectionInfo {
   public:
    ConnectionInfo(std::shared_ptr<RPCTracker> tracker, std::string host, int port, support::TCPSocket connection);
    ~ConnectionInfo();
    std::shared_ptr<RPCTracker> tracker_;
    std::thread connection_task_;
    std::string host_;
    int port_;
    support::TCPSocket connection_;
    std::string key_;
    std::set<std::string> pending_match_keys_;
    std::set<std::shared_ptr<PutInfo>> put_values_;

    void ConnectionLoop();
    int SendStatus(std::string status);
    int SendResponse(TRACKER_CODE value);
    int RecvAll(void* data, size_t length);
    int SendAll(const void* data, size_t length);
    void Close();
  private:
    void Fail();
  };
  friend std::ostream& operator<<(std::ostream& out, const ConnectionInfo& info) {
    out << "ConnectionInfo(" << info.host_ << ":" << info.port_ << " key=" << info.key_ << ")";
    return out;
  }
  using response_callback_t = std::function<bool(ConnectionInfo* conn)>;

  /*!
   * \brief The RequestInfo class tracking information from REQUEST messages.
   */
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

  /*!
   * \brief The PutInfo class tracks the information from PUT messages.
   */
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

  /*!
   * \brief The PriorityScheduler handles request messages in a priority order.
   *
   * The priority is passed in the REQUEST message with higher numeric values being processed
   * first.
   */
  class PriorityScheduler {
   public:
    PriorityScheduler(std::string key);
    ~PriorityScheduler();
    void Put(std::string address, int port, std::string match_key,
             std::shared_ptr<ConnectionInfo> conn);
    void Request(std::string user, int priority, std::shared_ptr<ConnectionInfo> conn);
    void Remove(PutInfo value);
    std::string Summary();

    void Schedule();

    std::mutex mutex_;
    std::string key_;
    size_t request_count_ = 0;
    std::deque<PutInfo> values_;
    std::deque<RequestInfo> requests_;
  };

  /*!
   * \brief This method is the loop over the listen call.
   *
   * Each new connection is passed to it's own new thread for processing. After spawning
   * this new connection thread this method returns to listen for new connections.
   */
  void ListenLoopEntry();

  void Put(std::string key, std::string address, int port, std::string match_key,
           ConnectionInfo* conn);
  void Request(std::string key, std::string user, int priority, ConnectionInfo* conn);
  std::string Summary();
  void Close(ConnectionInfo* conn);

  /*!
   * \brief Contains the IP address of the host where the RPC Tracker is instantiated.
   */
  std::string host_;

  /*!
   * \brief Contains the starting port the RPC Tracker uses to start searching a port.
   */
  int port_;

  /*!
   * \brief Contains the ending port the RPC Tracker uses to start searching a port.
   */
  int port_end_;

  /*!
   * \brief The port in use by the RPC Tracker.
   */
  int my_port_;

  bool silent_;

  /*!
   * \brief The port on which the RPC Tracker is listening.
   */
  support::TCPSocket listen_sock_;

  /*!
   * \brief The thread running the Tracker's listen loop.
   */
  std::unique_ptr<std::thread> listener_task_;

  static std::shared_ptr<RPCTracker> rpc_tracker_;

  /*!
   * \brief The map of `key` to PriorityScheduler.
   *
   * Each key has a unique scheduler for that key.
   */
  std::map<std::string, std::shared_ptr<PriorityScheduler>> scheduler_map_;

  /*!
   * \brief The collection of connections currently active.
   */
  std::set<std::shared_ptr<ConnectionInfo>> connection_list_;

  /*!
   * \brief The mutex used to lock access to the RPCTracker.
   *
   * Since connections run in separate threads and interact with the Tracker we need
   * a mutex to keep things safe.
   */
  std::mutex mutex_;

  bool active_;
};
}  // namespace rpc
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_RPC_RPC_TRACKER_H_
