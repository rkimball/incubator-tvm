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

class MockServer {
public:
  MockServer(int port, std::string key) : key_{key} {
    tvm::support::SockAddr addr("localhost", port);
    std::cout << __FILE__ << " " << __LINE__ << " " << addr.AsString() << std::endl;
    if (socket_.Connect(addr)) {
        std::cout << __FILE__ << " " << __LINE__ << " successfully start server " << key_ << std::endl;
    } else {
        std::cout << __FILE__ << " " << __LINE__ << " failed to start server " << key_ << std::endl;
    }
  }

private:
    tvm::support::TCPSocket socket_;
    std::string key_;
};

TEST(Tracker, Basic) {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  tvm::runtime::rpc::RPCTracker tracker("localhost", 9000, 10000);
  int tracker_port = tracker.GetPort();
  std::cout << "Tracker port " << tracker_port << std::endl;

  // Setup mock server
  MockServer s1(tracker_port, "abc");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
