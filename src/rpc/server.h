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
 * \file server.h
 * \brief RPC Server.
 */

#ifndef SRC_RPC_SERVER_H_
#define SRC_RPC_SERVER_H_

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

namespace tvm {
namespace rpc {

class ServerConnection;

/*!
 * \brief The main RPC Server class.
 *
 * This is the actual implementation of the Server.
 */
class ServerObj : public Object {
  friend class ConnectionInfo;

 public:
  ServerObj() {}

  /*!
   * \brief Start RPC server on a separate process.*
   *     This is a simple python implementation based on multi-processing.
   *     It is also possible to implement a similar C based server with
   *     TVM runtime which does not depend on the python.*
   *     Parameters
   *     ----------
   * \param host The network interface to bind to or 0.0.0.0 for all interfaces
   * \param port The start of the port range to search for an open port to bind to
   * \param port_end The end of the port range to search for an open port to bind to
   * \param is_proxy
   *         Whether the address specified is a proxy.
   *         If this is true, the host and port actually corresponds to the
   *         address of the proxy server.
   * \param use_popen
   *         Whether to use Popen to start a fresh new process instead of fork.
   *         This is recommended to switch on if we want to do local RPC demonstration
   *         for GPU devices to avoid fork safety issues.
   * \param tracker_host
   *         The address of RPC Tracker in tuple(host, ip) format.
   *         If is not None, the server will register itself to the tracker.
   * \param tracker_port
   *         The address of RPC Tracker in tuple(host, ip) format.
   *         If is not None, the server will register itself to the tracker.
   * \param key
   *         The key used to identify the device type in tracker.
   * \param load_library
   *         List of additional libraries to be loaded during execution.
   * \param custom_addr
   *         Custom IP Address to Report to RPC Tracker
   * \param silent
   *         Whether run this server in silent mode.
   */

  ServerObj(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
            std::string tracker_host, int tracker_port, std::string key, std::string load_library,
            std::string custom_host, int custom_port, bool silent = true);
  ~ServerObj();
  void Stop();
  void Terminate();
  int GetPort() const;

  void VisitAttrs(tvm::AttrVisitor* av) {
    av->Visit("port", &my_port_);
    av->Visit("host", &host_);
  }

 private:
  /*!
   * \brief This method is the loop over the listen call.
   *
   * Each new connection is passed to it's own new thread for processing. After spawning
   * this new connection thread this method returns to listen for new connections.
   */
  void ListenLoopEntry();

  /*!
   * \brief The port in use by the RPC Server.
   */
  int my_port_;

  std::string host_;
  std::string key_;

  /*!
   * \brief The port on which the RPC Server is listening.
   */
  support::TCPSocket listen_sock_;

  /*!
   * \brief The thread running the Server's listen loop.
   */
  std::unique_ptr<std::thread> listener_task_;

  /*!
   * \brief The mutex used to lock access to the ServerObj.
   *
   * Since connections run in separate threads and interact with the Server we need
   * a mutex to keep things safe.
   */
  std::mutex mutex_;

  bool active_;

  std::set<std::shared_ptr<ServerConnection>> connection_list_;

 public:
  static constexpr const char* _type_key = "rpc.Server";
  TVM_DECLARE_BASE_OBJECT_INFO(ServerObj, Object);
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

class Server : public ObjectRef {
 public:
  explicit Server(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                  std::string tracker_host, int tracker_port, std::string key,
                  std::string load_library = "", std::string custom_host = "", int custom_port = -1,
                  bool silent = true) {
    data_ =
        make_object<ServerObj>(host, port, port_end, is_proxy, use_popen, tracker_host,
                               tracker_port, key, load_library, custom_host, custom_port, silent);
  }

  TVM_DEFINE_MUTABLE_OBJECT_REF_METHODS(Server, ObjectRef, ServerObj);
};

}  // namespace rpc
}  // namespace tvm

#endif  // SRC_RPC_SERVER_H_
