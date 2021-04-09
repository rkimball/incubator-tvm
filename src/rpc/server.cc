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

#include "tracker.h"

namespace tvm {
namespace rpc {

class ServerConnection {
 public:
  ServerConnection(support::TCPSocket conn);
  ~ServerConnection();
  void ConnectionLoopEntry();
  void ConnectionLoop();
  void ProcessMessage(std::string json);

 private:
  int RecvAll(void* data, size_t length);
  int SendAll(const void* data, size_t length);

  std::thread connection_task_;
  std::string host_;
  int port_;
  support::TCPSocket connection_;
};

ServerObj::ServerObj(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                     std::string tracker_host, int tracker_port, std::string key,
                     std::string load_library, std::string custom_host, int custom_port,
                     bool silent)
    : host_{host}, key_{key} {
  if (host == "" || host == "0.0.0.0" || host == "localhost") {
    host_ = host;
  }

  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port, port_end);
  std::cout << __FILE__ << " " << __LINE__ << " Server bound to port " << my_port_ << std::endl;

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
      connection = listen_sock_.Accept();
    } catch (std::exception err) {
      break;
    }

    std::cout << __FILE__ << " " << __LINE__ << " new connection " << std::endl;
    connection_list_.emplace(std::make_shared<ServerConnection>(connection));
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

TVM_REGISTER_NODE_TYPE(ServerObj);
TVM_REGISTER_GLOBAL("rpc.Server")
    .set_body_typed([](std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                       std::string tracker_host, int tracker_port, std::string key,
                       std::string load_library, std::string custom_host, int custom_port,
                       bool silent) {
      return tvm::rpc::Server(host, port, port_end, is_proxy, use_popen, tracker_host, tracker_port,
                              key, load_library, custom_host, custom_port, silent);
    });

ServerConnection::ServerConnection(support::TCPSocket conn) : connection_{conn} {
  connection_task_ = std::thread(&ServerConnection::ConnectionLoopEntry, this);
  connection_task_.detach();
}

ServerConnection::~ServerConnection() {}

int ServerConnection::RecvAll(void* data, size_t length) {
  char* buf = static_cast<char*>(data);
  size_t remainder = length;
  while (remainder > 0) {
    int read_length = connection_.Recv(buf, remainder);
    if (read_length <= 0) {
      return -1;
    }
    remainder -= read_length;
    buf += read_length;
  }
  return length;
}

int ServerConnection::SendAll(const void* data, size_t length) {
  if (connection_.IsClosed()) {
    return -1;
  }
  const char* buf = static_cast<const char*>(data);
  size_t remainder = length;
  while (remainder > 0) {
    int send_length = connection_.Send(buf, remainder);
    if (send_length <= 0) {
      return -1;
    }
    remainder -= send_length;
    buf += send_length;
  }
  return length;
}

void ServerConnection::ConnectionLoopEntry() {
  // Do magic handshake
  int magic = 0;
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
    bool fail = false;
    try {
      int length = 0;
      if (RecvAll(&length, sizeof(length)) != sizeof(length)) {
        fail = true;
      }
      json.resize(length);
      if (!fail && RecvAll(&json[0], length) != length) {
        fail = true;
      }
    } catch (std::exception err) {
      fail = true;
      // This means that the connection has gone down. Tell the tracker to remove it.
    }

    if (fail) {
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
}

}  // namespace rpc
}  // namespace tvm
