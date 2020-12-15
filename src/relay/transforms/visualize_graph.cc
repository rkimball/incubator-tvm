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
 * \file visualize_graph.cc
 */

#include <tvm/ir/visualize.h>
#include <tvm/relay/analysis.h>
#include <tvm/relay/attrs/transform.h>
#include <tvm/relay/expr_functor.h>
#include <tvm/relay/interpreter.h>
#include <tvm/relay/op.h>
#include <tvm/relay/op_attr_types.h>
#include <tvm/relay/transform.h>
#include <tvm/runtime/container.h>
#include <tvm/runtime/ndarray.h>
#include <tvm/runtime/object.h>

#include <fstream>

#include "../../printer/text_printer.h"
#include "../../support/utils.h"
#include "../ir/indexed_graph.h"
#include "pattern_utils.h"

namespace tvm {
namespace relay {

class RelayNodeInfo;

class RelayEdgeInfo : public tvm::ir::EdgeInfo {
 public:
  RelayEdgeInfo(const RelayNodeInfo* node) : node_(node) {}

  std::string GetType() const override;
  std::string GetShape() const override;
  size_t GetIndex() const override { return 0; }
  const tvm::ir::NodeInfo* GetInfo() const override { return (const tvm::ir::NodeInfo*)node_; }

 private:
  std::string GetNodeType(const Type& checked_type, size_t index) const {
    std::string type = "unknown type";
    if (const TensorTypeNode* tensor_type = checked_type.as<TensorTypeNode>()) {
      // tensor_type->shape;
      type = DLDataType2String(tensor_type->dtype);
    } else if (const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
      type = GetNodeType(ttn->fields[index], 0);
    }
    return type;
  }

  std::string GetNodeShape(const Type& checked_type, size_t index) const {
    std::string shape = "unknown shape";
    if (const TensorTypeNode* tensor_type = checked_type.as<TensorTypeNode>()) {
      std::vector<std::string> axes;
      for (auto e : tensor_type->shape) {
        axes.push_back(tvm::TextPrinter(false, nullptr).PrintFinal(e).str());
      }
      shape = "[" + tvm::support::Join(axes, ",") + "]";
    } else if (const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
      shape = GetNodeShape(ttn->fields[index], 0);
    }
    return shape;
  }

  std::string GetNodeType(const Expr& expr, size_t index) const {
    std::string type = "unknown type";
    if (const RelayExprNode* rexpr = expr.as<RelayExprNode>()) {
      type = GetNodeType(rexpr->checked_type_, index);
    }
    // else if(const TupleTypeNode* ttn = expr.as<TupleTypeNode>()) {
    //   type = GetNodeType(ttn->fields[index]);
    // }
    return type;
  }

  std::string GetNodeShape(const Expr& expr, size_t index) const {
    std::string shape = "unknown shape";
    if (const RelayExprNode* rexpr = expr.as<RelayExprNode>()) {
      shape = GetNodeShape(rexpr->checked_type_, index);
    }
    // else if(const TupleTypeNode* ttn = expr.as<TupleTypeNode>()) {
    //   shape = GetNodeShape(ttn->fields[index]);
    // }
    return shape;
  }

  const RelayNodeInfo* node_;
};

class RelayNodeInfo : public tvm::ir::NodeInfo {
 public:
  RelayNodeInfo(const IndexedGraph<Expr>::Node* node) : node_(node) {}
  RelayNodeInfo(const RelayNodeInfo&) = default;
  std::string GetName() const override {
    Expr expr = node_->ref_;
    std::string node_name = "unknown";
    if (const CallNode* call_node = expr.as<CallNode>()) {
      if (const OpNode* op_node = call_node->op.as<OpNode>()) {
        node_name = op_node->name;
      } else {
        node_name = "call";
      }
    } else if (const OpNode* op_node = expr.as<OpNode>()) {
      node_name = "op " + op_node->name;
    } else if (expr.as<ConstantNode>()) {
      node_name = "constant";
    } else if (expr.as<VarNode>()) {
      node_name = "variable";
    } else if (expr.as<GlobalVarNode>()) {
      node_name = "global";
    } else if (expr.as<FunctionNode>()) {
      node_name = "function";
    } else if (const TupleGetItemNode* tgi = expr.as<TupleGetItemNode>()) {
      node_name = "tuple get item " + std::to_string(tgi->index);
    }
    return node_name;
  }
  std::vector<const tvm::ir::EdgeInfo*> GetInputs() const override { return inputs_; }
  std::vector<const tvm::ir::EdgeInfo*> GetOutputs() const override { return outputs_; }

  void PopulateIO(std::map<const Expr*, std::shared_ptr<RelayNodeInfo>>& node_map) {
    for (auto input : node_->inputs_) {
      if (input->ref_.as<OpNode>() || input->ref_.as<FunctionNode>()) {
        continue;
      }
      if (input->ref_->checked_type_.as<TupleTypeNode>()) {
        // TODO: do something here
      }
      RelayEdgeInfo rei(node_map[&input->ref_].get());
      input_instances.push_back(rei);
      inputs_.push_back(&input_instances[input_instances.size() - 1]);
    }
    for (auto output : node_->outputs_) {
      if (output->ref_->checked_type_.as<TupleTypeNode>()) {
        // TODO: do something here
      }
      RelayEdgeInfo rei(node_map[&output->ref_].get());
      output_instances.push_back(rei);
      outputs_.push_back(&output_instances[output_instances.size() - 1]);
    }
  }

  Expr GetExpr() const { return node_->ref_; }

 private:
  const IndexedGraph<Expr>::Node* node_;
  std::vector<const tvm::ir::EdgeInfo*> inputs_;
  std::vector<const tvm::ir::EdgeInfo*> outputs_;
  std::deque<RelayEdgeInfo> input_instances;
  std::deque<RelayEdgeInfo> output_instances;
};

std::string RelayEdgeInfo::GetType() const { return GetNodeType(node_->GetExpr(), 0); }
std::string RelayEdgeInfo::GetShape() const { return GetNodeShape(node_->GetExpr(), 0); }

class GraphVisualizer {
 public:
  explicit GraphVisualizer(IRModule module) : module_(module) {}

  Expr InferType(const Expr& expr) {
    auto mod = IRModule::FromExpr(expr);
    mod = transform::InferType()(mod);
    if (expr.as<FunctionNode>()) {
      return mod->Lookup("main");
    } else {
      return mod->Lookup("main").as<FunctionNode>()->body;
    }
  }

  void Visualize(const Expr& expr_, std::string output_path) {
    Expr expr = InferType(expr_);

    if (const FunctionNode* function = expr.as<FunctionNode>()) {
      expr = function->body;
    }

    IndexedGraph<Expr> relay_graph = CreateIndexedGraph(expr);
    std::map<const Expr*, std::shared_ptr<RelayNodeInfo>> node_map;
    for (auto relay_node : relay_graph.topological_order_) {
    }

    std::vector<tvm::ir::NodeInfo*> node_info_list;
    for (auto relay_node : relay_graph.topological_order_) {
      if (relay_node->ref_.as<OpNode>()) {
        continue;
      }
      auto rni = std::make_shared<RelayNodeInfo>(relay_node.get());
      node_map[&relay_node->ref_] = rni;
      node_info_list.push_back(rni.get());
    }
    for (auto info : node_info_list) {
      ((RelayNodeInfo*)info)->PopulateIO(node_map);
    }

    tvm::ir::VisualizeGraph(node_info_list, output_path);
  }

 private:
  // Module
  IRModule module_;
  std::stringstream m_ss;

  // Map the address of each node to a unique name
  std::map<const void*, std::string> node_name_map_;
  size_t next_id_ = 0;

  // Convert value to expression.
  Expr ObjectToExpr(const ObjectRef& value) {
    if (value->IsInstance<runtime::NDArray::ContainerType>()) {
      auto nd_array = Downcast<runtime::NDArray>(value);
      return Constant(nd_array);
    } else if (const auto* val = value.as<runtime::ADTObj>()) {
      runtime::ADT adt = GetRef<runtime::ADT>(val);
      Array<Expr> fields;
      for (size_t i = 0; i < adt.size(); ++i) {
        fields.push_back(ObjectToExpr(adt[i]));
      }
      return Tuple(fields);
    } else {
      LOG(FATAL) << "Cannot handle " << value->GetTypeKey();
      return Expr();
    }
  }

  std::string NextUniqueId(const void* op) {
    std::string name = "node_" + std::to_string(next_id_++);
    node_name_map_[op] = name;
    return name;
  }

  std::string GetNodeName(const Expr& op) {
    std::string node_name = AsText(op);
    if (const CallNode* call_node = op.as<CallNode>()) {
      if (const OpNode* op_node = call_node->op.as<OpNode>()) {
        node_name = op_node->name;
      }
    } else if (const OpNode* op_node = op.as<OpNode>()) {
      node_name = "op " + op_node->name;
    } else if (op.as<ConstantNode>()) {
      node_name = "constant";
    } else if (op.as<VarNode>()) {
      node_name = "variable";
    } else if (op.as<GlobalVarNode>()) {
      node_name = "global";
    } else if (op.as<FunctionNode>()) {
      node_name = "function";
    } else if (const TupleGetItemNode* tgi = op.as<TupleGetItemNode>()) {
      node_name = "tuple get item " + std::to_string(tgi->index);
    } else {
      std::cout << "unknown " << op << std::endl;
    }
    return node_name;
  }

  std::string GetUniqueId(const Expr& op) {
    auto it = node_name_map_.find(op.get());
    if (it == node_name_map_.end()) {
      std::cout << __FILE__ << " " << __LINE__ << " bad" << std::endl;
    }
    return it->second;
  }
};

void VisualizeGraph(const Expr& expr, const IRModule& mod, std::string output_path) {
  auto gv = GraphVisualizer(mod);
  gv.Visualize(expr, output_path);
}

namespace transform {

Pass VisualizeGraph(std::string output_path) {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        VisualizeGraph(f, m, output_path);
        return f;
      };
  return CreateFunctionPass(pass_func, 2, "VisualizeGraph", {});
}

TVM_REGISTER_GLOBAL("relay._transform.VisualizeGraph").set_body_typed(VisualizeGraph);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
