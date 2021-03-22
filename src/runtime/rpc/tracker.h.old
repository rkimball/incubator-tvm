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

/*!
 * \file rpc_tracker.h
 * \brief RPC Tracker.
 */
#ifndef TVM_RUNTIME_RPC_RPC_TRACKER_H_
#define TVM_RUNTIME_RPC_RPC_TRACKER_H_

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <functional>

#include <tvm/runtime/c_runtime_api.h>
#include <tvm/runtime/device_api.h>
#include <tvm/runtime/object.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>
#include <tvm/node/reflection.h>

#include "../../support/socket.h"

namespace tvm {
namespace runtime {
namespace rpc {

class RPCTrackerObj : public Object {
public:
  void VisitAttrs(tvm::AttrVisitor* av) {
    av->Visit("port", &port);
    av->Visit("port_end", &port_end);
    av->Visit("host", &host);
  }
  static constexpr const char* _type_key = "rpc.RPCTracker";
  TVM_DECLARE_BASE_OBJECT_INFO(RPCTrackerObj, Object);

  RPCTrackerObj(){}
  explicit RPCTrackerObj(std::string host, int port, int port_end, bool silent = true);
  // ~RPCTracker();
  void Stop();
  void Terminate();

  int64_t port;
  int64_t port_end;
  String host;
};

class RPCTracker : public ObjectRef {
public:
  explicit RPCTracker(std::string host, int port, int port_end, bool silent){
    data_ = make_object<RPCTrackerObj>(host, port, port_end, silent);
  }

  TVM_DEFINE_MUTABLE_OBJECT_REF_METHODS(RPCTracker, ObjectRef, RPCTrackerObj);
};

}  // namespace rpc
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_RPC_RPC_TRACKER_H_
