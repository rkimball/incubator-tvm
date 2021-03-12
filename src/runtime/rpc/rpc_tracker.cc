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

std::unique_ptr<RPCTracker> RPCTracker::rpc_tracker_ = nullptr;

int RPCTrackerEntry(std::string host, int port, int port_end, bool silent) {
  int result = -1;
  RPCTracker* tracker = RPCTracker::GetTracker();
  if (!tracker) {
    // Tracker is not currently running so start it
    result = RPCTracker::Start(host, port, port_end, silent);
  }
  return result;
}

RPCTracker::RPCTracker(std::string host, int port, int port_end, bool silent)
    : host_{host}, port_{port}, port_end_{port_end}, silent_{silent} {
  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
  listen_sock_.Listen(1);
  listener_task_ = std::async(std::launch::async, &RPCTracker::ListenLoopEntry, this);
}

RPCTracker::~RPCTracker() { std::cout << __FILE__ << " " << __LINE__ << std::endl; }

RPCTracker* RPCTracker::GetTracker() { return rpc_tracker_.get(); }

int RPCTracker::GetPort() const { return my_port_; }

int RPCTracker::Start(std::string host, int port, int port_end, bool silent) {
  RPCTracker* tracker = RPCTracker::GetTracker();
  int result = -1;
  if (!tracker) {
    rpc_tracker_ = std::make_unique<RPCTracker>(host, port, port_end, silent);
    result = rpc_tracker_->GetPort();
  }

  std::cout << __FILE__ << " " << __LINE__ << " " << result << std::endl;
  return result;
}

/*!
 * \brief ListenLoopProc The listen process.
 */
void RPCTracker::ListenLoopEntry() {
  while (true) {
    support::TCPSocket connection = listen_sock_.Accept();
    std::string peer_host;
    int peer_port;
    connection.GetPeerAddress(peer_host, peer_port);
    connection_list_.push_back(
        std::make_shared<ConnectionInfo>(this, peer_host, peer_port, connection));
    std::cout << __FILE__ << " " << __LINE__ << " peer=" << peer_host << ":" << peer_port
              << std::endl;
  }
}

void RPCTracker::Put(std::string key, std::string address, int port, std::string match_key,
                     std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, std::make_shared<PriorityScheduler>(key)});
  }
  scheduler_map_.at(key)->Put(address, port, match_key, conn);
}

void RPCTracker::Request(std::string key, std::string user, int priority,
                         std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, std::make_shared<PriorityScheduler>(key)});
  }
  scheduler_map_.at(key)->Request(user, priority, conn);
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

void RPCTracker::ConnectionInfo::SendResponse(TRACKER_CODE value) {
  std::stringstream ss;
  ss << static_cast<int>(value);
  std::string status = ss.str();
  SendStatus(status);
}

void RPCTracker::ConnectionInfo::SendStatus(std::string status) {
  int length = status.size();

  connection_.SendAll(&length, sizeof(length));
  std::cout << "<< " << host_ << ":" << port_ << " " << status << std::endl;
  connection_.SendAll(status.data(), status.size());
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
  std::cout << __FILE__ << " " << __LINE__ << " ######################################"
            << std::endl;
  for (auto r : requests_) {
    std::cout << r << std::endl;
  }
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
  std::cout << __FILE__ << " " << __LINE__ << " " << requests_.size() << std::endl;
  std::cout << __FILE__ << " " << __LINE__ << " " << values_.size() << std::endl;
  while (!requests_.empty() && !values_.empty()) {
    PutInfo& pi = values_[0];
    RequestInfo& request = requests_[0];
    try {
      std::stringstream ss;
      ss << "[" << static_cast<int>(TRACKER_CODE::SUCCESS) << ", [\"" << pi.address_ << "\", "
         << pi.port_ << ", \"" << pi.match_key_ << "\"]]";
      std::string msg = ss.str();
      request.conn_->SendStatus(ss.str());
      std::string key = pi.conn_->key_;
      auto it =
          find(pi.conn_->pending_match_keys_.begin(), pi.conn_->pending_match_keys_.end(), key);
      if (it != pi.conn_->pending_match_keys_.end()) {
        pi.conn_->pending_match_keys_.erase(it);
      }
    } catch (...) {
      values_.push_back(pi);
    }

    values_.pop_front();
    requests_.pop_front();
  }
}

RPCTracker::ConnectionInfo::ConnectionInfo(RPCTracker* tracker, std::string host, int port,
                                           support::TCPSocket connection)
    : tracker_{tracker}, host_{host}, port_{port}, connection_{connection} {
  connection_task_ =
      std::async(std::launch::async, &RPCTracker::ConnectionInfo::ConnectionLoop, this);
}

void RPCTracker::ConnectionInfo::ConnectionLoop() {
  // Do magic handshake
  int magic = 0;
  ICHECK_EQ(connection_.RecvAll(&magic, sizeof(magic)), sizeof(magic));
  // ICHECK_EQ(magic, RPC_CODE::RPC_MAGIC);
  std::cout << __FILE__ << " " << __LINE__ << " magic=" << magic << std::endl;
  connection_.SendAll(&magic, sizeof(magic));

  while (true) {
    std::string json;
    try {
      std::cout << __FILE__ << " " << __LINE__ << " peer=" << host_ << ":" << port_ << std::endl;
      int packet_length = 0;
      ICHECK_EQ(connection_.RecvAll(&packet_length, sizeof(packet_length)), sizeof(packet_length));
      std::cout << __FILE__ << " " << __LINE__ << " packet_length=" << packet_length << std::endl;
      std::vector<char> buffer;
      buffer.reserve(packet_length);
      ICHECK_EQ(connection_.RecvAll(buffer.data(), packet_length), packet_length);
      json = std::string(buffer.data(), packet_length);
      std::cout << __FILE__ << " " << __LINE__ << " " << host_ << ":" << port_ << " " << json
                << std::endl;
    } catch (std::exception err) {
      std::cout << __FILE__ << " " << __LINE__ << " exception " << err.what() << std::endl;
    }

    std::istringstream is(json);
    dmlc::JSONReader reader(&is);
    int tmp;
    reader.BeginArray();
    reader.NextArrayItem();
    reader.ReadNumber(&tmp);
    reader.NextArrayItem();
    switch (static_cast<TRACKER_CODE>(tmp)) {
      case TRACKER_CODE::FAIL:
        std::cout << __FILE__ << " " << __LINE__ << " FAIL" << std::endl;
        break;
      case TRACKER_CODE::SUCCESS:
        std::cout << __FILE__ << " " << __LINE__ << " SUCCESS" << std::endl;
        break;
      case TRACKER_CODE::PING:
        std::cout << __FILE__ << " " << __LINE__ << " PING" << std::endl;
        break;
      case TRACKER_CODE::STOP:
        std::cout << __FILE__ << " " << __LINE__ << " STOP" << std::endl;
        break;
      case TRACKER_CODE::PUT: {
        std::cout << __FILE__ << " " << __LINE__ << " PUT" << std::endl;
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
        tracker_->Put(key, addr, port, match_key, shared_from_this());
        SendResponse(TRACKER_CODE::SUCCESS);
        break;
      }
      case TRACKER_CODE::REQUEST: {
        std::cout << __FILE__ << " " << __LINE__ << " REQUEST" << std::endl;
        std::string key;
        std::string user;
        int priority;
        reader.Read(&key);
        reader.NextArrayItem();
        reader.Read(&user);
        reader.NextArrayItem();
        reader.Read(&priority);
        reader.NextArrayItem();
        tracker_->Request(key, user, priority, shared_from_this());
        break;
      }
      case TRACKER_CODE::UPDATE_INFO: {
        std::cout << __FILE__ << " " << __LINE__ << " UPDATE_INFO" << std::endl;
        std::string key;
        std::string value;
        reader.BeginObject();
        reader.NextObjectItem(&key);
        reader.Read(&value);
        key_ = value;
        SendResponse(TRACKER_CODE::SUCCESS);
        std::cout << __FILE__ << " " << __LINE__ << " " << *this << std::endl;
        break;
      }
      case TRACKER_CODE::SUMMARY: {
        std::cout << __FILE__ << " " << __LINE__ << " SUMMARY" << std::endl;
        // [0, {"queue_info": {"abc": {"free": 0, "pending": 0}, "xyz": {"free": 5, "pending": 0},
        // "xyz1": {"free": 0, "pending": 0}}, "server_info": [{"addr": ["127.0.0.1", 51602], "key":
        // "server:xyz"}, {"addr": ["127.0.0.1", 51600], "key": "server:abc"}, {"addr":
        // ["127.0.0.1", 51606], "key": "server:proxy[xyz,xyz1]"}]}]
        std::stringstream ss;
        ss << "[" << static_cast<int>(TRACKER_CODE::SUCCESS) << ", {\"queue_info\": {"
           << tracker_->Summary() << "}, ";
        ss << "\"server_info\": [";
        int count = 0;
        for (auto conn : tracker_->connection_list_) {
          std::cout << __FILE__ << " " << __LINE__ << " *********** key " << conn->key_
                    << std::endl;
          if (conn->key_.substr(0, 6) == "server") {
            if (count++ > 0) {
              ss << ", ";
            }
            ss << "{\"addr\": [\"" << conn->host_ << "\", " << conn->port_ << "], \"key\": \""
               << conn->key_ << "\"}";
          }
        }
        ss << "]}]";
        SendStatus(ss.str());
        std::cout << __FILE__ << " " << __LINE__ << " " << ss.str() << std::endl;
        break;
      }
      case TRACKER_CODE::GET_PENDING_MATCHKEYS:
        std::cout << __FILE__ << " " << __LINE__ << " GET_PENDING_MATCHKEYS" << std::endl;
        break;
    }
  }
}

}  // namespace rpc
TVM_REGISTER_GLOBAL("rpc.RPCTrackerStart").set_body_typed(tvm::runtime::rpc::RPCTrackerEntry);
}  // namespace runtime
}  // namespace tvm
