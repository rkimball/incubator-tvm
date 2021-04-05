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

#include "tracker.h"

#include <dmlc/json.h>
#include <tvm/runtime/registry.h>

#include <iomanip>
#include <iostream>
#include <memory>

namespace tvm {
namespace rpc {

class PutInfo;

/*!
 * \brief The ConnectionInfo class tracks each connection to the RPC Tracker.
 *
 * Each instance of this class handles a single socket connection to the RPC Tracker.
 */
class ConnectionInfo {
 public:
  ConnectionInfo(TrackerObj* tracker, std::string host, int port, support::TCPSocket connection);
  ~ConnectionInfo();
  TrackerObj* tracker_;
  std::thread connection_task_;
  std::string host_;
  int port_;
  support::TCPSocket connection_;
  std::string key_;
  std::set<std::string> pending_match_keys_;
  std::set<std::shared_ptr<PutInfo>> put_values_;
  bool active_ = true;

  void ConnectionLoopEntry();
  void ConnectionLoop();
  void ProcessMessage(std::string json);
  int SendStatus(std::string status);
  int SendResponse(TrackerObj::TRACKER_CODE value);
  int RecvAll(void* data, size_t length);
  int SendAll(const void* data, size_t length);
  void Close();
  void ShutdownThread();

  friend std::ostream& operator<<(std::ostream& out, const ConnectionInfo& info) {
    out << "ConnectionInfo(" << info.host_ << ":" << info.port_ << " key=" << info.key_ << ")";
    return out;
  }
};

/*!
 * \brief The RequestInfo class tracking information from REQUEST messages.
 *
 * The class contains information for tracking an RPC Request call.
 */
class RequestInfo {
 public:
  RequestInfo() = default;
  RequestInfo(const RequestInfo&) = default;
  RequestInfo(std::string user, int priority, int request_count,
              std::shared_ptr<ConnectionInfo> conn)
      : user_{user}, priority_{priority}, request_count_{request_count}, conn_{conn} {}

  friend std::ostream& operator<<(std::ostream& out, const RequestInfo& info) {
    out << "RequestInfo(" << info.priority_ << ", \"" << info.user_ << "\", " << info.request_count_
        << ")";
    return out;
  }
  std::string user_;
  int priority_;
  int request_count_;
  std::shared_ptr<ConnectionInfo> conn_;
};

/*!
 * \brief The PutInfo class tracks the information from PUT messages.
 *
 * This class contains information for tracking an RPC Put call.
 */
class PutInfo {
 public:
  PutInfo(std::string address, int port, std::string match_key,
          std::shared_ptr<ConnectionInfo> conn)
      : address_{address}, port_{port}, match_key_{match_key}, conn_{conn} {}
  std::string address_;
  int port_;
  std::string match_key_;
  std::shared_ptr<ConnectionInfo> conn_;

  bool operator==(const PutInfo& pi) { return pi.match_key_ == match_key_; }
  friend std::ostream& operator<<(std::ostream& out, const PutInfo& obj) {
    out << "PutInfo(" << obj.address_ << ":" << obj.port_ << ", " << obj.match_key_ << ")";
    return out;
  }
};

/*!
 * \brief The PriorityScheduler handles request messages in a priority order.
 *
 * The priority is passed in the REQUEST message with higher numeric values being processed
 * first.
 */
class PriorityScheduler {
 public:
  PriorityScheduler(std::string key);
  ~PriorityScheduler();
  void Put(std::string address, int port, std::string match_key,
           std::shared_ptr<ConnectionInfo> conn);
  void Request(std::string user, int priority, std::shared_ptr<ConnectionInfo> conn);
  void Remove(PutInfo value);
  std::string Summary();
  void Schedule();
  void RemoveServer(std::shared_ptr<ConnectionInfo> conn);
  void RemoveClient(std::shared_ptr<ConnectionInfo> conn);

  std::mutex mutex_;
  std::string key_;
  size_t request_count_ = 0;
  std::deque<PutInfo> values_;
  std::deque<RequestInfo> requests_;
};

TrackerObj::TrackerObj(std::string host, int port, int port_end, bool silent) : host_{host} {
  listen_sock_.Create();
  my_port_ = listen_sock_.TryBindHost(host_, port, port_end);

  // Set socket so we can reuse the address later
  listen_sock_.SetReuseAddress();

  listen_sock_.Listen();
  listener_task_ = std::make_unique<std::thread>(&TrackerObj::ListenLoopEntry, this);
}

TrackerObj::~TrackerObj() { Terminate(); }

/*!
 * \brief ListenLoopProc The listen process.
 */
void TrackerObj::ListenLoopEntry() {
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

void TrackerObj::RemoveStaleConnections() {
  // Check the connection_list_ for stale connections
  std::lock_guard<std::mutex> guard(mutex_);
  std::set<std::shared_ptr<ConnectionInfo>> erase_list;
  for (auto conn : connection_list_) {
    if (conn->active_ == false) {
      conn->ShutdownThread();
      erase_list.insert(conn);
    }
  }
  for (auto conn : erase_list) {
    // Search through requests in the schedulers
    for (auto p : scheduler_map_) {
      p.second->RemoveServer(conn);
      p.second->RemoveClient(conn);
    }
    connection_list_.erase(conn);
  }
}

int TrackerObj::GetPort() const { return my_port_; }

void TrackerObj::Stop() {
  // For now call Terminate
  Terminate();
}

void TrackerObj::Terminate() {
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

std::shared_ptr<PriorityScheduler> TrackerObj::GetScheduler(std::string key) {
  std::shared_ptr<PriorityScheduler> scheduler;
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

void TrackerObj::Put(std::string key, std::string address, int port, std::string match_key,
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

void TrackerObj::Request(std::string key, std::string user, int priority,
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

std::string TrackerObj::Summary() {
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

void TrackerObj::Close(ConnectionInfo* connection) {
  std::lock_guard<std::mutex> guard(mutex_);
  std::shared_ptr<ConnectionInfo> conn;
  for (auto c : connection_list_) {
    if (c.get() == connection) {
      conn = c;
    }
  }

  connection_list_.erase(conn);
}

PriorityScheduler::PriorityScheduler(std::string key) : key_{key} {}

PriorityScheduler::~PriorityScheduler() {}

void PriorityScheduler::Request(std::string user, int priority,
                                std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  requests_.emplace_back(user, priority, request_count_++, conn);
  std::sort(requests_.begin(), requests_.end(),
            [](const RequestInfo& a, const RequestInfo& b) { return a.priority_ > b.priority_; });
  Schedule();
}

void PriorityScheduler::Put(std::string address, int port, std::string match_key,
                            std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  values_.emplace_back(address, port, match_key, conn);
  Schedule();
}

void PriorityScheduler::Remove(PutInfo value) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = std::find(values_.begin(), values_.end(), value);
  if (it != values_.end()) {
    values_.erase(it);
    Schedule();
  }
}

namespace {
template <typename T>
void RemoveRemote(T& list, std::shared_ptr<ConnectionInfo> conn) {
  bool erased = true;
  while (erased) {
    erased = false;
    for (auto it = list.begin(); it != list.end(); ++it) {
      if (it->conn_ == conn) {
        list.erase(it);
        erased = true;
        break;
      }
    }
  }
}
}  // namespace

void PriorityScheduler::RemoveServer(std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  RemoveRemote(values_, conn);
}

void PriorityScheduler::RemoveClient(std::shared_ptr<ConnectionInfo> conn) {
  std::lock_guard<std::mutex> guard(mutex_);
  RemoveRemote(requests_, conn);
}

std::string PriorityScheduler::Summary() {
  std::lock_guard<std::mutex> guard(mutex_);
  std::stringstream ss;
  ss << "{\"free\": " << values_.size() << ", \"pending\": " << requests_.size() << "}";
  return ss.str();
}

void PriorityScheduler::Schedule() {
  while (!requests_.empty() && !values_.empty()) {
    PutInfo& pi = values_[0];
    RequestInfo& request = requests_[0];
    try {
      std::stringstream ss;
      ss << "[" << static_cast<int>(TrackerObj::TRACKER_CODE::SUCCESS) << ", [\"" << pi.address_
         << "\", " << pi.port_ << ", \"" << pi.match_key_ << "\"]]";
      request.conn_->SendStatus(ss.str());
      pi.conn_->pending_match_keys_.erase(pi.match_key_);
    } catch (...) {
      values_.push_back(pi);
    }

    values_.pop_front();
    requests_.pop_front();
  }
}

ConnectionInfo::ConnectionInfo(TrackerObj* tracker, std::string host, int port,
                               support::TCPSocket connection)
    : tracker_{tracker}, host_{host}, port_{port}, connection_{connection} {
  connection_task_ = std::thread(&ConnectionInfo::ConnectionLoopEntry, this);
  connection_task_.detach();
}

ConnectionInfo::~ConnectionInfo() { Close(); }

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

int ConnectionInfo::SendResponse(TrackerObj::TRACKER_CODE value) {
  std::stringstream ss;
  ss << static_cast<int>(value);
  std::string status = ss.str();
  return SendStatus(status);
}

int ConnectionInfo::SendStatus(std::string status) {
  int length = status.size();
  bool fail = false;

  // std::cout << host_ << ":" << port_ << " << " << status << std::endl;

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
}

void ConnectionInfo::ConnectionLoop() {
  // Do magic handshake
  int magic = 0;
  if (RecvAll(&magic, sizeof(magic)) == -1) {
    // Error setting up connection
    return;
  }
  if (magic != static_cast<int>(TrackerObj::RPC_CODE::RPC_TRACKER_MAGIC)) {
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
      SendResponse(TrackerObj::TRACKER_CODE::FAIL);
    }
  }
}

void ConnectionInfo::ProcessMessage(std::string json) {
  tracker_->RemoveStaleConnections();

  // std::cout << __FILE__ << " " << __LINE__ << " " << host_ << ":" << port_ << " >> " << json
  //           << std::endl;

  std::istringstream is(json);
  dmlc::JSONReader reader(&is);
  int tmp;
  reader.BeginArray();
  reader.NextArrayItem();
  reader.ReadNumber(&tmp);
  reader.NextArrayItem();
  switch (static_cast<TrackerObj::TRACKER_CODE>(tmp)) {
    case TrackerObj::TRACKER_CODE::FAIL:
      break;
    case TrackerObj::TRACKER_CODE::SUCCESS:
      break;
    case TrackerObj::TRACKER_CODE::PING:
      if (SendResponse(TrackerObj::TRACKER_CODE::SUCCESS) == -1) {
        // Failed to send response so connection broken
        return;
      }
      break;
    case TrackerObj::TRACKER_CODE::STOP:
      if (SendResponse(TrackerObj::TRACKER_CODE::SUCCESS) == -1) {
        // Failed to send response so connection broken
        return;
      }

      if (auto tracker = tracker_) {
        tracker->Stop();
      }
      break;
    case TrackerObj::TRACKER_CODE::PUT: {
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
      if (SendResponse(TrackerObj::TRACKER_CODE::SUCCESS) == -1) {
        // Failed to send response so connection broken
        return;
      }
      break;
    }
    case TrackerObj::TRACKER_CODE::REQUEST: {
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
    case TrackerObj::TRACKER_CODE::UPDATE_INFO: {
      std::string key;
      std::string value;
      reader.BeginObject();
      reader.NextObjectItem(&key);
      reader.Read(&value);
      key_ = value;
      if (SendResponse(TrackerObj::TRACKER_CODE::SUCCESS) == -1) {
        // Failed to send response so connection broken
        return;
      }
      break;
    }
    case TrackerObj::TRACKER_CODE::SUMMARY: {
      if (auto tracker = tracker_) {
        std::stringstream ss;
        ss << "[" << static_cast<int>(TrackerObj::TRACKER_CODE::SUCCESS) << ", {\"queue_info\": {"
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
          return;
        }
      }
      break;
    }
    case TrackerObj::TRACKER_CODE::GET_PENDING_MATCHKEYS: {
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
    default:
      SendResponse(TrackerObj::TRACKER_CODE::FAIL);
      break;
  }
}

TVM_REGISTER_NODE_TYPE(TrackerObj);
TVM_REGISTER_GLOBAL("rpc.Tracker")
    .set_body_typed([](std::string host, int port, int port_end, bool silent) {
      return tvm::rpc::Tracker(host, port, port_end, silent);
    });

}  // namespace rpc
}  // namespace tvm
