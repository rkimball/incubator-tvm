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
 * \file annotate_device_placement.cc
 * \brief Annotate Expr with on_device indicating the device_type to use per op.
 * Once the ops are annotated running the RewriteAnnotatedOps pass inserts device_copy ops
 * to copy tensors to the correct device.
 */

#include <tvm/relay/attrs/transform.h>
#include <tvm/relay/expr_functor.h>
#include <tvm/relay/transform.h>

#include "../ir/indexed_graph.h"

namespace tvm {
namespace relay {

class DeviceAnnotator : public MixedModeMutator {
 public:
  explicit DeviceAnnotator(const Expr& expr, IRModule module,
                           transform::FTVMGetDevicePlacement get_placement)
      : module_(module), get_placement_(get_placement) {
    for (std::shared_ptr<tvm::relay::IndexedGraph<tvm::relay::Expr>::Node> node :
         CreateIndexedGraph(expr).topological_order_) {
      node_map_[node->ref_] = node;
    }
  }

 private:
  IRModule module_;
  transform::FTVMGetDevicePlacement get_placement_;
  std::map<tvm::relay::Expr, std::shared_ptr<tvm::relay::IndexedGraph<tvm::relay::Expr>::Node>>
      node_map_;

  Expr Rewrite_(const CallNode* pre, const Expr& post) override {
    Expr rc = post;
    auto node = node_map_[GetRef<Expr>(pre)];
    Array<Expr> inputs;
    Array<Expr> outputs;
    for (auto i : node->inputs_) {
      inputs.push_back(i->ref_);
    }
    for (auto o : node->outputs_) {
      outputs.push_back(o->ref_);
    }

    const CallNode* call_node = post.as<CallNode>();
    if (call_node->op.as<OpNode>()) {
      // int device_type = get_placement_(GetRef<Expr>(call_node));
      int device_type = get_placement_(post, inputs, outputs);
      std::cout << __FILE__ << " " << __LINE__ << " " << device_type << std::endl;
      if (device_type > 0) {
        rc = relay::op::annotation::on_device(post, device_type);
      }
    }
    return rc;
  }

  Expr Rewrite_(const TupleGetItemNode* pre, const Expr& post) override { return post; }
  Expr Rewrite_(const TupleNode* pre, const Expr& post) override { return post; }

  using MixedModeMutator::VisitExpr_;
  Expr VisitExpr_(const FunctionNode* op) override {
    const Expr& expr = GetRef<Expr>(op);
    auto node = node_map_[expr];
    Array<Expr> inputs;
    Array<Expr> outputs;
    for (auto i : node->inputs_) {
      inputs.push_back(i->ref_);
    }
    for (auto o : node->outputs_) {
      outputs.push_back(o->ref_);
    }

    get_placement_(expr, inputs, outputs);
    return ExprMutator::VisitExpr_(op);
  }
};

Expr AnnotateDevicePlacement(const Expr& expr, const IRModule& mod,
                             transform::FTVMGetDevicePlacement get_placement) {
  return DeviceAnnotator(expr, mod, get_placement).Mutate(expr);
}

namespace transform {

Pass AnnotateDevicePlacement(FTVMGetDevicePlacement get_placement) {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        return Downcast<Function>(AnnotateDevicePlacement(f, m, get_placement));
      };
  return CreateFunctionPass(pass_func, 2, "AnnotateDevicePlacement", {});
}

TVM_REGISTER_GLOBAL("relay._transform.AnnotateDevicePlacement")
    .set_body_typed(AnnotateDevicePlacement);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
