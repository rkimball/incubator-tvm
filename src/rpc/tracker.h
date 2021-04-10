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
 * \file tracker.h
 * \brief RPC Tracker.
 */
#ifndef SRC_RPC_TRACKER_H_
#define SRC_RPC_TRACKER_H_

#include <tvm/node/reflection.h>
#include <tvm/runtime/c_runtime_api.h>
#include <tvm/runtime/device_api.h>
#include <tvm/runtime/object.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../support/socket.h"
#include "base.h"

namespace tvm {
namespace rpc {

class ConnectionInfo;
class PriorityScheduler;

/*!
 * \brief The main RPC Tracker class.
 *
 * This is the actual implementation of the Tracker.
 */
class TrackerObj : public Object {
  friend class ConnectionInfo;

 public:
  TrackerObj() {}
  TrackerObj(std::string host, int port, int port_end, bool silent = true);
  ~TrackerObj();
  void Stop();
  void Terminate();
  int GetPort() const;

  void VisitAttrs(tvm::AttrVisitor* av) {
    av->Visit("port", &my_port_);
    av->Visit("host", &host_);
  }

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
  void RemoveStaleConnections();
  std::shared_ptr<PriorityScheduler> GetScheduler(std::string key);

  /*!
   * \brief Contains the IP address of the host where the RPC Tracker is instantiated.
   */
  std::string host_;

  /*!
   * \brief The port in use by the RPC Tracker.
   */
  int my_port_;

  /*!
   * \brief The port on which the RPC Tracker is listening.
   */
  support::TCPSocket listen_sock_;

  /*!
   * \brief The thread running the Tracker's listen loop.
   */
  std::unique_ptr<std::thread> listener_task_;

  /*!
   * \brief The map of 'key' to PriorityScheduler.
   *
   * Each key has a unique scheduler for that key.
   */
  std::map<std::string, std::shared_ptr<PriorityScheduler>> scheduler_map_;

  /*!
   * \brief The collection of connections currently active.
   */
  std::set<std::shared_ptr<ConnectionInfo>> connection_list_;

  /*!
   * \brief The mutex used to lock access to the TrackerObj.
   *
   * Since connections run in separate threads and interact with the Tracker we need
   * a mutex to keep things safe.
   */
  std::mutex mutex_;

  bool active_;

 public:
  static constexpr const char* _type_key = "rpc.Tracker";
  TVM_DECLARE_BASE_OBJECT_INFO(TrackerObj, Object);
};

/*!
 * \brief The interface of all remote RPC sessions.
 *
 *  It contains all the necessary interface to implement
 *  remote call and resource management.
 *
 *  The interface is designed to allow easy proxy-chaining
 *  by forward requests to another RPCSession.
 */

class Tracker : public ObjectRef {
 public:
  explicit Tracker(std::string host, int port, int port_end, bool silent = true) {
    data_ = make_object<TrackerObj>(host, port, port_end, silent);
  }

  TVM_DEFINE_MUTABLE_OBJECT_REF_METHODS(Tracker, ObjectRef, TrackerObj);
};

}  // namespace rpc
}  // namespace tvm

#endif  // SRC_RPC_TRACKER_H_
