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

#include <tvm/arith/analyzer.h>
#include <tvm/runtime/container.h>
#include <tvm/runtime/device_api.h>
#include <tvm/runtime/ndarray.h>
#include <tvm/runtime/object.h>
#include <tvm/runtime/registry.h>
#include <tvm/target/target_info.h>
#include <tvm/te/operation.h>
#include <tvm/tir/buffer.h>
#include <tvm/tir/builtin.h>
#include <tvm/tir/expr.h>
#include <tvm/tir/op.h>
#include <tvm/tir/stmt.h>
#include <tvm/tir/stmt_functor.h>
#include <tvm/tir/transform.h>

#include <fstream>

#include "../../printer/text_printer.h"
#include "../../relay/ir/indexed_graph.h"
#include "../../support/utils.h"

// #include "pattern_utils.h"

namespace tvm {
namespace tir {



tvm::relay::IndexedGraph<PrimExpr> CreateIndexedGraph(const PrimExpr& expr) {
  using NodePtr = std::shared_ptr<relay::IndexedGraph<PrimExpr>::Node>;
  /*! \brief Creator Creates an IndexedGraph and determintes Topological order */
  class Creator : public tvm::tir::ExprVisitor {
   public:
    relay::IndexedGraph<PrimExpr> CreateGraph(const PrimExpr& expr) {
      VisitExpr(expr);
      graph_.node_map_[expr]->is_external_ = true;
      return std::move(graph_);
    }

   protected:
    void VisitExpr(const PrimExpr& expr) override {
      ExprVisitor::VisitExpr(expr);
      auto node = std::make_shared<relay::IndexedGraph<PrimExpr>::Node>(expr, index_++);
      graph_.node_map_[expr] = node;
      graph_.topological_order_.push_back(node);
    }
    relay::IndexedGraph<PrimExpr> graph_;
    size_t index_ = 0;
  };
  // /*! \brief Annotator takes an IndexedGraph, fills it's forward outputs, and does dominator tree
  //  * analysis.
  //  *
  //  *  Annotator use ExprFunctor to visit nodes, but iterates over them in pre-determined
  //  * topological order instead of recursing.
  //  */
  // class Annotator : public ExprFunctor<void(const Expr&, NodePtr)> {
  //  public:
  //   Annotator(const relay::IndexedGraph<Expr>& graph) : graph_(graph) {}
  //   relay::IndexedGraph<Expr> Annotate() {
  //     // Visit all of the nodes in topological order to get forward outputs
  //     for (const auto& node : graph_.topological_order_) {
  //       ExprFunctor::VisitExpr(node->ref_, nullptr);
  //     }
  //     // do the dominator analysis
  //     graph_.PostDom();
  //     return std::move(graph_);
  //   }

  //   /*! Default visitation pushes the parent to the child's ouputs and the child to the parent's
  //    * inputs*/
  //   void VisitExpr(const Expr& expr, NodePtr parent) override {
  //     auto current = graph_.node_map_[expr];
  //     if (parent) {
  //       current->outputs_.push_back(parent.get());
  //       parent->inputs_.push_back(current.get());
  //     }
  //   }

  //  protected:
  //   relay::IndexedGraph<Expr> graph_;
  //   void VisitExpr_(const VarNode* op, NodePtr parent) override {
  //     if (op->type_annotation.defined()) {
  //       this->VisitType(op->type_annotation);
  //     }
  //   }

  //   void VisitExpr_(const GlobalVarNode* op, NodePtr parent) override {}

  //   void VisitExpr_(const ConstantNode* op, NodePtr parent) override {}

  //   void VisitExpr_(const TupleNode* op, NodePtr parent) override {
  //     for (auto field : op->fields) {
  //       this->VisitExpr(field, graph_.node_map_[GetRef<Expr>(op)]);
  //     }
  //   }

  //   void VisitExpr_(const FunctionNode* op, NodePtr parent) override {
  //     for (auto param : op->params) {
  //       this->VisitExpr(param, graph_.node_map_[GetRef<Expr>(op)]);
  //     }

  //     this->VisitExpr(op->body, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const CallNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->op, graph_.node_map_[GetRef<Expr>(op)]);

  //     for (auto ty_arg : op->type_args) {
  //       this->VisitType(ty_arg);
  //     }

  //     for (auto arg : op->args) {
  //       this->VisitExpr(arg, graph_.node_map_[GetRef<Expr>(op)]);
  //     }
  //   }

  //   void VisitExpr_(const LetNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->value, graph_.node_map_[GetRef<Expr>(op)]);
  //     this->VisitExpr(op->var, graph_.node_map_[GetRef<Expr>(op)]);
  //     this->VisitExpr(op->body, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const IfNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->cond, graph_.node_map_[GetRef<Expr>(op)]);
  //     this->VisitExpr(op->true_branch, graph_.node_map_[GetRef<Expr>(op)]);
  //     this->VisitExpr(op->false_branch, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const OpNode* op, NodePtr parent) override { return; }

  //   void VisitExpr_(const TupleGetItemNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->tuple, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const RefCreateNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->value, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const RefReadNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->ref, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const RefWriteNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->ref, graph_.node_map_[GetRef<Expr>(op)]);
  //     this->VisitExpr(op->value, graph_.node_map_[GetRef<Expr>(op)]);
  //   }

  //   void VisitExpr_(const ConstructorNode* op, NodePtr parent) override {
  //     for (const Type& t : op->inputs) {
  //       this->VisitType(t);
  //     }
  //     this->VisitType(op->belong_to);
  //   }

  //   void VisitExpr_(const MatchNode* op, NodePtr parent) override {
  //     this->VisitExpr(op->data, graph_.node_map_[GetRef<Expr>(op)]);
  //     for (const Clause& c : op->clauses) {
  //       this->VisitClause(c, graph_.node_map_[GetRef<Expr>(op)]);
  //     }
  //   }

  //   void VisitClause(const Clause& op, NodePtr parent) {
  //     this->VisitPattern(op->lhs);
  //     this->VisitExpr(op->rhs, parent);
  //   }

  //   void VisitPattern(const Pattern& p) { return; }

  //   void VisitType(const Type& t) { return; }
  // };
  // return Annotator(Creator().CreateGraph(expr)).Annotate();
  return Creator().CreateGraph(expr);
}
















class TIRVisualizer {
 public:
  explicit TIRVisualizer(IRModule module) : module_(module) {}

  void Visualize(const PrimFunc& expr, std::string output_path) {

std::cout << __FILE__ << " " << __LINE__ << std::endl;
//     relay::IndexedGraph<PrimExpr> indexed_graph = CreateIndexedGraph(expr);
//     // First populate the node name map so that outputs are valid
//     for (auto node : indexed_graph.topological_order_) {
//       node_name_map_[node->ref_.get()] = NextUniqueId(node->ref_.get());
//     }

//     std::unordered_map<const void*, HeightMap> height_maps;

//     auto nodes = indexed_graph.topological_order_;
//     for (auto node : nodes) {
//         height_maps[node.get()] = HeightMap();
//     }
//     auto result_node = nodes[nodes.size()-1];
//     height_maps[result_node.get()] = HeightMap({result_node.get()});

//     for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
//       const IndexedGraph<PrimExpr>::Node& node = **it;
//       for (const IndexedGraph<PrimExpr>::Node* output : node.outputs_) {
//         for (const IndexedGraph<PrimExpr>::Node* input : output->inputs_) {
//           // auto target_node = input.get_node();
//           height_maps[&node.ref_].absorb(height_maps[&input->ref_]);
//         }
//       }
//     }

//     size_t fake_node_ctr = 0;
//     for (auto node : indexed_graph.topological_order_) {
//       if (!node->ref_.as<OpNode>()){
//         add_node_arguments(*node, height_maps, fake_node_ctr);
//       }
//     }

//     render(output_path);
  }

//   std::string GetNodeType(const Type& checked_type, size_t index) const {
//     std::string type = "unknown type";
//     if (const TensorTypeNode* tensor_type = checked_type.as<TensorTypeNode>()) {
//       // tensor_type->shape;
//       type = DLDataType2String(tensor_type->dtype);
//     }
//     else if(const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
//       type = GetNodeType(ttn->fields[index], 0);
//     }
//     return type;
//   }

//   std::string GetNodeShape(const Type& checked_type, size_t index) const {
//     std::string shape = "unknown shape";
//     if (const TensorTypeNode* tensor_type = checked_type.as<TensorTypeNode>()) {
//       std::vector<std::string> axes;
//       for (auto e : tensor_type->shape) {
//         axes.push_back(tvm::TextPrinter(false, nullptr).PrintFinal(e).str());
//       }
//       shape = "[" + tvm::support::Join(axes, ",") + "]";
//     }
//     else if(const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
//       shape = GetNodeShape(ttn->fields[index], 0);
//     }
//     return shape;
//   }

//   std::string GetNodeType(const PrimExpr& expr, size_t index) const {
//     std::string type = "unknown type";
//     if (const RelayExprNode* rexpr = expr.as<RelayExprNode>()) {
//       type = GetNodeType(rexpr->checked_type_, index);
//     }
//     // else if(const TupleTypeNode* ttn = expr.as<TupleTypeNode>()) {
//     //   type = GetNodeType(ttn->fields[index]);
//     // }
//     return type;
//   }

//   std::string GetNodeShape(const PrimExpr& expr, size_t index) const {
//     std::string shape = "unknown shape";
//     if (const RelayExprNode* rexpr = expr.as<RelayExprNode>()) {
//       shape = GetNodeShape(rexpr->checked_type_, index);
//     }
//     // else if(const TupleTypeNode* ttn = expr.as<TupleTypeNode>()) {
//     //   shape = GetNodeShape(ttn->fields[index]);
//     // }
//     return shape;
//   }

 private:
  // Module
  IRModule module_;
  std::stringstream m_ss;

//   // Map the address of each node to a unique name
//   std::map<const void*, std::string> node_name_map_;
//   size_t next_id_ = 0;

//   // Convert value to expression.
//   PrimExpr ObjectToExpr(const ObjectRef& value) {
//     if (value->IsInstance<runtime::NDArray::ContainerType>()) {
//       auto nd_array = Downcast<runtime::NDArray>(value);
//       return Constant(nd_array);
//     } else if (const auto* val = value.as<runtime::ADTObj>()) {
//       runtime::ADT adt = GetRef<runtime::ADT>(val);
//       Array<PrimExpr> fields;
//       for (size_t i = 0; i < adt.size(); ++i) {
//         fields.push_back(ObjectToExpr(adt[i]));
//       }
//       return Tuple(fields);
//     } else {
//       LOG(FATAL) << "Cannot handle " << value->GetTypeKey();
//       return PrimExpr();
//     }
//   }

//   std::string NextUniqueId(const void* op) {
//     std::string name = "node_" + std::to_string(next_id_++);
//     node_name_map_[op] = name;
//     return name;
//   }

//   std::string GetNodeName(const PrimExpr& op) {
//     std::string node_name = "unknown";
//     if (const CallNode* call_node = op.as<CallNode>()){
//       if (const OpNode* op_node = call_node->op.as<OpNode>()){
//         node_name = op_node->name;
//       }
//     } else if (const OpNode* op_node = op.as<OpNode>()) {
//       node_name = "op " + op_node->name;
//     } else if (op.as<ConstantNode>()) {
//       node_name = "constant";
//     } else if (op.as<VarNode>()) {
//       node_name = "variable";
//     } else if (op.as<GlobalVarNode>()) {
//       node_name = "global";
//     } else if (op.as<FunctionNode>()) {
//       node_name = "function";
//     } else if (const TupleGetItemNode* tgi = op.as<TupleGetItemNode>()) {
//       node_name = "tuple get item " + std::to_string(tgi->index);
//     } else {
//       std::cout << "unknown " << op << std::endl;
//     }
//     return node_name;
//   }

//   std::string GetUniqueId(const PrimExpr& op) {
//     auto it = node_name_map_.find(op.get());
//     if (it == node_name_map_.end()) {
//       std::cout << __FILE__ << " " << __LINE__ << " bad" << std::endl;
//     }
//     return it->second;
//   }
};

namespace transform {

Pass VisualizeGraph(std::string output_path) {
  auto pass_func = [=](PrimFunc f, IRModule m, PassContext pc) {
    auto gv = TIRVisualizer(m);
    gv.Visualize(f, output_path);
    return f;
  };
  return CreatePrimFuncPass(pass_func, 2, "tir.VisualizeGraph", {});
}

TVM_REGISTER_GLOBAL("tir.transform.VisualizeGraph").set_body_typed(VisualizeGraph);

}  // namespace transform
}  // namespace tir
}  // namespace tvm
