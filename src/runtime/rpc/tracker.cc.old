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
namespace runtime{
namespace rpc {

// RPCTracker::RPCTracker(std::string host, int port, int port_end, bool silent) {
//   std::cout << __FILE__ << " " << __LINE__ << std::endl;
// }

// RPCTracker::~RPCTracker() {
//   std::cout << __FILE__ << " " << __LINE__ << std::endl;
// }

// void RPCTracker::Stop() {
//   std::cout << __FILE__ << " " << __LINE__ << std::endl;
// }

// void RPCTracker::Terminate() {
//   std::cout << __FILE__ << " " << __LINE__ << std::endl;
// }

RPCTrackerObj::RPCTrackerObj(std::string host, int port, int port_end, bool)
  : host{host}, port{port}, port_end{port_end} {
      std::cout << __FILE__ << " " << __LINE__ << std::endl;

}

  void RPCTrackerObj::Stop() {
      std::cout << __FILE__ << " " << __LINE__ << std::endl;
      }

  void RPCTrackerObj::Terminate() {
      std::cout << __FILE__ << " " << __LINE__ << std::endl;
      }



TVM_REGISTER_NODE_TYPE(RPCTrackerObj);
TVM_REGISTER_GLOBAL("rpc.RPCTracker").set_body_typed([](std::string host, int port, int port_end, bool silent) {
  return tvm::runtime::rpc::RPCTracker(host, port, port_end, silent);
});

}  // namespace rpc
}  // namespace runtime
}  // namespace tvm
