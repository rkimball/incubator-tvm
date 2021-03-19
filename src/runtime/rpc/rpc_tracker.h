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

#include "../../support/socket.h"

namespace tvm {
namespace runtime {
namespace rpc {

class RPCTrackerNode : public Object {
public:
  static constexpr const char* _type_key = "RPCTrackerNode";
  TVM_DECLARE_BASE_OBJECT_INFO(RPCTrackerNode, Object);
};

class RPCTracker : public ObjectRef {
public:
  RPCTracker(std::string host, int port, int port_end, bool silent = true);
  ~RPCTracker();
  void Stop();
  void Terminate();

  TVM_DEFINE_MUTABLE_OBJECT_REF_METHODS(RPCTracker, ObjectRef, RPCTrackerNode);
};

}  // namespace rpc
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_RPC_RPC_TRACKER_H_
