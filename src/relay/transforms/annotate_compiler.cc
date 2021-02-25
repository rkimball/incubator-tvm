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
  explicit CompilerAnnotator(IRModule module, const Expr& expr, transform::FTVMGetPlacement get_placement)
      : module_(module), get_placement_(get_placement), sorted_graph_(CreateIndexedGraph(expr)) {
        std::cout << __FILE__ << " " << __LINE__ << std::endl;

        bool found_divider = false;
        for (std::shared_ptr<tvm::relay::IndexedGraph<tvm::relay::Expr>::Node> node : sorted_graph_.topological_order_) {
          if (const CallNode* call_node = node->ref_.as<CallNode>()) {
            if (const OpNode* op_node = call_node->op.as<OpNode>()) {
              if (op_node->name == "nn.relu") {
                if (node->outputs_.size() == 5) {
                  found_divider = true;
                }
                if (!found_divider) {
                  backbone_.push_back(call_node);
                }
                std::cout << __FILE__ << " " << __LINE__ << " " << op_node->name << " " << node->outputs_.size() << std::endl;
              }
            }
          }
        }
      }

  Expr InferType(const Expr& expr) {
    auto mod = IRModule::FromExpr(expr);
    mod = transform::InferType()(mod);
    if (expr.as<FunctionNode>()) {
      return mod->Lookup("main");
    } else {
      return mod->Lookup("main").as<FunctionNode>()->body;
    }
  }

 private:
  IRModule module_;
  transform::FTVMGetPlacement get_placement_;
  const IndexedGraph<Expr> sorted_graph_;
  std::vector<const CallNode*> backbone_;

  Expr Rewrite_(const TupleNode* pre, const Expr& post) override { return post; }

  Expr Rewrite_(const CallNode* pre, const Expr& post) override {
    Expr rc = post;
    const CallNode* call_node = post.as<CallNode>();
    if (const OpNode* op_node = call_node->op.as<OpNode>()) {
      std::string placement = get_placement_(GetRef<Expr>(call_node));

      Array<Expr> wrapped_args;
      for (Expr arg : call_node->args) {
        wrapped_args.push_back(MakeCompilerBegin(arg, placement));
      }

      Expr new_call =
          MakeCompilerEnd(Call(call_node->op, wrapped_args, call_node->attrs), placement);
      new_call->checked_type_ = call_node->checked_type_;
      rc = new_call;
    }
    return rc;
  }

  Expr Rewrite_(const TupleGetItemNode* pre, const Expr& post) override { return post; }

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
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
  return CompilerAnnotator(mod, expr, get_placement).Mutate(expr);
}

namespace transform {

Pass AnnotateCompiler(FTVMGetPlacement get_placement) {
  std::cout << __FILE__ << " " << __LINE__ << std::endl;
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
