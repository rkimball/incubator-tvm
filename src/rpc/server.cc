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

#include "server.h"

#include <dmlc/json.h>
#include <tvm/runtime/registry.h>

#include <iomanip>
#include <iostream>
#include <memory>

namespace tvm {
namespace rpc {

ServerObj::ServerObj(std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                     std::string tracker_host, int tracker_port, std::string key,
                     std::string load_library, std::string custom_host, int custom_port,
                     bool silent)
    : host_(host) {
  if (host == "" || host == "0.0.0.0" || host == "localhost") {
    host_ = host;
  }
  std::cout << __FILE__ << " " << __LINE__ << " hello from ServerObj" << std::endl;
}

ServerObj::~ServerObj() {}

void ServerObj::Stop() {}

void ServerObj::Terminate() {}

int ServerObj::GetPort() const { return my_port_; }

TVM_REGISTER_NODE_TYPE(ServerObj);
TVM_REGISTER_GLOBAL("rpc.Server")
    .set_body_typed([](std::string host, int port, int port_end, bool is_proxy, bool use_popen,
                       std::string tracker_host, int tracker_port, std::string key,
                       std::string load_library, std::string custom_host, int custom_port,
                       bool silent) {
      return tvm::rpc::Server(host, port, port_end, is_proxy, use_popen, tracker_host, tracker_port,
                              key, load_library, custom_host, custom_port, silent);
    });

}  // namespace rpc
}  // namespace tvm
