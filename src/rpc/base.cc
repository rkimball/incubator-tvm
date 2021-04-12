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

#include "base.h"

#include <vector>

namespace tvm {
namespace rpc {

int RPCBase::RecvAll(void* data, size_t length) {
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

int RPCBase::SendAll(const void* data, size_t length) {
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

std::string RPCBase::ReceiveJSON() {
  std::string data;
  int32_t length = 0;
  if (RecvAll(&length, sizeof(length)) != sizeof(length)) {
    throw std::runtime_error("Error receiving json");
  }
  data.resize(length);
  if (RecvAll(&data[0], length) != length) {
    throw std::runtime_error("Error receiving json");
  }
  return data;
}

void RPCBase::SendJSON(std::string data) {
  int32_t length = data.size();
  SendAll(&length, sizeof(length));
  SendAll(data.data(), length);
}

std::stringstream RPCBase::ReceivePacket() {
  std::stringstream ss(std::ios_base::in | std::ios_base::out | std::ios_base::binary);
  int64_t length = 0;
  if (RecvAll(&length, sizeof(length)) != sizeof(length)) {
    throw std::runtime_error("Error receiving packet");
  }
  std::cout << __FILE__ << " " << __LINE__ << " length=" << length << std::endl;
  // Read length's worth of data
  std::vector<char> buffer;
  buffer.resize(length);
  if (RecvAll(buffer.data(), length) != length) {
    throw std::runtime_error("Error receiving packet");
  }
  ss.write(buffer.data(), length);
  ss.seekg(0);
  return ss;
}

bool RPCBase::MagicHandshake(RPC_CODE code) {
  int32_t magic = 0;
  if (RecvAll(&magic, sizeof(magic)) == -1) {
    // Error setting up connection
    return false;
  }
  if (magic != static_cast<int>(code)) {
    return false;
  }
  if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
    // Failed to send magic
    return false;
  }
  return true;
}

support::TCPSocket AcceptWithTimeout(support::TCPSocket listen_sock, int timeout_ms,
                                     std::function<void()> timeout_callback) {
  int numfds = 1;
  pollfd poll_set[1];
  memset(poll_set, '\0', sizeof(poll_set));
  poll_set[0].fd = listen_sock.sockfd;
  poll_set[0].events = POLLIN;
  while (true) {
    poll(poll_set, numfds, timeout_ms);
    if (poll_set[0].revents & POLLIN) {
      break;
    } else if (timeout_callback) {
      timeout_callback();
    }
  }
  return listen_sock.Accept();
}

}  // namespace rpc
}  // namespace tvm
