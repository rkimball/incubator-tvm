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

#include "rpc_tracker.h"

#include <dmlc/json.h>
#include <tvm/runtime/registry.h>
#include <tvm/support/logging.h>

#include <iomanip>
#include <iostream>
#include <memory>

namespace tvm {
namespace runtime {
namespace rpc {

std::shared_ptr<RPCTracker> RPCTracker::rpc_tracker_ = nullptr;

int RPCTrackerStart(std::string host, int port, int port_end, bool silent) {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTrackerStart" << std::endl;
  return RPCTracker::Start(host, port, port_end, silent);
}

void RPCTrackerStop() {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTrackerStop" << std::endl;
  RPCTracker* tracker = RPCTracker::GetTracker();
  tracker->Stop();
}

void RPCTrackerTerminate() {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTrackerTerminate" << std::endl;
  RPCTracker* tracker = RPCTracker::GetTracker();
  if (tracker) {
    tracker->Terminate();
  }
}

RPCTracker::RPCTracker(std::string host, int port, int port_end, bool silent)
    : host_{host}, port_{port}, port_end_{port_end}, silent_{silent} {
  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
  listen_sock_.Listen();
  listener_task_ = std::make_unique<std::thread>(&RPCTracker::ListenLoopEntry, this);
  // listener_task_->detach();
}

RPCTracker::~RPCTracker() {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  // First shutdown the listen socket so we don't get any new connections
  listen_sock_.Shutdown();
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  listen_sock_.Close();
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  if (listener_task_->joinable()) {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    listener_task_->join();
  }
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  active_ = false;
  listener_task_ = nullptr;
  std::cout << __FILE__ << " " << __LINE__ << std::endl;

  // Second clear out any open connections since those have no tracker
  std::lock_guard<std::mutex> guard(mutex_);
  // for (auto conn : connection_list_) {
  //   std::cout << __FILE__ << " " << __LINE__ << std::endl;
  //   conn->Close();
  //   std::cout << __FILE__ << " " << __LINE__ << " conn ref count=" << conn.use_count() << std::endl;
  // }
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  connection_list_.clear();
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  scheduler_map_.clear();
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
}

/*!
 * \brief ListenLoopProc The listen process.
 */
void RPCTracker::ListenLoopEntry() {
  active_ = true;
  while (active_) {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    support::TCPSocket connection;
    try {
      connection = listen_sock_.Accept();
    } catch(...) {
      std::cout << __FILE__ << " " << __LINE__ << std::endl;
      break;
    }
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    std::string peer_host;
    int peer_port;
    connection.GetPeerAddress(peer_host, peer_port);
    std::lock_guard<std::mutex> guard(mutex_);
    connection_list_.insert(
        std::make_shared<ConnectionInfo>(rpc_tracker_, peer_host, peer_port, connection));
  }
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
}

RPCTracker* RPCTracker::GetTracker() { return rpc_tracker_.get(); }

int RPCTracker::GetPort() const { return my_port_; }

int RPCTracker::Start(std::string host, int port, int port_end, bool silent) {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTracker::Start" << std::endl;
  RPCTracker* tracker = RPCTracker::GetTracker();
  int result = -1;
  if (!tracker) {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    rpc_tracker_ = std::make_shared<RPCTracker>(host, port, port_end, silent);
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
  }
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  result = rpc_tracker_->GetPort();
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  return result;
}

void RPCTracker::Stop() {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTracker::Stop" << std::endl;
  // For now call Terminate
  Terminate();
}

void RPCTracker::Terminate() {
  std::cout << __FILE__ << " " << __LINE__ << " RPCTracker::Terminate" << std::endl;
  // Delete the RPCTracker object to terminate
  rpc_tracker_ = nullptr;
}

void RPCTracker::Put(std::string key, std::string address, int port, std::string match_key,
             std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, std::make_shared<PriorityScheduler>(key)});
  }
  auto it = scheduler_map_.find(key);
  if (it != scheduler_map_.end()) {
    it->second->Put(address, port, match_key, conn);
  } else {
    std::cout << __FILE__ << " " << __LINE__ << " put error" << key << std::endl;
    for (auto p : scheduler_map_) {
      std::cout << __FILE__ << " " << __LINE__ << " " << p.first << std::endl;
    }
  }
}

void RPCTracker::Request(std::string key, std::string user, int priority,
                         std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, std::make_shared<PriorityScheduler>(key)});
  }
  auto it = scheduler_map_.find(key);
  if (it != scheduler_map_.end()) {
    it->second->Request(user, priority, conn);
  } else {
    std::cout << __FILE__ << " " << __LINE__ << " request error" << key << std::endl;
    for (auto p : scheduler_map_) {
      std::cout << __FILE__ << " " << __LINE__ << " " << p.first << std::endl;
    }
  }
}

std::string RPCTracker::Summary() {
  std::stringstream ss;
  int count = scheduler_map_.size();
  for (auto p : scheduler_map_) {
    ss << "\"" << p.first << "\": " << p.second->Summary();
    if (--count > 0) {
      ss << ", ";
    }
  }
  return ss.str();
}

void RPCTracker::Close(std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  connection_list_.erase(conn);
  std::string key = conn->key_;
  if (!key.empty()) {
    // "server:rasp3b" -> "rasp3b"
    auto pos = key.find(':');
    if (pos != std::string::npos) {
      key = key.substr(pos+1);
    }
    // TODO: rkimball remove values from scheduler_map
  }
}

int RPCTracker::ConnectionInfo::SendResponse(TRACKER_CODE value) {
  std::stringstream ss;
  ss << static_cast<int>(value);
  std::string status = ss.str();
  return SendStatus(status);
}

int RPCTracker::ConnectionInfo::SendStatus(std::string status) {
  int length = status.size();
  bool fail = false;

  if (SendAll(&length, sizeof(length)) != sizeof(length)) {
    fail = true;
  }
  // std::cout << host_ << ":" << port_ << " << " << status << std::endl;
  if (!fail && SendAll(status.data(), status.size()) != length) {
    fail = true;
  }
  return fail ? -1 : length;
}

RPCTracker::PriorityScheduler::PriorityScheduler(std::string key) : key_{key} {}

void RPCTracker::PriorityScheduler::Request(std::string user, int priority,
                                            std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  requests_.emplace_back(user, priority, request_count_++, conn);
  std::sort(requests_.begin(), requests_.end(),
            [](const RPCTracker::RequestInfo& a, const RPCTracker::RequestInfo& b) {
              return a.priority_ > b.priority_;
            });
  Schedule();
}

void RPCTracker::PriorityScheduler::Put(std::string address, int port, std::string match_key,
             std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  values_.emplace_back(address, port, match_key, conn);
  Schedule();
}

void RPCTracker::PriorityScheduler::Remove(PutInfo value) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = std::find(values_.begin(), values_.end(), value);
  if (it != values_.end()) {
    values_.erase(it);
    Schedule();
  }
}

std::string RPCTracker::PriorityScheduler::Summary() {
  std::stringstream ss;
  ss << "{\"free\": " << values_.size() << ", \"pending\": " << requests_.size() << "}";
  return ss.str();
}

void RPCTracker::PriorityScheduler::Schedule() {
  while (!requests_.empty() && !values_.empty()) {
    PutInfo& pi = values_[0];
    RequestInfo& request = requests_[0];
    try {
      std::stringstream ss;
      ss << "[" << static_cast<int>(TRACKER_CODE::SUCCESS) << ", [\"" << pi.address_ << "\", "
         << pi.port_ << ", \"" << pi.match_key_ << "\"]]";
      request.conn_->SendStatus(ss.str());
      pi.conn_->pending_match_keys_.erase(pi.match_key_);
    } catch (...) {
      values_.push_back(pi);
    }

    values_.pop_front();
    requests_.pop_front();
  }
}

RPCTracker::ConnectionInfo::ConnectionInfo(std::weak_ptr<RPCTracker> tracker, std::string host, int port,
                                           support::TCPSocket connection)
    : tracker_{tracker}, host_{host}, port_{port}, connection_{connection} {
  std::cout << __FILE__ << " " << __LINE__ << " " << static_cast<void*>(this) << std::endl;
  connection_task_ =
      std::thread(&RPCTracker::ConnectionInfo::ConnectionLoop, this);
  connection_task_.detach();
}

RPCTracker::ConnectionInfo::~ConnectionInfo(){
  std::cout << __FILE__ << " " << __LINE__ << " " << static_cast<void*>(this) << std::endl;
}

void RPCTracker::ConnectionInfo::Close() {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  if (!connection_.IsClosed()) {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    connection_.Shutdown();
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    connection_.Close();
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
  }
}

int RPCTracker::ConnectionInfo::RecvAll(void* data, size_t length) {
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

int RPCTracker::ConnectionInfo::SendAll(const void* data, size_t length) {
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

void RPCTracker::ConnectionInfo::Fail() {
  Close();
  if (auto tracker = tracker_.lock()) {
    std::lock_guard<std::mutex> guard(tracker->mutex_);
    tracker->connection_list_.erase(shared_from_this());
  }
}

void RPCTracker::ConnectionInfo::ConnectionLoop() {
  // Do magic handshake
  int magic = 0;
  if (RecvAll(&magic, sizeof(magic)) == -1) {
    // Error setting up connection
    std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
    Fail();
    return;
  }
  if (magic != static_cast<int>(RPC_CODE::RPC_TRACKER_MAGIC)) {
    // Not a tracker connection so close connection and exit
    std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
    Fail();
    return;
  }
  if (SendAll(&magic, sizeof(magic)) != sizeof(magic)) {
    // Failed to send magic so exit
    std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
    Fail();
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
      if(!fail && RecvAll(&json[0], length) != length) {
        fail = true;
      }
    } catch (std::exception err) {
      fail = true;
      // This means that the connection has gone down. Tell the tracker to remove it.
    }

    if (fail) {
      Fail();
      return;
    }

    // std::cout << host_ << ":" << port_ << " >> " << json << std::endl;

    std::istringstream is(json);
    dmlc::JSONReader reader(&is);
    int tmp;
    reader.BeginArray();
    reader.NextArrayItem();
    reader.ReadNumber(&tmp);
    reader.NextArrayItem();
    switch (static_cast<TRACKER_CODE>(tmp)) {
      case TRACKER_CODE::FAIL:
        break;
      case TRACKER_CODE::SUCCESS:
        break;
      case TRACKER_CODE::PING:
        if (SendResponse(TRACKER_CODE::SUCCESS) == -1){
          // Failed to send response so connection broken
          std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
          Fail();
          return;
        }
        break;
      case TRACKER_CODE::STOP:
        if (SendResponse(TRACKER_CODE::SUCCESS) == -1){
          // Failed to send response so connection broken
          std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
          Fail();
          return;
        }

        if (auto tracker = tracker_.lock()) {
          tracker->Stop();
        }
        break;
      case TRACKER_CODE::PUT: {
        std::string key;
        int port;
        std::string match_key;
        std::string addr = host_;
        reader.Read(&key);
        reader.NextArrayItem();
        reader.BeginArray();
        reader.NextArrayItem();
        reader.Read(&port);
        reader.NextArrayItem();
        reader.Read(&match_key);
        reader.NextArrayItem();  // This is an EndArray
        if (reader.NextArrayItem()) {
          // 4 args in message
          std::string tmp;
          try {
            reader.Read(&tmp);
          } catch (...) {
            // Not a string so we don't care
          }
          if (!tmp.empty() && tmp != "null") {
            addr = tmp;
          }
        }
        pending_match_keys_.insert(match_key);
        // auto put_info = std::make_shared<PutInfo>(addr, port, match_key, shared_from_this());
        if (auto tracker = tracker_.lock()) {
          tracker->Put(key, addr, port, match_key, shared_from_this());
        }
        // put_values_.insert(put_info);
        if (SendResponse(TRACKER_CODE::SUCCESS) == -1){
          // Failed to send response so connection broken
          std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
          Fail();
          return;
        }
        break;
      }
      case TRACKER_CODE::REQUEST: {
        std::string key;
        std::string user;
        int priority;
        reader.Read(&key);
        reader.NextArrayItem();
        reader.Read(&user);
        reader.NextArrayItem();
        reader.Read(&priority);
        reader.NextArrayItem();
        if (auto tracker = tracker_.lock()) {
          tracker->Request(key, user, priority, shared_from_this());
        }
        break;
      }
      case TRACKER_CODE::UPDATE_INFO: {
        std::string key;
        std::string value;
        reader.BeginObject();
        reader.NextObjectItem(&key);
        reader.Read(&value);
        key_ = value;
        if (SendResponse(TRACKER_CODE::SUCCESS) == -1){
          // Failed to send response so connection broken
          std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
          Fail();
          return;
        }
        break;
      }
      case TRACKER_CODE::SUMMARY: {
        if (auto tracker = tracker_.lock()) {
          std::stringstream ss;
          ss << "[" << static_cast<int>(TRACKER_CODE::SUCCESS) << ", {\"queue_info\": {"
            << tracker->Summary() << "}, ";
          ss << "\"server_info\": [";
          int count = 0;
          {
            std::lock_guard<std::mutex> guard(tracker->mutex_);
            for (auto conn : tracker->connection_list_) {
              if (conn->key_.substr(0, 6) == "server") {
                if (count++ > 0) {
                  ss << ", ";
                }
                ss << "{\"addr\": [\"" << conn->host_ << "\", " << conn->port_ << "], \"key\": \""
                  << conn->key_ << "\"}";
              }
            }
          }
          ss << "]}]";
          if (SendStatus(ss.str()) == -1) {
            // Failed to send response so connection broken
            std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
            Fail();
            return;
          }
        }
        break;
      }
      case TRACKER_CODE::GET_PENDING_MATCHKEYS:
        std::stringstream ss;
        ss << "[";
        int count = 0;
        for (auto match_key : pending_match_keys_) {
          if (count++ > 0) {
            ss << ", ";
          }
          ss << "\"" << match_key << "\"";
        }
        ss << "]";
        if (SendStatus(ss.str()) == -1) {
          // Failed to send response so connection broken
          std::cout << __FILE__ << " " << __LINE__ << " error sending response\n";
          Fail();
          return;
        }
        break;
    }
  }
}

}  // namespace rpc
TVM_REGISTER_GLOBAL("rpc.RPCTrackerStart").set_body_typed(tvm::runtime::rpc::RPCTrackerStart);
TVM_REGISTER_GLOBAL("rpc.RPCTrackerStop").set_body_typed(tvm::runtime::rpc::RPCTrackerStop);
TVM_REGISTER_GLOBAL("rpc.RPCTrackerTerminate").set_body_typed(tvm::runtime::rpc::RPCTrackerTerminate);
}  // namespace runtime
}  // namespace tvm
