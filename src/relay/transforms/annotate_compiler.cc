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
  explicit CompilerAnnotator(IRModule module) : module_(module) {}

  Expr InferType(const Expr& expr) {
    auto mod = IRModule::FromExpr(expr);
    mod = transform::InferType()(mod);
    if (expr.as<FunctionNode>()) {
      return mod->Lookup("main");
    } else {
      return mod->Lookup("main").as<FunctionNode>()->body;
    }
  }

  void DoAnnotation(const Expr& expr_) {
    Expr expr = InferType(expr_);

    if (const FunctionNode* function = expr.as<FunctionNode>()) {
      expr = function->body;
      // for (Var var : function->params) {
      //   placement_[var] = "default";
      // }
    }

    // for (Expr arg : expr.args) {
    //   auto it = placement_.find(arg);
    //   if (it != placement_.end()) {
    //     std::cout << __FILE__ << " " << __LINE__ << " " << it->second << std::endl;
    //   } else {
    //     std::cout << __FILE__ << " " << __LINE__ << " arg not found" << std::endl;
    //   }
    // }


    VisitExpr(expr);
  }

 private:
  IRModule module_;
  std::unordered_map<Expr, std::string, ObjectPtrHash, ObjectPtrEqual> placement_;

  // void VisitLeaf(const Expr& expr) override {
  //   std::cout << "leaf" << std::endl;
  // }

  Expr Rewrite_(const TupleNode* pre, const Expr& post) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return post;
  }

  Expr Rewrite_(const CallNode* pre, const Expr& post) override {
    if (const OpNode* op_node = pre->op.as<OpNode>()) {
      std::string node_name = op_node->name;
      std::string placement = "default";
      if (node_name == "multiply") {
        placement = "test_target";
      }
      placement_[GetRef<Expr>(pre)] = placement;
      std::cout << __FILE__ << " " << __LINE__ << " " << node_name << ", placement=" << placement << std::endl;

      // Iterate over the inputs and check their placements. If they are placed differently
      // than this node we need to insert compiler_begin and compiler_end annotation
      bool change_placement = false;
      for (Expr arg : pre->args) {
        auto it = placement_.find(arg);
        if (it != placement_.end()) {
          std::cout << __FILE__ << " " << __LINE__ << " " << it->second << std::endl;
        } else {
          std::cout << __FILE__ << " " << __LINE__ << " arg not found" << std::endl;
        }
      }
    }
    return post;
  }

  Expr Rewrite_(const TupleGetItemNode* pre, const Expr& post) override {
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    return post;
  }

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
};

void AnnotateCompiler(const Expr& expr, const IRModule& mod) {
  auto pass = CompilerAnnotator(mod);
  pass.DoAnnotation(expr);
}

namespace transform {

Pass AnnotateCompiler() {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        AnnotateCompiler(f, m);
        return f;
      };
  return CreateFunctionPass(pass_func, 2, "AnnotateCompiler", {});
}

TVM_REGISTER_GLOBAL("relay._transform.AnnotateCompiler").set_body_typed(AnnotateCompiler);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
