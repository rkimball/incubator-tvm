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

#ifndef TVM_IR_VISUALIZE_H
#define TVM_IR_VISUALIZE_H

#include <string>
#include <vector>

namespace tvm {
namespace ir {

class NodeInfo;

class EdgeInfo {
 public:
  virtual ~EdgeInfo() {}
  virtual std::string GetType() const = 0;
  virtual std::string GetShape() const = 0;
  virtual size_t GetIndex() const = 0;
  virtual const NodeInfo* GetInfo() const = 0;
};

class NodeInfo {
 public:
  explicit NodeInfo() {
    static size_t next_id = 0;
    unique_name_ = "node_" + std::to_string(next_id++);
  }
  virtual ~NodeInfo() {}
  virtual std::string GetName() const = 0;
  virtual std::vector<const EdgeInfo*> GetInputs() const = 0;
  virtual std::vector<const EdgeInfo*> GetOutputs() const = 0;
  virtual std::string GetUniqueName() const { return unique_name_; }

 private:
  std::string unique_name_;
};

void VisualizeGraph(const std::vector<NodeInfo*>& node_info, std::string output_path);

}  // namespace ir
}  // namespace tvm

#endif  // TVM_IR_VISUALIZE_H
