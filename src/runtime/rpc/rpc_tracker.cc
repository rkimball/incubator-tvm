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
    connection_list_.emplace_back(this, peer_host, peer_port, connection);
    std::cout << __FILE__ << " " << __LINE__ << " peer=" << peer_host << ":" << peer_port
              << std::endl;
  }
}

void RPCTracker::Request(std::string key, std::string user, int priority, response_callback_t response_callback) {
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, PriorityScheduler(key)});
  }
  scheduler_map_.at(key).Request(user, priority, response_callback);
}

void RPCTracker::ConnectionInfo::SendResponse(support::TCPSocket& conn, TRACKER_CODE value) {
  std::stringstream ss;
  ss << static_cast<int>(value);
  std::string status = ss.str();
  int length = status.size();

  conn.SendAll(&length, sizeof(length));
  std::cout << "<< " << status << std::endl;
  conn.SendAll(status.data(), status.size());
}

RPCTracker::PriorityScheduler::PriorityScheduler(std::string key) : key_{key} {
}

void RPCTracker::PriorityScheduler::Request(std::string user, int priority, response_callback_t response_callback){
  requests_.emplace_back(user, priority, request_count_++, response_callback);
  std::sort(requests_.begin(), requests_.end(), [](const RPCTracker::RequestInfo& a, const RPCTracker::RequestInfo& b){
    return a.priority_ > b.priority_;
  });
  std::cout << __FILE__ << " " << __LINE__ << " ######################################" << std::endl;
  for (auto r : requests_) {
    std::cout << r << std::endl;
  }
  Schedule();
}

void RPCTracker::PriorityScheduler::Put(ConnectionInfo* value) {
  values_.push_back(value);
  Schedule();
}
void RPCTracker::PriorityScheduler::Remove(ConnectionInfo* value) {
  auto it = std::find(values_.begin(), values_.end(), value);
  if (it != values_.end()) {
    values_.erase(it);
    Schedule();
  }
}
void RPCTracker::PriorityScheduler::Summary() {}

void RPCTracker::PriorityScheduler::Schedule() {
  std::cout << __FILE__ << " " << __LINE__ << " " << requests_.size() << std::endl;
  std::cout << __FILE__ << " " << __LINE__ << " " << values_.size() << std::endl;
  while (!requests_.empty() && !values_.empty()) {
    ConnectionInfo* conn = values_[0];
    RequestInfo &request = requests_[0];
    if(request.response_callback_(conn)) {
      std::string key = conn->key_;
      auto it = find(conn->pending_match_keys_.begin(), conn->pending_match_keys_.end(), key);
      if (it != conn->pending_match_keys_.end()) {
        conn->pending_match_keys_.erase(it);
      }
    } else {
      values_.push_back(conn);
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
    std::cout << __FILE__ << " " << __LINE__ << " peer=" << host_ << ":" << port_ << std::endl;
    int packet_length = 0;
    ICHECK_EQ(connection_.RecvAll(&packet_length, sizeof(packet_length)), sizeof(packet_length));
    std::cout << __FILE__ << " " << __LINE__ << " packet_length=" << packet_length << std::endl;
    std::vector<char> buffer;
    buffer.reserve(packet_length);
    ICHECK_EQ(connection_.RecvAll(buffer.data(), packet_length), packet_length);
    std::string json(buffer.data(), packet_length);
    std::cout << __FILE__ << " " << __LINE__ << " " << json << std::endl;

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
        reader.Read(&key);
        reader.NextArrayItem();
        reader.BeginArray();
        reader.NextArrayItem();
        reader.Read(&port);
        reader.NextArrayItem();
        reader.Read(&match_key);
        std::cout << __FILE__ << " " << __LINE__ << " key " << key << std::endl;
        std::cout << __FILE__ << " " << __LINE__ << " port " << port << std::endl;
        std::cout << __FILE__ << " " << __LINE__ << " matchkey " << match_key << std::endl;
        // SendResponse(connection_, TRACKER_CODE::SUCCESS);
        tracker_->Put(key, host_, port, match_key);
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
        tracker_->Request(key, user, priority, [&](ConnectionInfo* conn){
                  std::cout << __FILE__ << " " << __LINE__ << std::endl;
                  std::cout << *conn << std::endl;
                  std::cout << *this << std::endl;
                  SendResponse(connection_, TRACKER_CODE::SUCCESS);
                  return true;});
        std::cout << __FILE__ << " " << __LINE__ << " key " << key << std::endl;
        std::cout << __FILE__ << " " << __LINE__ << " user " << user << std::endl;
        std::cout << __FILE__ << " " << __LINE__ << " priority " << priority << std::endl;
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
        SendResponse(connection_, TRACKER_CODE::SUCCESS);
        std::cout << __FILE__ << " " << __LINE__ << " " << *this << std::endl;
        break;
      }
      case TRACKER_CODE::SUMMARY:
        std::cout << __FILE__ << " " << __LINE__ << " SUMMARY" << std::endl;
        break;
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
