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

#include <gtest/gtest.h>
#include <tvm/te/operation.h>
#include <tvm/topi/elemwise.h>

#include "../../src/runtime/rpc/rpc_tracker.h"
#include "../../src/support/socket.h"

using TRACKER_CODE = tvm::runtime::rpc::RPCTrackerObj::TRACKER_CODE;
using RPC_CODE = tvm::runtime::rpc::RPCTrackerObj::RPC_CODE;

class RPCUtil {
 public:
  RPCUtil(int tracker_port) { ConnectToTracker(tracker_port); }
  ~RPCUtil() { DisconnectFromTracker(); }
  bool ConnectToTracker(int tracker_port) {
    tvm::support::SockAddr addr("localhost", tracker_port);
    tracker_socket_.Create();
    if (tracker_socket_.Connect(addr)) {
    } else {
      std::cout << __FILE__ << " " << __LINE__ << " failed to start server " << std::endl;
    }
    int magic = static_cast<int>(RPC_CODE::RPC_TRACKER_MAGIC);
    if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
      // Failed to send magic so exit
      std::cout << __FILE__ << " " << __LINE__ << std::endl;
    }
    if (RecvAll(&magic, sizeof(magic)) == -1) {
      // Error setting up connection
      std::cout << __FILE__ << " " << __LINE__ << std::endl;
    }

    return true;
  }

  void DisconnectFromTracker() {
    if (!tracker_socket_.IsClosed()) {
      tracker_socket_.Shutdown();
      tracker_socket_.Close();
    }
  }

  std::string Summary() {
    std::stringstream ss;
    ss << "[" << static_cast<int>(TRACKER_CODE::SUMMARY) << "]";
    SendAll(ss.str());
    std::string json = RecvAll();
    std::cout << __FILE__ << " " << __LINE__ << " " << json << std::endl;
    return "fix this";
  }

  std::string RecvAll() {
    int32_t size = 0;
    std::string json;
    RecvAll(&size, sizeof(size));
    json.resize(size);
    RecvAll(&json[0], json.size());
    return json;
  }

  int RecvAll(void* data, size_t length) {
    char* buf = static_cast<char*>(data);
    size_t remainder = length;
    while (remainder > 0) {
      int read_length = tracker_socket_.Recv(buf, remainder);
      if (read_length <= 0) {
        return -1;
      }
      remainder -= read_length;
      buf += read_length;
    }
    return length;
  }

  int SendAll(std::string msg) {
    int32_t size = msg.size();
    SendAll(&size, sizeof(size));
    SendAll(msg.data(), msg.size());
    return msg.size();
  }

  int SendAll(const void* data, size_t length) {
    if (tracker_socket_.IsClosed()) {
      return -1;
    }
    const char* buf = static_cast<const char*>(data);
    size_t remainder = length;
    while (remainder > 0) {
      int send_length = tracker_socket_.Send(buf, remainder);
      if (send_length <= 0) {
        return -1;
      }
      remainder -= send_length;
      buf += send_length;
    }
    return length;
  }

 protected:
  tvm::support::TCPSocket tracker_socket_;
  std::string key_;
};

class MockServer : public RPCUtil {
 public:
  MockServer(int tracker_port, std::string key) : RPCUtil(tracker_port), key_{key} {
    std::string status;

    listen_socket_.Create();
    my_port_ = listen_socket_.TryBindHost("localhost", 30000, 40000);
    std::cout << __FILE__ << " " << __LINE__ << " MockServer listen " << my_port_ << std::endl;

    {
      std::ostringstream ss;
      ss << "[" << static_cast<int>(TRACKER_CODE::UPDATE_INFO) << ", {\"key\": \"server:" << key_
          << "\"}]";
      SendAll(ss.str());
    }

    // Receive status and validate
    status = RecvAll();
    std::cout << __FILE__ << " " << __LINE__ << " " << status << std::endl;

    match_key_ = key_ + ":" + std::to_string(rand());
    std::cout << __FILE__ << " " << __LINE__ << " " << match_key_ << std::endl;
    {
      std::ostringstream ss;
      ss << "[" << static_cast<int>(TRACKER_CODE::PUT) << ", \"" << key_ << "\", [" << my_port_
          << ", \"" << match_key_ << "\"], " << custom_addr_ << "]";
      SendAll(ss.str());
    }
    status = RecvAll();
    std::cout << __FILE__ << " " << __LINE__ << " " << status << std::endl;
  }

  ~MockServer() {
    if (!listen_socket_.IsClosed()) {
      listen_socket_.Shutdown();
      listen_socket_.Close();
    }
  }

 private:
  std::string key_;
  std::string match_key_;
  std::string custom_addr_ = "\"127.0.0.1\"";
  int my_port_;
  tvm::support::TCPSocket listen_socket_;
};

class MockClient : public RPCUtil {
  MockClient(int port) : RPCUtil(port) {}
};

TEST(Tracker, Basic) {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  auto tracker =
      tvm::runtime::make_object<tvm::runtime::rpc::RPCTrackerObj>("localhost", 9000, 10000);
  int tracker_port = tracker->GetPort();
  std::cout << "Tracker port " << tracker_port << std::endl;

  // Setup mock server
  MockServer s1(tracker_port, "abc-1");
  MockServer s2(tracker_port, "abc-1");
  MockServer s3(tracker_port, "abc-1");
  MockServer s4(tracker_port, "abc-2");
  MockServer s5(tracker_port, "abc-2");
  MockServer s6(tracker_port, "abc-2");

  s1.Summary();
  std::cout << __FILE__ << " " << __LINE__ << " sleep 3 seconds" << std::endl;
  sleep(3);
  std::cout << __FILE__ << " " << __LINE__ << " done with sleep" << std::endl;
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
