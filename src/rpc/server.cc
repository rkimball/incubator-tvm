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
#include <tvm/runtime/registry.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <poll.h>

#include "tracker.h"
#include "base.h"

namespace tvm {
namespace rpc {

class ServerConnection : public RPCBase {
 public:
  ServerConnection(support::TCPSocket conn);
  ~ServerConnection();
  void ConnectionLoopEntry();
  void ConnectionLoop();
  void ProcessMessage(std::string json);

 private:
  std::thread connection_task_;
  std::string host_;
  int port_;
};

ServerObj::ServerObj(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                     std::string tracker_host, int tracker_port, std::string key,
                     std::string load_library, std::string custom_host, int custom_port,
                     bool silent)
    : host_{host}, key_{key}, tracker_addr_{tracker_host, tracker_port}, custom_addr_{custom_host, custom_port}, load_library_{load_library} {
  if (host == "" || host == "0.0.0.0" || host == "localhost") {
    host_ = host;
  }

  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port, port_end);
  std::cout << __FILE__ << " " << __LINE__ << " Server bound to port " << my_port_ << ", key " << key_ << std::endl;
  std::cout << __FILE__ << " " << __LINE__ << " tracker at " << tracker_host << ":" << tracker_port << std::endl;

  if (tracker_host != "") {
    RegisterWithTracker();
  }

  // Set socket so we can reuse the address later
  listen_sock_.SetReuseAddress();

  listen_sock_.Listen();
  listener_task_ = std::make_unique<std::thread>(&ServerObj::ListenLoopEntry, this);
}

ServerObj::~ServerObj() {}

void ServerObj::ListenLoopEntry() {
  active_ = true;
  while (active_) {
    support::TCPSocket connection;
    try {
      if (tracker_conn_ == nullptr && tracker_addr_) {
        // There is a track address but it is not yet connected
        std::cout << __FILE__ << " " << __LINE__ << " connect to tracker unsupported " << std::endl;
      }

      int numfds = 1;
      const int timeout_ms = 1000;
      pollfd poll_set[1];
      memset(poll_set, '\0', sizeof(poll_set));
      poll_set[0].fd = listen_sock_.sockfd;
      poll_set[0].events = POLLIN;
      poll(poll_set, numfds, timeout_ms);
      std::cout << __FILE__ << " " << __LINE__ << " post poll" << std::endl;
      if( poll_set[0].revents & POLLIN ) {
        // index 0 is the listener socket
        std::cout << __FILE__ << " " << __LINE__ << " POLLIN event" << std::endl;
        connection = listen_sock_.Accept();
        std::cout << __FILE__ << " " << __LINE__ << " new connection " << std::endl;
        connection_list_.emplace(std::make_shared<ServerConnection>(connection));
      } else {
        std::cout << __FILE__ << " " << __LINE__ << " timout" << std::endl;
      }
    } catch (std::exception err) {
      break;
    }

  }

  //   RemoveStaleConnections();

  //   std::string peer_host;
  //   int peer_port;
  //   connection.GetPeerAddress(peer_host, peer_port);
  //   std::lock_guard<std::mutex> guard(mutex_);
  //   connection_list_.insert(
  //       std::make_shared<ConnectionInfo>(this, peer_host, peer_port, connection));
  // }
}

void ServerObj::Stop() {}

void ServerObj::Terminate() {}

int ServerObj::GetPort() const { return my_port_; }

void ServerObj::RegisterWithTracker(){
  std::cout << __FILE__ << " " << __LINE__ << " RegisterWithTracker unimplemented " << std::endl;
  exit(-1);
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

ServerConnection::ServerConnection(support::TCPSocket conn) : RPCBase{conn} {
  connection_task_ = std::thread(&ServerConnection::ConnectionLoopEntry, this);
  connection_task_.detach();
}

ServerConnection::~ServerConnection() {}

void ServerConnection::ConnectionLoopEntry() {
  // Do magic handshake
  int32_t magic = 0;
  if (RecvAll(&magic, sizeof(magic)) == -1) {
    // Error setting up connection
    return;
  }
  std::cout << __FILE__ << " " << __LINE__ << " connection magic " << magic << std::endl;
  if (magic != static_cast<int>(TrackerObj::RPC_CODE::RPC_MAGIC)) {
    // Not a tracker connection so close connection and exit
    return;
  }
  if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
    // Failed to send magic so exit
    return;
  }

  while (true) {
    std::string json;
    try {
      json = ReceivePacket();
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

void ServerConnection::ConnectionLoop() {}

void ServerConnection::ProcessMessage(std::string json) {
  std::cout << __FILE__ << " " << __LINE__ << " msg " << json << std::endl;
  static std::regex client_reg("client:(.*)");
  std::smatch sm;
  if (std::regex_match(json, sm, client_reg)) {
    std::cout << __FILE__ << " " << __LINE__ << " client " << sm[1] << std::endl;
    // SendResponse("server:");
  }
}

}  // namespace rpc
}  // namespace tvm
