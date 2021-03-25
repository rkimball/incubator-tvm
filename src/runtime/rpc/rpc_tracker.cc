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

#include <iomanip>
#include <iostream>
#include <memory>

namespace tvm {
namespace runtime {
namespace rpc {

RPCTrackerObj::RPCTrackerObj(std::string host, int port, int port_end, bool silent)
    : host_{host} {
  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port, port_end);

  // Set socket so we can reuse the address later
  // listen_sock_.SetReuseAddress();

  listen_sock_.Listen();
  listener_task_ = std::make_unique<std::thread>(&RPCTrackerObj::ListenLoopEntry, this);
  // listener_task_->detach();
}

RPCTrackerObj::~RPCTrackerObj() { Terminate(); }

/*!
 * \brief ListenLoopProc The listen process.
 */
void RPCTrackerObj::ListenLoopEntry() {
  active_ = true;
  while (active_) {
    support::TCPSocket connection;
    try {
      connection = listen_sock_.Accept();
    } catch (std::exception err) {
      break;
    }

    RemoveStaleConnections();

    std::string peer_host;
    int peer_port;
    connection.GetPeerAddress(peer_host, peer_port);
    std::lock_guard<std::mutex> guard(mutex_);
    connection_list_.insert(
        std::make_shared<ConnectionInfo>(this, peer_host, peer_port, connection));
  }
}

void RPCTrackerObj::RemoveStaleConnections() {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  // Check the connection_list_ for stale connections
  std::lock_guard<std::mutex> guard(mutex_);
  std::set<std::shared_ptr<ConnectionInfo>> erase_list;
  for (auto conn : connection_list_) {
    if (conn->active_ == false) {
      std::cout << __FILE__ << " " << __LINE__ << " remove stale " << *conn << std::endl;
      conn->ShutdownThread();
      erase_list.insert(conn);
    }
  }
  for (auto conn : erase_list) {
    std::string key = conn->key_;
    std::cout << __FILE__ << " " << __LINE__ << " key " << key << std::endl;
    if (!key.empty()) {
      // This is a server
      // "server:rasp3b" -> "rasp3b"
      auto pos = key.find(':');
      if (pos != std::string::npos) {
        key = key.substr(pos + 1);
      }
      std::cout << __FILE__ << " " << __LINE__ << " key " << key << std::endl;
      auto scheduler = GetScheduler(key);
      if (scheduler) {
        std::cout << __FILE__ << " " << __LINE__ << " got scheduler for " << key << std::endl;
        scheduler->RemoveServer(conn);
      }
    }
    // Search through requests in the schedulers
    for (auto p : scheduler_map_) {
      p.second->RemoveClient(conn);
    }

    connection_list_.erase(conn);
  }
}

int RPCTrackerObj::GetPort() const { return my_port_; }

void RPCTrackerObj::Stop() {
  // For now call Terminate
  Terminate();
}

void RPCTrackerObj::Terminate() {
  // First shutdown the listen socket so we don't get any new connections
  listen_sock_.Shutdown();
  listen_sock_.Close();
  if (listener_task_->joinable()) {
    listener_task_->join();
  }
  active_ = false;
  listener_task_ = nullptr;

  // Second clear out any open connections since those have no tracker
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto conn : connection_list_) {
    if (!conn->connection_.IsClosed()) {
      conn->Close();
    }
  }
  scheduler_map_.clear();
  connection_list_.clear();
}

std::shared_ptr<RPCTrackerObj::PriorityScheduler> RPCTrackerObj::GetScheduler(std::string key) {
  std::shared_ptr<RPCTrackerObj::PriorityScheduler> scheduler;
  if (scheduler_map_.find(key) == scheduler_map_.end()) {
    // There is no scheduler for this key yet so add one
    scheduler_map_.insert({key, std::make_shared<PriorityScheduler>(key)});
  }
  auto it = scheduler_map_.find(key);
  if (it != scheduler_map_.end()) {
    scheduler = it->second;
  }
  return scheduler;
}

void RPCTrackerObj::Put(std::string key, std::string address, int port, std::string match_key,
                        ConnectionInfo* connection) {
  std::lock_guard<std::mutex> guard(mutex_);
  std::shared_ptr<ConnectionInfo> conn;
  for (auto c : connection_list_) {
    if (c.get() == connection) {
      conn = c;
    }
  }
  if (auto scheduler = GetScheduler(key)) {
    scheduler->Put(address, port, match_key, conn);
  }
}

void RPCTrackerObj::Request(std::string key, std::string user, int priority,
                            ConnectionInfo* connection) {
  std::lock_guard<std::mutex> guard(mutex_);
  std::shared_ptr<ConnectionInfo> conn;
  for (auto c : connection_list_) {
    if (c.get() == connection) {
      conn = c;
    }
  }
  if (auto scheduler = GetScheduler(key)) {
    scheduler->Request(user, priority, conn);
  }
}

std::string RPCTrackerObj::Summary() {
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

void RPCTrackerObj::Close(ConnectionInfo* connection) {
  std::cout << __FILE__ << " " << __LINE__ << " " << *connection << std::endl;
  std::lock_guard<std::mutex> guard(mutex_);
  std::cout << __FILE__ << " " << __LINE__ << " **************************\n";
  std::shared_ptr<ConnectionInfo> conn;
  for (auto c : connection_list_) {
    if (c.get() == connection) {
      conn = c;
    }
  }

  std::cout << __FILE__ << " " << __LINE__ << " **************************\n";
  connection_list_.erase(conn);
  std::string key = conn->key_;
  if (!key.empty()) {
    // "server:rasp3b" -> "rasp3b"
    auto pos = key.find(':');
    if (pos != std::string::npos) {
      key = key.substr(pos + 1);
    }
    // TODO: rkimball remove values from scheduler_map
  }
}

RPCTrackerObj::PriorityScheduler::PriorityScheduler(std::string key) : key_{key} {}

RPCTrackerObj::PriorityScheduler::~PriorityScheduler() {}

void RPCTrackerObj::PriorityScheduler::Request(std::string user, int priority,
                                               std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  requests_.emplace_back(user, priority, request_count_++, conn);
  std::sort(requests_.begin(), requests_.end(),
            [](const RPCTrackerObj::RequestInfo& a, const RPCTrackerObj::RequestInfo& b) {
              return a.priority_ > b.priority_;
            });
  Schedule();
}

void RPCTrackerObj::PriorityScheduler::Put(std::string address, int port, std::string match_key,
                                           std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  values_.emplace_back(address, port, match_key, conn);
  Schedule();
}

void RPCTrackerObj::PriorityScheduler::Remove(PutInfo value) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = std::find(values_.begin(), values_.end(), value);
  if (it != values_.end()) {
    values_.erase(it);
    Schedule();
  }
}

void RPCTrackerObj::PriorityScheduler::RemoveServer(std::shared_ptr<ConnectionInfo> conn) {
  std::cout << __FILE__ << " " << __LINE__ << " remove server " << *conn << std::endl;
  std::lock_guard<std::mutex> guard(mutex_);
  bool erased = true;
  while(erased) {
    erased = false;
    for (auto it=values_.begin(); it!=values_.end(); ++it) {
      if (it->conn_ == conn) {
        values_.erase(it);
        std::cout << __FILE__ << " " << __LINE__ << " erase " << std::endl;
        erased = true;
        break;
      }
    }
  }
  // std::vector<PutInfo> erase_list;
  // for (auto put : values_) {
  //   if (put.conn_ == conn) {
  //     erase_list.push_back(put);
  //     std::cout << __FILE__ << " " << __LINE__ << " found put to erase " << put << std::endl;
  //   }
  // }
  // for (auto item_to_erase : erase_list) {
  //   for (auto it=values_.begin(); it!=values_.end(); ++it) {
  //     if (it->conn_ == item_to_erase) {
  //       std::cout << __FILE__ << " " << __LINE__ << " erasing put " << item_to_erase << std::endl;
  //       values_.erase(it);
  //     }
  //   }
  // }
}

void RPCTrackerObj::PriorityScheduler::RemoveClient(std::shared_ptr<ConnectionInfo> conn) {
  std::cout << __FILE__ << " " << __LINE__ << " remove client " << *conn << std::endl;
  std::lock_guard<std::mutex> guard(mutex_);
  bool erased = true;
  while(erased) {
    erased = false;
    for (auto it=requests_.begin(); it!=requests_.end(); ++it) {
      if (it->conn_ == conn) {
        requests_.erase(it);
        std::cout << __FILE__ << " " << __LINE__ << " erase " << std::endl;
        erased = true;
        break;
      }
    }
  }
  // std::vector<RequestInfo> erase_list;
  // for (auto request : requests_) {
  //   if (request.conn_ == conn) {
  //     erase_list.push_back(request);
  //     std::cout << __FILE__ << " " << __LINE__ << " found request to erase " << request << std::endl;
  //   }
  // }
  // for (auto item_to_erase : erase_list) {
  //   auto it = std::find(requests_.begin(), requests_.end(), item_to_erase);
  //   if (it != requests_.end()) {
  //     std::cout << __FILE__ << " " << __LINE__ << " erasing put " << item_to_erase << std::endl;
  //     requests_.erase(it);
  //   }
  // }
}

std::string RPCTrackerObj::PriorityScheduler::Summary() {
  std::stringstream ss;
  ss << "{\"free\": " << values_.size() << ", \"pending\": " << requests_.size() << "}";
  return ss.str();
}

void RPCTrackerObj::PriorityScheduler::Schedule() {
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

ConnectionInfo::ConnectionInfo(RPCTrackerObj* tracker, std::string host, int port,
                               support::TCPSocket connection)
    : tracker_{tracker}, host_{host}, port_{port}, connection_{connection} {
  connection_task_ = std::thread(&ConnectionInfo::ConnectionLoopEntry, this);
  connection_task_.detach();
}

ConnectionInfo::~ConnectionInfo() {
  std::cout << __FILE__ << " " << __LINE__ << " dtor " << *this << std::endl;
  Close(); }

void ConnectionInfo::Close() {
  if (!connection_.IsClosed()) {
    connection_.Shutdown();
    connection_.Close();
  }
}

void ConnectionInfo::ShutdownThread() {
  if (std::this_thread::get_id() != connection_task_.get_id()) {
    if (connection_task_.joinable()) {
      connection_task_.join();
    }
  }
}

int ConnectionInfo::RecvAll(void* data, size_t length) {
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

int ConnectionInfo::SendAll(const void* data, size_t length) {
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

int ConnectionInfo::SendResponse(RPCTrackerObj::TRACKER_CODE value) {
  std::stringstream ss;
  ss << static_cast<int>(value);
  std::string status = ss.str();
  return SendStatus(status);
}

int ConnectionInfo::SendStatus(std::string status) {
  int length = status.size();
  bool fail = false;

  std::cout << host_ << ":" << port_ << " << " << status << std::endl;

  if (SendAll(&length, sizeof(length)) != sizeof(length)) {
    fail = true;
  }
  if (!fail && SendAll(status.data(), status.size()) != length) {
    fail = true;
  }
  return fail ? -1 : length;
}

void ConnectionInfo::ConnectionLoopEntry() {
  ConnectionLoop();
  Close();
  active_ = false;
  std::cout << __FILE__ << " " << __LINE__ << " exit " << *this << std::endl;
}

void ConnectionInfo::ConnectionLoop() {
  // Do magic handshake
  int magic = 0;
  if (RecvAll(&magic, sizeof(magic)) == -1) {
    // Error setting up connection
    return;
  }
  if (magic != static_cast<int>(RPCTrackerObj::RPC_CODE::RPC_TRACKER_MAGIC)) {
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

    tracker_->RemoveStaleConnections();

    std::cout << __FILE__ << " " << __LINE__ << " " << host_ << ":" << port_ << " >> " << json << std::endl;

    std::istringstream is(json);
    dmlc::JSONReader reader(&is);
    int tmp;
    reader.BeginArray();
    reader.NextArrayItem();
    reader.ReadNumber(&tmp);
    reader.NextArrayItem();
    switch (static_cast<RPCTrackerObj::TRACKER_CODE>(tmp)) {
      case RPCTrackerObj::TRACKER_CODE::FAIL:
        break;
      case RPCTrackerObj::TRACKER_CODE::SUCCESS:
        break;
      case RPCTrackerObj::TRACKER_CODE::PING:
        if (SendResponse(RPCTrackerObj::TRACKER_CODE::SUCCESS) == -1) {
          // Failed to send response so connection broken
          return;
        }
        break;
      case RPCTrackerObj::TRACKER_CODE::STOP:
        if (SendResponse(RPCTrackerObj::TRACKER_CODE::SUCCESS) == -1) {
          // Failed to send response so connection broken
          return;
        }

        if (auto tracker = tracker_) {
          tracker->Stop();
        }
        break;
      case RPCTrackerObj::TRACKER_CODE::PUT: {
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
        if (auto tracker = tracker_) {
          tracker->Put(key, addr, port, match_key, this);
        }
        // put_values_.insert(put_info);
        if (SendResponse(RPCTrackerObj::TRACKER_CODE::SUCCESS) == -1) {
          // Failed to send response so connection broken
          return;
        }
        break;
      }
      case RPCTrackerObj::TRACKER_CODE::REQUEST: {
        std::string key;
        std::string user;
        int priority;
        reader.Read(&key);
        reader.NextArrayItem();
        reader.Read(&user);
        reader.NextArrayItem();
        reader.Read(&priority);
        reader.NextArrayItem();
        tracker_->Request(key, user, priority, this);
        break;
      }
      case RPCTrackerObj::TRACKER_CODE::UPDATE_INFO: {
        std::string key;
        std::string value;
        reader.BeginObject();
        reader.NextObjectItem(&key);
        reader.Read(&value);
        key_ = value;
        if (SendResponse(RPCTrackerObj::TRACKER_CODE::SUCCESS) == -1) {
          // Failed to send response so connection broken
          return;
        }
        break;
      }
      case RPCTrackerObj::TRACKER_CODE::SUMMARY: {
        if (auto tracker = tracker_) {
          std::stringstream ss;
          ss << "[" << static_cast<int>(RPCTrackerObj::TRACKER_CODE::SUCCESS)
             << ", {\"queue_info\": {" << tracker->Summary() << "}, ";
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
            return;
          }
        }
        break;
      }
      case RPCTrackerObj::TRACKER_CODE::GET_PENDING_MATCHKEYS:
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
          return;
        }
        break;
    }
  }
}

TVM_REGISTER_NODE_TYPE(RPCTrackerObj);
TVM_REGISTER_GLOBAL("rpc.RPCTracker")
    .set_body_typed([](std::string host, int port, int port_end, bool silent) {
      return tvm::runtime::rpc::RPCTracker(host, port, port_end, silent);
    });

}  // namespace rpc
}  // namespace runtime
}  // namespace tvm
