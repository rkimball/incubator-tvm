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

// int magic = 0;
// if (RecvAll(&magic, sizeof(magic)) == -1) {
//   // Error setting up connection
//   return;
// }
// if (magic != static_cast<int>(RPCTrackerObj::RPC_CODE::RPC_TRACKER_MAGIC)) {
//   // Not a tracker connection so close connection and exit
//   return;
// }
// if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
//   // Failed to send magic so exit
//   return;
// }

class RPCUtil {
 public:
  RPCUtil(int port) { ConnectToTracker(port); }
  ~RPCUtil() { DisconnectFromTracker(); }
  bool ConnectToTracker(int port) {
    tvm::support::SockAddr addr("localhost", port);
    socket_.Create();
    if (socket_.Connect(addr)) {
    } else {
      std::cout << __FILE__ << " " << __LINE__ << " failed to start server " << std::endl;
    }
    int magic = static_cast<int>(tvm::runtime::rpc::RPCTrackerObj::RPC_CODE::RPC_TRACKER_MAGIC);
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
    if (!socket_.IsClosed()) {
      socket_.Shutdown();
      socket_.Close();
    }
  }

  int RecvAll(void* data, size_t length) {
    char* buf = static_cast<char*>(data);
    size_t remainder = length;
    while (remainder > 0) {
      int read_length = socket_.Recv(buf, remainder);
      if (read_length <= 0) {
        return -1;
      }
      remainder -= read_length;
      buf += read_length;
    }
    return length;
  }

  int SendAll(const void* data, size_t length) {
    if (socket_.IsClosed()) {
      return -1;
    }
    const char* buf = static_cast<const char*>(data);
    size_t remainder = length;
    while (remainder > 0) {
      int send_length = socket_.Send(buf, remainder);
      if (send_length <= 0) {
        return -1;
      }
      remainder -= send_length;
      buf += send_length;
    }
    return length;
  }

 protected:
  tvm::support::TCPSocket socket_;
};

class MockServer : public RPCUtil {
 public:
  MockServer(int port, std::string key) : RPCUtil(port), key_{key} {
  }

 private:
  std::string key_;
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
  MockServer s1(tracker_port, "abc");
  MockServer s2(tracker_port, "abc");
  MockServer s3(tracker_port, "abc");

  std::cout << __FILE__ << " " << __LINE__ << " sleep 3 seconds" << std::endl;
  sleep(3);
  std::cout << __FILE__ << " " << __LINE__ << " done with sleep" << std::endl;
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
