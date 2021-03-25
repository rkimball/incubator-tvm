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

#include <chrono>
#include <future>
#include <regex>

#include "../../src/runtime/rpc/rpc_tracker.h"
#include "../../src/support/socket.h"

using TRACKER_CODE = tvm::runtime::rpc::RPCTrackerObj::TRACKER_CODE;
using RPC_CODE = tvm::runtime::rpc::RPCTrackerObj::RPC_CODE;

class Summary {
 public:
  class Queue {
   public:
    std::string key;
    int free_count = 0;
    int pending_count = 0;

    friend std::ostream& operator<<(std::ostream& out, const Queue& obj) {
      out << "queue(" << obj.key << ", free=" << obj.free_count << ", pending=" << obj.pending_count
          << ")";
      return out;
    }
  };

  class Server {
   public:
    std::string key;
    std::string host;
    int port;

    friend std::ostream& operator<<(std::ostream& out, const Server& obj) {
      out << "server(" << obj.key << ", " << obj.host << ":" << obj.port << ")";
      return out;
    }
  };

  Summary(std::string json) {
    std::istringstream is(json);
    dmlc::JSONReader reader(&is);
    int tmp;
    reader.BeginArray();
    reader.NextArrayItem();
    reader.ReadNumber(&tmp);
    reader.NextArrayItem();
    reader.BeginObject();
    std::string key;
    while (reader.NextObjectItem(&key)) {
      // reader.ReadString(&key);
      if (key == "queue_info") {
        ParseQueueInfo(reader);
      } else if (key == "server_info") {
        ParseServerInfo(reader);
      }
    }
  }

  Queue GetQueue(std::string key) {
    Queue result;
    for (auto queue : queues) {
      if (queue.key == key) {
        result = queue;
        break;
      }
    }
    return result;
  }

  bool ContainsServer(int port) {
    for (auto s : servers) {
      if (s.port == port) {
        return true;
      }
    }
    return false;
  }

  void ParseQueueInfo(dmlc::JSONReader& reader) {
    reader.BeginObject();
    std::string key;
    while (reader.NextObjectItem(&key)) {
      Queue queue;
      queue.key = key;
      reader.BeginObject();
      std::string key1;
      while (reader.NextObjectItem(&key1)) {
        int value = 0;
        reader.ReadNumber(&value);
        if (key1 == "free") {
          queue.free_count = value;
        } else if (key1 == "pending") {
          queue.pending_count = value;
        }
      }
      queues.push_back(queue);
    }
  }

  void ParseServerInfo(dmlc::JSONReader& reader) {
    reader.BeginArray();
    while (reader.NextArrayItem()) {
      reader.BeginObject();
      std::string key;
      Server server;
      while (reader.NextObjectItem(&key)) {
        int port;
        if (key == "addr") {
          std::string addr;
          reader.BeginArray();
          reader.NextArrayItem();
          reader.Read(&addr);
          reader.NextArrayItem();
          reader.Read(&port);
          reader.NextArrayItem();
          server.host = addr;
          server.port = port;
        } else if (key == "key") {
          std::string server_key;
          reader.Read(&server_key);
          server.key = server_key;
        }
      }
      servers.push_back(server);
    }
  }

  friend std::ostream& operator<<(std::ostream& out, const Summary& obj) {
    out << "Queue:\n";
    for (auto queue : obj.queues) {
      out << "   " << queue << "\n";
    }
    out << "Server:\n";
    for (auto server : obj.servers) {
      out << "   " << server << "\n";
    }
    return out;
  }

  std::vector<Queue> queues;
  std::vector<Server> servers;
};

class RPCUtil {
 public:
  RPCUtil(int tracker_port) { ConnectToTracker(tracker_port); }
  ~RPCUtil() { DisconnectFromTracker(); }
  bool ConnectToTracker(int tracker_port) {
    tvm::support::SockAddr addr("localhost", tracker_port);
    tracker_socket_.Create();
    tracker_socket_.Connect(addr);
    tracker_socket_.GetPeerAddress(remote_host_, remote_port_);
    tracker_socket_.GetLocalAddress(local_host_, local_port_);
    int magic = static_cast<int>(RPC_CODE::RPC_TRACKER_MAGIC);
    if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
      // Failed to send magic so exit
      return false;
    }
    if (RecvAll(&magic, sizeof(magic)) == -1) {
      // Error setting up connection
      return false;
    }

    return true;
  }

  void DisconnectFromTracker() {
    if (!tracker_socket_.IsClosed()) {
      tracker_socket_.Shutdown();
      tracker_socket_.Close();
    }
  }

  std::string RecvPacket() {
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

  int SendPacket(std::string msg) {
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

  Summary GetSummary() {
    std::ostringstream ss;
    ss << "[" << static_cast<int>(TRACKER_CODE::SUMMARY) << "]";
    SendPacket(ss.str());
    std::string json = RecvPacket();
    Summary summary(json);
    return summary;
  }

  int GetRemotePort() { return remote_port_; }
  int GetLocalPort() { return local_port_; }

 protected:
  tvm::support::TCPSocket tracker_socket_;
  std::string key_;
  int remote_port_;
  std::string remote_host_;
  int local_port_;
  std::string local_host_;
};

class MockServer : public RPCUtil {
 public:
  MockServer(int tracker_port, std::string key) : RPCUtil(tracker_port), key_{key} {
    listen_socket_.Create();
    my_port_ = listen_socket_.TryBindHost("localhost", 30000, 40000);

    std::ostringstream ss;
    ss << "[" << static_cast<int>(TRACKER_CODE::UPDATE_INFO) << ", {\"key\": \"server:" << key_
       << "\"}]";
    SendPacket(ss.str());

    // Receive status and validate
    std::string status = RecvPacket();

    /*!
     * \brief Register this device with the tracker.
     */
    PutDevice();
  }

  ~MockServer() {
    if (!listen_socket_.IsClosed()) {
      listen_socket_.Shutdown();
      listen_socket_.Close();
    }
  }

  void PutDevice() {
    match_key_ = key_ + ":" + std::to_string(rand());
    std::ostringstream ss;
    ss << "[" << static_cast<int>(TRACKER_CODE::PUT) << ", \"" << key_ << "\", [" << my_port_
       << ", \"" << match_key_ << "\"], " << custom_addr_ << "]";
    SendPacket(ss.str());
    std::string status = RecvPacket();
  }

  int GetListenerPort() { return my_port_; }

 private:
  std::string key_;
  std::string match_key_;
  std::string custom_addr_ = "\"127.0.0.1\"";
  int my_port_;
  tvm::support::TCPSocket listen_socket_;
};

class RequestResponse {
 public:
  std::string host;
  int port;
  std::string match_key;
  int status_;
};

class MockClient : public RPCUtil {
 public:
  MockClient(int port) : RPCUtil(port) {}

  RequestResponse Request(std::string key, int priority) {
    SendRequest(key, priority);
    return ReadRequestResponse();
  }

  void SendRequest(std::string key, int priority) {
    std::ostringstream ss;
    ss << "[" << static_cast<int>(TRACKER_CODE::REQUEST) << ", \"" << key << "\", \"\", "
       << priority << "]";
    SendPacket(ss.str());
  }

  RequestResponse ReadRequestResponse() {
    RequestResponse response;
    std::string status = RecvPacket();
    std::regex reg("\\[(\\d),.*\\[\"([^\"]+)\", (\\d+), \"([^\"]+)\"\\]\\]");
    std::smatch sm;
    if (std::regex_match(status, sm, reg)) {
      response.status_ = std::stoi(sm[1]);
      if (response.status_ == 0) {
        response.host = sm[2];
        response.port = std::stoi(sm[3]);
        response.match_key = sm[4];
      }
    }
    return response;
  }
};

template <typename R>
bool is_ready(R const& f) {
  return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

TEST(Tracker, Priority) {
  std::chrono::milliseconds wait_time(100);
  auto tracker =
      tvm::runtime::make_object<tvm::runtime::rpc::RPCTrackerObj>("localhost", 9000, 10000);
  int tracker_port = tracker->GetPort();

  // Setup mock server
  MockServer dev1(tracker_port, "abc-1");
  MockServer dev2(tracker_port, "abc-1");
  MockServer dev3(tracker_port, "abc-1");
  MockServer dev4(tracker_port, "abc-2");
  MockServer dev5(tracker_port, "abc-2");
  MockServer dev6(tracker_port, "abc-2");

  MockClient client1(tracker_port);
  MockClient client2(tracker_port);
  MockClient client3(tracker_port);

  std::future<RequestResponse> f1 = std::async(&MockClient::Request, &client1, "abc-1", 0);
  ASSERT_TRUE(f1.valid());
  EXPECT_EQ(f1.wait_for(wait_time), std::future_status::ready);
  EXPECT_TRUE(is_ready(f1));

  std::future<RequestResponse> f2 = std::async(&MockClient::Request, &client2, "abc-1", 0);
  ASSERT_TRUE(f2.valid());
  EXPECT_EQ(f2.wait_for(wait_time), std::future_status::ready);
  EXPECT_TRUE(is_ready(f2));

  std::future<RequestResponse> f3 = std::async(&MockClient::Request, &client3, "abc-1", 0);
  ASSERT_TRUE(f3.valid());
  EXPECT_EQ(f3.wait_for(wait_time), std::future_status::ready);
  EXPECT_TRUE(is_ready(f3));

  // At this point there are no devices ready
  // Request 3 more device using priority ordering
  std::future<RequestResponse> f4 = std::async(&MockClient::Request, &client1, "abc-1", 10);
  ASSERT_TRUE(f4.valid());
  EXPECT_NE(f4.wait_for(wait_time), std::future_status::ready);

  std::future<RequestResponse> f5 = std::async(&MockClient::Request, &client2, "abc-1", 100);
  ASSERT_TRUE(f5.valid());
  EXPECT_NE(f5.wait_for(wait_time), std::future_status::ready);

  std::future<RequestResponse> f6 = std::async(&MockClient::Request, &client3, "abc-1", 30);
  ASSERT_TRUE(f6.valid());
  EXPECT_NE(f6.wait_for(wait_time), std::future_status::ready);

  // The requests must be satisfied in the order f5, f6, f4 even though the order requested
  // was f4, f5, f6
  dev1.PutDevice();
  EXPECT_NE(f4.wait_for(wait_time), std::future_status::ready);
  EXPECT_EQ(f5.wait_for(wait_time), std::future_status::ready);
  EXPECT_NE(f6.wait_for(wait_time), std::future_status::ready);

  dev1.PutDevice();
  EXPECT_NE(f4.wait_for(wait_time), std::future_status::ready);
  EXPECT_EQ(f6.wait_for(wait_time), std::future_status::ready);

  dev1.PutDevice();
  EXPECT_EQ(f4.wait_for(wait_time), std::future_status::ready);
}

// This test checks that when a server closes then it is removed from the list of servers
TEST(Tracker, DeviceClose) {
  auto tracker =
      tvm::runtime::make_object<tvm::runtime::rpc::RPCTrackerObj>("localhost", 9000, 10000);
  int tracker_port = tracker->GetPort();

  // Setup mock server
  auto dev1 = std::make_shared<MockServer>(tracker_port, "abc-1");
  auto dev2 = std::make_shared<MockServer>(tracker_port, "abc-1");
  auto dev3 = std::make_shared<MockServer>(tracker_port, "abc-1");
  auto dev4 = std::make_shared<MockServer>(tracker_port, "abc-2");
  auto dev5 = std::make_shared<MockServer>(tracker_port, "abc-2");
  auto dev6 = std::make_shared<MockServer>(tracker_port, "abc-2");

  auto summary = dev1->GetSummary();
  int dev6_port = dev6->GetLocalPort();
  EXPECT_TRUE(summary.ContainsServer(dev6_port));
  Summary::Queue queue = summary.GetQueue("abc-2");
  EXPECT_EQ(queue.free_count, 3);
  EXPECT_EQ(queue.pending_count, 0);

  dev6 = nullptr;

  // Since the tracker is multithreaded we need to add a sleep in here just to make sure the
  // call to GetSummary does not happen before dev6 is fully removed
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  summary = dev1->GetSummary();
  EXPECT_FALSE(summary.ContainsServer(dev6_port));
  queue = summary.GetQueue("abc-2");
  EXPECT_EQ(queue.free_count, 2);
  EXPECT_EQ(queue.pending_count, 0);
}

// This test checks that a pending request is removed when a client closes
TEST(Tracker, PendingRequest) {
  auto tracker =
      tvm::runtime::make_object<tvm::runtime::rpc::RPCTrackerObj>("localhost", 9000, 10000);
  int tracker_port = tracker->GetPort();

  auto dev1 = std::make_shared<MockServer>(tracker_port, "xyz");

  auto client1 = std::make_shared<MockClient>(tracker_port);
  client1->SendRequest("abc", 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto summary = dev1->GetSummary();

  {
    Summary::Queue queue = summary.GetQueue("abc");
    EXPECT_EQ(queue.free_count, 0);
    EXPECT_EQ(queue.pending_count, 1);
  }

  // Close the client connection, we are not going to get a response. The server must
  // remove the pending request.
  client1 = nullptr;

  // Since the tracker is multithreaded we need to add a sleep in here just to make sure the
  // call to GetSummary does not happen before things have settled
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  summary = dev1->GetSummary();
  {
    Summary::Queue queue = summary.GetQueue("abc");
    EXPECT_EQ(queue.free_count, 0);
    EXPECT_EQ(queue.pending_count, 0);
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
