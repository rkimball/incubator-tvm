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

#include "server.h"

#include <dmlc/json.h>
#include <poll.h>
#include <tvm/runtime/registry.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>

#include "base.h"
#include "tracker.h"

namespace tvm {
namespace rpc {

class ServerConnection : public RPCBase {
 public:
  ServerConnection(support::TCPSocket conn, std::string key);
  ~ServerConnection();
  void ConnectionLoopEntry();
  void ConnectionLoop();
  void ProcessMessage(std::string json);
  bool InitiateRPCSession();

 private:
  std::thread connection_task_;
  std::string host_;
  // int port_;
  std::string key_;
};

ServerObj::ServerObj(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                     std::string tracker_host, int tracker_port, std::string key,
                     std::string load_library, std::string custom_host, int custom_port,
                     bool silent)
    : host_{host},
      key_{key},
      tracker_addr_{tracker_host, tracker_port},
      custom_addr_{custom_host, custom_port},
      load_library_{load_library} {
  if (host == "" || host == "0.0.0.0" || host == "localhost") {
    host_ = "127.0.0.1";
  }

  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port, port_end);
  std::cout << __FILE__ << " " << __LINE__ << " Server bound to port " << my_port_ << ", key "
            << key_ << std::endl;
  std::cout << __FILE__ << " " << __LINE__ << " tracker at " << tracker_host << ":" << tracker_port
            << std::endl;

  // Set socket so we can reuse the address later
  listen_sock_.SetReuseAddress();

  listener_task_ = std::make_unique<std::thread>(&ServerObj::ListenLoopEntry, this);
}

ServerObj::~ServerObj() {}

void ServerObj::ListenLoopEntry() {
  listen_sock_.Listen();
  active_ = true;
  while (active_) {
    support::TCPSocket connection;
    try {
      if (tracker_conn_ == nullptr && tracker_addr_) {
        // There is a tracker address but it is not yet connected
        std::cout << __FILE__ << " " << __LINE__ << " connect to tracker unsupported " << std::endl;
        RegisterWithTracker();
      }

      connection = AcceptWithTimeout(listen_sock_, 1000, []() {
        std::cout << __FILE__ << " " << __LINE__ << " timeout " << std::endl;
      });
      auto server = std::make_shared<ServerConnection>(connection, key_);
      connection_list_.insert(server);
    } catch (std::exception err) {
      break;
    }
  }
}

void ServerObj::Stop() {}

void ServerObj::Terminate() {}

int ServerObj::GetPort() const { return my_port_; }

void ServerObj::RegisterWithTracker() {
  std::cout << __FILE__ << " " << __LINE__ << " RegisterWithTracker unimplemented " << std::endl;
  // exit(-1);
}

TVM_REGISTER_NODE_TYPE(ServerObj);
TVM_REGISTER_GLOBAL("rpc.Server")
    .set_body_typed([](std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                       std::string tracker_host, int tracker_port, std::string key,
                       std::string load_library, std::string custom_host, int custom_port,
                       bool silent) {
      return tvm::rpc::Server(host, port, port_end, is_proxy, use_popen, tracker_host, tracker_port,
                              key, load_library, custom_host, custom_port, silent);
    });

ServerConnection::ServerConnection(support::TCPSocket conn, std::string key) : RPCBase{conn}, key_{key} {
  connection_task_ = std::thread(&ServerConnection::ConnectionLoopEntry, this);
  connection_task_.detach();
}

ServerConnection::~ServerConnection() {}

void ServerConnection::ConnectionLoopEntry() {
  MagicHandshake(RPC_CODE::RPC_MAGIC);
  InitiateRPCSession();

  while (true) {
    std::string json;
    try {
      json = ReceiveJSON();
    } catch (std::exception err) {
      return;
    }

    try {
      ProcessMessage(json);
    } catch (std::exception err) {
      // SendResponse(TrackerObj::TRACKER_CODE::FAIL);
    }
  }
}

bool ServerConnection::InitiateRPCSession() {
  bool rc = false;
  try {
    std::string json = ReceiveJSON();
    std::cout << __FILE__ << " " << __LINE__ << " msg " << json << std::endl;
    static std::regex client_reg("client:(.*)");
    std::smatch sm;
    if (std::regex_match(json, sm, client_reg)) {
      std::cout << __FILE__ << " " << __LINE__ << " client " << sm[1] << std::endl;
      SendJSON("server:"+key_);
      rc = true;
    }
  } catch (std::exception err) {
    return false;
  }
  return rc;
}

void ServerConnection::ConnectionLoop() {}

void ServerConnection::ProcessMessage(std::string json) {
}

}  // namespace rpc
}  // namespace tvm
