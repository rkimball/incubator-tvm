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

class FunctionInternalizer : public MixedModeMutator {
 public:
  explicit FunctionInternalizer(IRModule module) : module_(module) {}

 private:
  IRModule module_;

  Expr Rewrite_(const TupleNode* pre, const Expr& post) override { return post; }

  Expr Rewrite_(const CallNode* pre, const Expr& post) override {
    std::cout << __FILE__ << " " << __LINE__ << " **************** " << std::endl;
    const CallNode* post_node = post.as<CallNode>();
    if (const OpNode* op_node = post_node->op.as<OpNode>()) {
      std::cout << __FILE__ << " " << __LINE__ << " **************** " << op_node->name
                << std::endl;
    }
    if (auto* func_node = post_node->op.as<FunctionNode>()) {
      std::cout << __FILE__ << " " << __LINE__ << " **************** FunctionNode" << std::endl;
    } else {
      std::cout << __FILE__ << " " << __LINE__ << " **************** not a FunctionNode"
                << std::endl;
    }
    return post;
  }

  Expr Rewrite_(const TupleGetItemNode* pre, const Expr& post) override { return post; }
};

// Expr ExternalFunctionToInternal(const Expr& expr, const IRModule& mod) {
//     std::cout << __FILE__ << " " << __LINE__ << " ###################" << std::endl;
//   return FunctionInternalizer(mod).Mutate(expr);
// }

namespace transform {

Pass ExternalFunctionToInternal() {
  runtime::TypedPackedFunc<Function(Function, IRModule, PassContext)> pass_func =
      [=](Function f, IRModule m, PassContext pc) {
        // return Downcast<Function>(ExternalFunctionToInternal(f, m));
        return Downcast<Function>(FunctionInternalizer(m).Mutate(f));
      };
  return CreateFunctionPass(pass_func, 2, "ExternalFunctionToInternal", {});
}

TVM_REGISTER_GLOBAL("relay._transform.ExternalFunctionToInternal")
    .set_body_typed(ExternalFunctionToInternal);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
