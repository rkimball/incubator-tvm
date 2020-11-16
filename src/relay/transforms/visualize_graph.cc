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
 * \file hetero_annotate.cc
 */
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

#include "pattern_utils.h"

namespace tvm {
namespace relay {

class GraphVisualizer : public MixedModeVisitor {
 public:
  explicit GraphVisualizer(IRModule module) : module_(module) {}

  using MixedModeVisitor::VisitExpr_;

  void VisitExpr_(const VarNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const ConstantNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const GlobalVarNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const OpNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const TupleNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const FunctionNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const CallNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;

    for (Type ty_arg : op->type_args) {
    }

    for (Expr arg : op->args) {
      std::string name = GetNodeName(arg);
      std::cout << __FILE__ << " " << __LINE__ << " arg " << name << std::endl;
    }

    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const LetNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const IfNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const TupleGetItemNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const RefCreateNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const RefReadNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const RefWriteNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const ConstructorNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const MatchNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }

 private:
  // Module
  IRModule module_;

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

  std::string NextNodeName(const void* op) {
    std::string name = "node_" + std::to_string(next_id_++);
    node_name_map_[op] = name;
    return name;
  }

  std::string GetNodeName(const Expr& op) {
    auto it = node_name_map_.find(op.get());
    if (it == node_name_map_.end()) {
      std::cout << __FILE__ << " " << __LINE__ << " bad" << std::endl;
    }
    return it->second;
  }
};

void VisualizeGraph(const Expr& expr, const IRModule& mod) {
  auto gv = GraphVisualizer(mod);
  gv(expr);
}

namespace transform {

Pass VisualizeGraph(std::string output_path) {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        // return Downcast<Function>(VisualizeGraph(f, m));
        // run visualize
        VisualizeGraph(f, m);
        return f;
      };
  return CreateFunctionPass(pass_func, 2, "VisualizeGraph", {});
}

TVM_REGISTER_GLOBAL("relay._transform.VisualizeGraph").set_body_typed(VisualizeGraph);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
