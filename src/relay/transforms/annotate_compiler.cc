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

#include <tvm/relay/analysis.h>
#include <tvm/relay/attrs/annotation.h>
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
#include <unordered_map>

#include "../../printer/text_printer.h"
#include "../../support/utils.h"
#include "../ir/indexed_graph.h"
#include "pattern_utils.h"

namespace tvm {
namespace relay {

class CompilerAnnotator : public MixedModeMutator {
 public:
  explicit CompilerAnnotator(IRModule module, const Expr& expr,
                             transform::FTVMGetPlacement get_placement)
      : module_(module), get_placement_(get_placement), sorted_graph_(CreateIndexedGraph(expr)) {
    bool found_divider = false;
    for (std::shared_ptr<tvm::relay::IndexedGraph<tvm::relay::Expr>::Node> node :
         sorted_graph_.topological_order_) {
      if (const CallNode* call_node = node->ref_.as<CallNode>()) {
        if (const OpNode* op_node = call_node->op.as<OpNode>()) {
          if (op_node->name == "nn.relu") {
            if (node->outputs_.size() == 5) {
              found_divider = true;
            }
          }
        }
      }
      if (!found_divider) {
        backbone_.insert(node->ref_);
      }
    }
  }

 private:
  IRModule module_;
  transform::FTVMGetPlacement get_placement_;
  const IndexedGraph<Expr> sorted_graph_;
  std::set<Expr> backbone_;

  Expr Rewrite_(const CallNode* pre, const Expr& post) override {
    Expr rc = post;
    const CallNode* call_node = post.as<CallNode>();
    if (const OpNode* op_node = call_node->op.as<OpNode>()) {
      std::string placement = "default";  // get_placement_(GetRef<Expr>(pre));

      Expr expr = GetRef<Expr>(pre);
      if (backbone_.find(expr) != backbone_.end()) {
        placement = "cuda";
      }

      if (!placement.empty()) {
        Array<Expr> wrapped_args;
        for (Expr arg : call_node->args) {
          wrapped_args.push_back(MakeCompilerBegin(arg, placement));
        }

        Expr new_call = Call(call_node->op, wrapped_args, call_node->attrs, call_node->type_args);
        rc = MakeCompilerEnd(new_call, placement);
        // new_call->checked_type_ = call_node->checked_type_;
        rc = new_call;
      }
    }
    return rc;
  }

  Expr Rewrite_(const TupleNode* pre, const Expr& post) override { return post; }

  Expr Rewrite_(const TupleGetItemNode* pre, const Expr& post) override { return post; }

  // Silence warning and inform compiler we are using VisitExpr_ from base class
  using MixedModeMutator::VisitExpr_;

  Expr VisitExpr_(const VarNode* op) override {
    // Needs support
    // if (backbone_.find(expr) != backbone_.end()) {
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const ConstantNode* op) override {
    // Needs support
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const GlobalVarNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const OpNode* op) override {
    // this is called but we can ignore?
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const FunctionNode* op) override {
    // Needs support
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const LetNode* op) override {
    // Needs support
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const IfNode* op) override {
    // Needs support
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const RefCreateNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const RefReadNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const RefWriteNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const ConstructorNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }
  Expr VisitExpr_(const MatchNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return ExprMutator::VisitExpr_(op);
  }

  Expr MakeCompilerBegin(Expr expr, std::string compiler) {
    auto attrs = make_object<CompilerAttrs>();
    attrs->compiler = compiler;
    static const Op& op = Op::Get("annotation.compiler_begin");
    return Call(op, {expr}, Attrs(attrs), {});
  }

  Expr MakeCompilerEnd(Expr expr, std::string compiler) {
    auto attrs = make_object<CompilerAttrs>();
    attrs->compiler = compiler;
    static const Op& op = Op::Get("annotation.compiler_end");
    return Call(op, {expr}, Attrs(attrs), {});
  }
};

Expr AnnotateCompiler(const Expr& expr, const IRModule& mod,
                      transform::FTVMGetPlacement get_placement) {
  return CompilerAnnotator(mod, expr, get_placement).Mutate(expr);
}

namespace transform {

Pass AnnotateCompiler(FTVMGetPlacement get_placement) {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        return Downcast<Function>(AnnotateCompiler(f, m, get_placement));
      };
  return CreateFunctionPass(pass_func, 2, "AnnotateCompiler", {});
}

TVM_REGISTER_GLOBAL("relay._transform.AnnotateCompiler").set_body_typed(AnnotateCompiler);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
