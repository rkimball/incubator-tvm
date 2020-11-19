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

#include <fstream>

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
#include "../ir/indexed_graph.h"
#include "../../support/utils.h"
#include "../../printer/text_printer.h"

#include "pattern_utils.h"

namespace tvm {
namespace relay {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
// This code is originally from https://github.com/NervanaSystems/ngraph
//
// As we are visualizing the graph, we will make some tweaks to the generated dot file to make
// routing more tractable for Graphviz as well as (hopefully) more legible for the user.
//
// NOTE: It's possible, even likely, that better algorithms are available here. I just tried a
// few different things without doing much research, and this seemed to work well. Please feel
// free to improve on this. --amprocte
//
// -----------------
//
// The first tweak is to trim edges that, intuitively speaking, have long "skip distance". For
// example:
//
// [Actual Graph Structure]      [Visualization]
//    n0                             n0
//    | \                            |  \                            this text silences warning
//    n1 \                           n1  [to n50]
//    |   |                          |
//    n2  |                          n2
//    |   |                          |
//    n3  |                          n3
//    |   |                          |
//   ...  |                         ...  [from n0]
//    |  /                           |  /
//   n50                            n50
//
// This is useful for training graphs especially, which tend to have very long feed-forward edges
// for intermediate values from fprop being stored for later reuse in the bprop phase.
//
// Efficiently detecting a "long skip" is a bit tricky. We want to come up with a metric that is
// reasonably fast to compute, but does not result in cuts that will split the graph into multiple
// components. The heuristic we are using for the jump distance between n and m is the maximum
// difference in maximum path length from n and m to any result node that is reachable from both
// n and m (or 0, if no such result node exists). Not sure if this is mathematically *guaranteed*
// not to split graph components, but it seems to work well in practice.
//
// Formally:
//
// Compute-Heights-Above-Each-Parameter(N):
//    Inputs: nodes N; define R={n in N | n is a Result node}
//    Output: height_maps: map from N to (map from R to int)
//
//    height_maps is initially empty
//
//    for each r in R:
//        Insert into height_map the map {r -> 1}
//
//    for each n in N in reverse topological ("results-first") order:
//        for each user m of n:
//            for each r in height_maps[m].keys:
//                height_maps[n][r] := max(height_maps[n][r], height_maps[m][r]+1)
//
// Jump-Distance(n,m,height_maps):
//     Inputs: n (source node), m (destination node), height_maps (pre-computed above)
//     Output: jump_distance: int
//
//     jump_distance := 0
//
//     for each r in height_maps[n].keys:
//         if r is in height_maps[m].keys:
//             jump_distance := max(jump_distance, abs(height_maps[n][r] - height_maps[m][r]))
//
// Later on, if E is an edge from n to m, and Jump-Distance(n,m,height_map) > K (where K is kind
// of arbitrary but currently set to 20), we will "cut" the edge as illustrated above.
//
// -----------------
//
// The second tweak aims to eliminate routing pressure from nodes that have large outdegree and
// are connected to many otherwise-distant places in the graph. For this, the only thing we are
// doing at the moment is to "float" Parameter and Constant nodes. This means that rather than
// visualizing them as a single node (which might have very large outdegree as in, e.g., a
// learning rate parameter being fed to many different places), we make a "copy" of the node at
// each occurrence site (with a dashed outline).
//
// NOTE: This tweak could probably be extended to float other kinds of nodes with high out-degree.
// (This situation is likely to arise after constant subexpression elimination.) Here one has to
// be careful to avoid splitting the components. I have some rough ideas on how this could be
// dealt with, but have not had time to implement them yet. --amprocte
//
// dot file colors are defined here http://www.graphviz.org/doc/info/colors.html
//
#pragma GCC diagnostic pop

class GraphVisualizer : public MixedModeVisitor {
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

    IndexedGraph<Expr> indexed_graph = CreateIndexedGraph(expr);
    // First populate the node name map so that outputs are valid
    for (auto node : indexed_graph.topological_order_) {
      node_name_map_[node->ref_.get()] = NextUniqueId(node->ref_.get());
    }

    std::unordered_map<const void*, HeightMap> height_maps;

    auto nodes = indexed_graph.topological_order_;
    for (auto node : nodes) {
        height_maps[node.get()] = HeightMap();
    }
    auto result_node = nodes[nodes.size()-1];
    height_maps[result_node.get()] = HeightMap({result_node.get()});

    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
      const IndexedGraph<Expr>::Node& node = **it;
      for (const IndexedGraph<Expr>::Node* output : node.outputs_) {
        for (const IndexedGraph<Expr>::Node* input : output->inputs_) {
          // auto target_node = input.get_node();
          height_maps[&node.ref_].absorb(height_maps[&input->ref_]);
        }
      }
    }

    size_t fake_node_ctr = 0;
    for (auto node : indexed_graph.topological_order_) {
      if (!node->ref_.as<OpNode>()){
        add_node_arguments(*node, height_maps, fake_node_ctr);
      }
    }

    render(output_path);
  }

  using MixedModeVisitor::VisitExpr_;

  const size_t max_jump_distance = 20;

  class HeightMap {
  public:
    HeightMap() {}
    HeightMap(std::set<const void*> initials) {
      for (auto& n : initials) {
        heights_[n] = 0;
      }
    }
    void absorb(const HeightMap& other) {
      for (auto& p : other.heights_) {
        auto k = p.first;
        auto v = p.second;
        heights_[k] = std::max(heights_[k], v + 1);
      }
    }
    int64_t max_jump_to(const HeightMap& target) {
      int64_t result = 0;
      for (auto& p : heights_) {
        auto k = p.first;
        auto v = p.second;
        if (target.heights_.count(k) != 0) {
          result = std::max(result, std::abs(target.heights_.at(k) - v));
        }
      }
      return result;
    }

  private:
    std::unordered_map<const void*, int64_t> heights_;
  };

  static std::string label_edge(const IndexedGraph<Expr>::Node* source,
                                const IndexedGraph<Expr>::Node* target,
                                int64_t jump_distance) {
    std::stringstream ss;
    // for (Input<Node> input : output.get_target_inputs()) {
    //   if (input.get_node() == dst.get()) {
    //     std::stringstream label;
    //     label << "[label=\" " << output.get_index() << "-" << input.get_index() << " \"]";
    //     ss << label.str();
    //   }
    // }

    // if (getenv_bool("NGRAPH_VISUALIZE_EDGE_JUMP_DISTANCE")) {
    //   if (jump_distance > 1) {
    //     std::stringstream label;
    //     label << "[label=\"jump=" << jump_distance << "\"]";
    //     ss << label.str();
    //   }
    // }
    return ss.str();
  }

  // VisualizeTree(const string& file_name, node_modifiers_t nm, bool dot_only)
  //     : m_name{file_name}, m_node_modifiers{nm}, m_dot_only(dot_only) {}

  void add_node_arguments(const IndexedGraph<Expr>::Node& node,
                          std::unordered_map<const void*, HeightMap>& height_maps,
                          size_t& fake_node_ctr) {
    for (auto input_value : node.inputs_) {
      Expr arg = input_value->ref_;
      size_t jump_distance = height_maps[arg.get()].max_jump_to(height_maps[node.ref_.get()]);
      if (arg.as<OpNode>()) {
        // Don't render OpNode
      } else if (arg.as<ConstantNode>() || arg.as<VarNode>()) {
        auto clone_name = "CLONE_" + std::to_string(fake_node_ctr);
        auto color = (arg.as<ConstantNode>() ? "blue" : "green3");
        m_ss << "    " << clone_name << "[shape=\"box\" style=\"dashed,filled\" color=\"" << color
             << "\" fillcolor=\"white\" label=\"" << GetNodeName(arg) << "\"]\n";
        m_ss << "    " << clone_name << " -> " << GetUniqueId(node.ref_)
             << label_edge(input_value, &node, jump_distance) << "\n";
        fake_node_ctr++;
      } else if (jump_distance > max_jump_distance) {
        m_ss << add_attributes(*input_value);
        m_ss << add_attributes(node);
        auto recv_node_name = "RECV_" + std::to_string(fake_node_ctr);
        auto send_node_name = "SEND_" + std::to_string(fake_node_ctr);
        m_ss << "    " << recv_node_name
             << "[shape=\"box\" style=\"solid,filled\" "
                "fillcolor=\"#ffcccc\" label=\"Receive["
             << GetUniqueId(arg) << "]\"]\n";
        m_ss << "    " << send_node_name
             << "[shape=\"box\" style=\"solid,filled\" "
                "fillcolor=\"#ccffcc\" label=\"Send["
             <<GetUniqueId(node.ref_) << "]\"]\n";
        m_ss << "    " << GetUniqueId(arg) << " -> " << send_node_name
             << label_edge(input_value, &node, jump_distance) << "\n";
        m_ss << "    " << recv_node_name << " -> " <<GetUniqueId(node.ref_)
             << label_edge(input_value, &node, jump_distance) << "\n";
        fake_node_ctr++;
      } else {
        m_ss << add_attributes(*input_value);
        m_ss << add_attributes(node);
        m_ss << "    " << GetUniqueId(arg) << " -> " <<GetUniqueId(node.ref_)
             << label_edge(input_value, &node, jump_distance) << "\n";
      }
    }
  }

  std::string add_attributes(const IndexedGraph<Expr>::Node& node) {
    std::string rc;
    // if (m_nodes_with_attributes.find(node) == m_nodes_with_attributes.end()) {
    //   m_nodes_with_attributes.insert(node);
    //   rc = get_attributes(node);
    // }
    rc = get_attributes(node);
    return rc;
  }

  // static std::string pretty_partial_shape(const PartialShape& shape) {
  //   std::stringstream ss;

  //   if (shape.rank().is_dynamic()) {
  //     ss << "?";
  //   } else {
  //     bool first = true;

  //     ss << "[";
  //     for (size_t i = 0; i < shape.rank().get_length(); i++) {
  //       if (!first) {
  //         ss << ",";
  //       }
  //       if (shape[i].is_dynamic()) {
  //         ss << "?";
  //       } else {
  //         ss << shape[i].get_length();
  //       }
  //       first = false;
  //     }
  //     ss << "]";
  //   }

  //   return ss.str();
  // }

  std::string get_attributes(const IndexedGraph<Expr>::Node& node) {
    std::vector<std::string> attributes;
    attributes.push_back("shape=box");

    // if (node->is_output()) {
    //   attributes.push_back("color=crimson");
    //   attributes.push_back("penwidth=1.5");
    // } else {
      attributes.push_back("color=black");
    // }

    // Construct the label attribute
    std::stringstream label;

    label << "label=<<table border=\"0\" cellborder=\"0\" cellpadding=\"0\" "
            "style=\"\"><tr><td align=\"center\" colspan=\"5\">"
          << GetNodeName(node.ref_) << "</td></tr>";
    size_t table_index = 0;
    size_t tuple_index = 0;
    if (const TupleGetItemNode* tgi = node.ref_.as<TupleGetItemNode>()) {
      tuple_index = tgi->index;
    }
    const std::string td_start = "<td><font point-size=\"10\" face=\"courier\">";
    const std::string td_end = "</font></td>";
    std::vector<std::string> rows;
    std::vector<std::string> row_compare;
    for (auto input : node.inputs_) {
      if (input->ref_.as<OpNode>()) {
        continue;
      }
      std::stringstream row_ss;
      std::stringstream row_compare_ss;
      row_ss << "<tr>";
      row_ss << td_start << "I[" << table_index++ << "]" << td_end;
      row_compare_ss << td_start << GetNodeType(input->ref_, tuple_index) << td_end;
      row_compare_ss << td_start << GetNodeShape(input->ref_, tuple_index) << td_end;
      row_ss << row_compare_ss.str() << "</tr>";
      rows.push_back(row_ss.str());
      row_compare.push_back("I" + row_compare_ss.str());
    }
    table_index = 0;
    Array<Type> types;
    if (const TupleTypeNode* tuple_node = node.ref_->checked_type_.as<TupleTypeNode>()) {
      types = tuple_node->fields;
    } else {
      types.push_back(node.ref_->checked_type_);
    }
    for (Type type : types) {
      std::stringstream row_ss;
      std::stringstream row_compare_ss;
      row_ss << "<tr>";
      row_ss << td_start << "O[" << table_index++ << "]" << td_end;
      row_compare_ss << td_start << GetNodeType(type, 0) << td_end;
      row_compare_ss << td_start << GetNodeShape(type, 0) << td_end;
      row_ss << row_compare_ss.str() << "</tr>";
      rows.push_back(row_ss.str());
      row_compare.push_back("O" + row_compare_ss.str());
    }

    // Collapse duplicate rows
    std::vector<int64_t> remove_list;
    for (size_t i = 1; i < row_compare.size() - 1; i++) {
      std::string s1 = row_compare[i - 1];
      std::string s2 = row_compare[i];
      std::string s3 = row_compare[i + 1];
      if (s1 == s2 && s2 == s3) {
        remove_list.push_back(i);
      }
    }
    if (remove_list.size() > 3) {
      // Go backwards through the list to make removal easier
      int64_t start = remove_list[remove_list.size() - 1];
      int64_t end = start;
      int64_t count = 0;
      for (int64_t i = remove_list.size() - 2; i >= 0; --i) {
        int64_t row = remove_list[i];
        if (row == start - 1) {
          // continue
          start = row;
          count++;
        } else {
          rows.erase(rows.begin() + start, rows.begin() + end + 1);
          std::string str = "<tr><td align=\"center\" colspan=\"5\">...</td></tr>";
          rows.insert(rows.begin() + start, str);
          end = row;
          start = row;
        }
      }
      if (start != end) {
        rows.erase(rows.begin() + start, rows.begin() + end + 1);
        std::string str = "<tr><td align=\"center\" colspan=\"5\">...</td></tr>";
        rows.insert(rows.begin() + start, str);
      }
    }

    for (const std::string& s : rows) {
      label << s;
    }
    label << "</table>>";

    attributes.push_back(label.str());

    // if (m_node_modifiers) {
    //   m_node_modifiers(*node, attributes);
    // }

    std::stringstream ss;
    ss << "    " << GetUniqueId(node.ref_) << " [" << tvm::support::Join(attributes, " ") << "]\n";

    return ss.str();
  }

  // std::string get_node_name(std::shared_ptr<Node> node) {
  //   std::string rc = node->get_friendly_name();
  //   if (node->get_friendly_name() != node->get_name()) {
  //     rc += "\\n" + node->get_name();
  //   }
  //   return rc;
  // }

  void render(std::string output_path) const {
    // Need a real temporary here
    std::string dot_file = output_path + ".dot";
    std::string output_format = "pdf";

    std::ofstream out(dot_file);
    if (out) {
      out << "digraph ngraph\n{\n";
      out << m_ss.str();
      out << "}\n";
      out.close();

      std::stringstream ss;
      ss << "dot -T" << output_format << " " << dot_file << " -o" << output_path;
      auto cmd = ss.str();

      auto stream = tvm::support::TVMPOpen(cmd.c_str(), "r");
      if (stream) {
        tvm::support::TVMPClose(stream);
      }
    }
  }

  std::string GetNodeType(const Type& checked_type, size_t index) const {
    std::string type = "unknown type";
    if (const TensorTypeNode* tensor_type = checked_type.as<TensorTypeNode>()) {
      // tensor_type->shape;
      type = DLDataType2String(tensor_type->dtype);
    }
    else if(const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
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
    }
    else if(const TupleTypeNode* ttn = checked_type.as<TupleTypeNode>()) {
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
    std::string node_name = "unknown";
    if (const CallNode* call_node = op.as<CallNode>()){
      if (const OpNode* op_node = call_node->op.as<OpNode>()){
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
        // return Downcast<Function>(VisualizeGraph(f, m));
        // run visualize
        VisualizeGraph(f, m, output_path);
        return f;
      };
  return CreateFunctionPass(pass_func, 2, "VisualizeGraph", {});
}

TVM_REGISTER_GLOBAL("relay._transform.VisualizeGraph").set_body_typed(VisualizeGraph);

}  // namespace transform
}  // namespace relay
}  // namespace tvm
