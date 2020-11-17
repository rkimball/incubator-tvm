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
#include "../ir/indexed_graph.h"

#include "pattern_utils.h"

namespace tvm {
namespace relay {

namespace {
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
//    | \                            |  \ 
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

const int max_jump_distance = 20;

/*!
 * \brief Join a sequence of values into a string with separators.
 * \param v The collection to iterate.
 * \param sep The separator to place between each item in v.
 * \return the combined result.
 */
template <typename T>
std::string Join(const T& v, const std::string& sep = ", ")
{
    std::ostringstream ss;
    size_t count = 0;
    for (const auto& x : v)
    {
        if (count++ > 0)
        {
            ss << sep;
        }
        ss << x;
    }
    return ss.str();
}

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

// static std::string label_edge(const Output<Node>& output, const std::shared_ptr<Node>& dst,
//                               int64_t jump_distance) {
//   std::stringstream ss;
//   // for (Input<Node> input : output.get_target_inputs()) {
//   //   if (input.get_node() == dst.get()) {
//   //     std::stringstream label;
//   //     label << "[label=\" " << output.get_index() << "-" << input.get_index() << " \"]";
//   //     ss << label.str();
//   //   }
//   // }

//   // if (getenv_bool("NGRAPH_VISUALIZE_EDGE_JUMP_DISTANCE")) {
//   //   if (jump_distance > 1) {
//   //     std::stringstream label;
//   //     label << "[label=\"jump=" << jump_distance << "\"]";
//   //     ss << label.str();
//   //   }
//   // }
//   return ss.str();
// }

// bool run_on_module(std::vector<std::shared_ptr<Function>>& functions) {
//   for (std::shared_ptr<Function> f : functions) {
//     std::unordered_map<Node*, HeightMap> height_maps;

//     for (auto& node : f->get_ops()) {
//       if (is_type<op::v0::Result>(node)) {
//         height_maps[node.get()] = HeightMap({node.get()});
//       } else {
//         height_maps[node.get()] = HeightMap();
//       }
//     }

//     auto nodes = topological_sort(f->get_ops());

//     for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
//       auto& node = *it;
//       for (auto& output : node->outputs()) {
//         for (auto& input : output.get_target_inputs()) {
//           auto target_node = input.get_node();
//           height_maps[node.get()].absorb(height_maps[target_node]);
//         }
//       }
//     }

//     size_t fake_node_ctr = 0;

//     traverse_nodes(f, [&](shared_ptr<Node> node) {
//       if (auto ck = as_type_ptr<ngraph::op::v0::CompiledKernel>(node)) {
//         // print sub-graph
//         auto nodes_list = ck->get_function()->get_ordered_ops();

//         // all nodes inside the CK sub-graph
//         for (auto& ck_node : nodes_list) {
//           m_ss << add_attributes(ck_node);
//         }
//         // all edges to each node in the sub-graph
//         for (auto& subgraph_node : nodes_list) {
//           add_node_arguments(subgraph_node, height_maps, fake_node_ctr);
//         }
//       }
//       add_node_arguments(node, height_maps, fake_node_ctr);
//     });
//   }

//   render();

//   return false;
// }

// VisualizeTree(const string& file_name, node_modifiers_t nm, bool dot_only)
//     : m_name{file_name}, m_node_modifiers{nm}, m_dot_only(dot_only) {}

// void add_node_arguments(Expr node,
//                         std::unordered_map<const void*, HeightMap>& height_maps,
//                         size_t& fake_node_ctr) {
//   for (auto input_value : node->input_values()) {
//     auto arg = input_value.get_node_shared_ptr();
//     size_t jump_distance = height_maps[arg.get()].max_jump_to(height_maps[node.get()]);
//     if (is_type<ngraph::op::v0::Constant>(arg) || is_type<ngraph::op::v0::Parameter>(arg)) {
//       auto clone_name = "CLONE_" + std::to_string(fake_node_ctr);
//       auto color = (is_type<op::v0::Parameter>(arg) ? "blue" : "black");
//       m_ss << "    " << clone_name << "[shape=\"box\" style=\"dashed,filled\" color=\"" << color
//            << "\" fillcolor=\"white\" label=\"" << get_node_name(arg) << "\"]\n";
//       m_ss << "    " << clone_name << " -> " << node->get_name()
//            << label_edge(input_value, node, jump_distance) << "\n";
//       fake_node_ctr++;
//     } else if (jump_distance > max_jump_distance) {
//       m_ss << add_attributes(arg);
//       m_ss << add_attributes(node);
//       auto recv_node_name = "RECV_" + std::to_string(fake_node_ctr);
//       auto send_node_name = "SEND_" + std::to_string(fake_node_ctr);
//       m_ss << "    " << recv_node_name
//            << "[shape=\"box\" style=\"solid,filled\" "
//               "fillcolor=\"#ffcccc\" label=\"Receive["
//            << arg->get_name() << "]\"]\n";
//       m_ss << "    " << send_node_name
//            << "[shape=\"box\" style=\"solid,filled\" "
//               "fillcolor=\"#ccffcc\" label=\"Send["
//            << node->get_name() << "]\"]\n";
//       m_ss << "    " << arg->get_name() << " -> " << send_node_name
//            << label_edge(input_value, node, jump_distance) << "\n";
//       m_ss << "    " << recv_node_name << " -> " << node->get_name()
//            << label_edge(input_value, node, jump_distance) << "\n";
//       fake_node_ctr++;
//     } else {
//       m_ss << add_attributes(arg);
//       m_ss << add_attributes(node);
//       m_ss << "    " << arg->get_name() << " -> " << node->get_name()
//            << label_edge(input_value, node, jump_distance) << "\n";
//     }
//   }
// }

std::string add_attributes(Expr node) {
  std::string rc;
  // if (m_nodes_with_attributes.find(node) == m_nodes_with_attributes.end()) {
  //   m_nodes_with_attributes.insert(node);
  //   rc = get_attributes(node);
  // }
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

std::string GetName(Expr op) {
  return "some node";
}

// std::string get_attributes(Expr node) {
//   std::vector<std::string> attributes;
//   attributes.push_back("shape=box");

//   // if (node->is_output()) {
//   //   attributes.push_back("color=crimson");
//   //   attributes.push_back("penwidth=1.5");
//   // } else {
//     attributes.push_back("color=black");
//   // }

//   // Construct the label attribute
//   {
//     std::stringstream label;
//     label << "label=<<table border=\"0\" cellborder=\"0\" cellpadding=\"0\" "
//              "style=\"\"><tr><td align=\"center\" colspan=\"5\">"
//           << GetName(node) << "</td></tr>";

//     size_t index = 0;
//     const std::string td_start = "<td><font point-size=\"10\" face=\"courier\">";
//     const std::string td_end = "</font></td>";
//     // std::vector<std::string> rows;
//     // std::vector<std::string> row_compare;
//     // for (auto input : node->inputs()) {
//     //   std::stringstream row_ss;
//     //   std::stringstream row_compare_ss;
//     //   row_ss << "<tr>";
//     //   row_ss << td_start << "I[" << index++ << "]" << td_end;
//     //   row_compare_ss << td_start << input.get_element_type().get_type_name() << td_end;
//     //   row_compare_ss << td_start << pretty_partial_shape(input.get_shape()) << td_end;
//     //   row_ss << row_compare_ss.str() << "</tr>";
//     //   rows.push_back(row_ss.str());
//     //   row_compare.push_back("I" + row_compare_ss.str());
//     // }
//     // index = 0;
//     // for (auto output : node->outputs()) {
//     //   std::stringstream row_ss;
//     //   std::stringstream row_compare_ss;
//     //   row_ss << "<tr>";
//     //   row_ss << td_start << "O[" << index++ << "]" << td_end;
//     //   row_compare_ss << td_start << output.get_element_type().get_type_name() << td_end;
//     //   row_compare_ss << td_start << pretty_partial_shape(output.get_shape()) << td_end;
//     //   row_ss << row_compare_ss.str() << "</tr>";
//     //   rows.push_back(row_ss.str());
//     //   row_compare.push_back("O" + row_compare_ss.str());
//     // }

//     // // Collapse duplicate rows
//     // std::vector<int64_t> remove_list;
//     // for (size_t i = 1; i < row_compare.size() - 1; i++) {
//     //   std::string s1 = row_compare[i - 1];
//     //   std::string s2 = row_compare[i];
//     //   std::string s3 = row_compare[i + 1];
//     //   if (s1 == s2 && s2 == s3) {
//     //     remove_list.push_back(i);
//     //   }
//     // }
//     // if (remove_list.size() > 3) {
//     //   // Go backwards through the list to make removal easier
//     //   int64_t start = remove_list[remove_list.size() - 1];
//     //   int64_t end = start;
//     //   int64_t count = 0;
//     //   for (int64_t i = remove_list.size() - 2; i >= 0; --i) {
//     //     int64_t row = remove_list[i];
//     //     if (row == start - 1) {
//     //       // continue
//     //       start = row;
//     //       count++;
//     //     } else {
//     //       rows.erase(rows.begin() + start, rows.begin() + end + 1);
//     //       std::string str = "<tr><td align=\"center\" colspan=\"5\">...</td></tr>";
//     //       rows.insert(rows.begin() + start, str);
//     //       end = row;
//     //       start = row;
//     //     }
//     //   }
//     //   if (start != end) {
//     //     rows.erase(rows.begin() + start, rows.begin() + end + 1);
//     //     std::string str = "<tr><td align=\"center\" colspan=\"5\">...</td></tr>";
//     //     rows.insert(rows.begin() + start, str);
//     //   }
//     // }

//     // if (get_provenance_enabled())
//     // {
//     //     for (auto tag : node->get_provenance_tags())
//     //     {
//     //         std::string str = "<tr><td align=\"left\" colspan=\"5\">tag=" + tag + "</td></tr>";
//     //         rows.push_back(str);
//     //     }
//     // }

//     // for (const std::string& s : rows) {
//     //   label << s;
//     // }

//     label << "</table>>";
//     attributes.push_back(label.str());
//   }

//   // if (m_node_modifiers) {
//   //   m_node_modifiers(*node, attributes);
//   // }

//   std::stringstream ss;
//   ss << "    " << GetName(node) << " [" << Join(attributes, " ") << "]\n";

//   return ss.str();
// }

// std::string get_node_name(std::shared_ptr<Node> node) {
//   std::string rc = node->get_friendly_name();
//   if (node->get_friendly_name() != node->get_name()) {
//     rc += "\\n" + node->get_name();
//   }
//   return rc;
// }

// void render() const {
//   std::string ext = file_util::get_file_ext(m_name);
//   std::string output_format = ext.substr(1);
//   std::string dot_file = m_name;
//   if (to_lower(ext) != ".dot") {
//     dot_file += ".dot";
//   }
//   std::ofstream out(dot_file);
//   if (out) {
//     out << "digraph ngraph\n{\n";
//     out << m_ss.str();
//     out << "}\n";
//     out.close();

//     if (!m_dot_only && to_lower(ext) != ".dot") {
// #ifndef _WIN32
//       std::stringstream ss;
//       ss << "dot -T" << output_format << " " << dot_file << " -o" << m_name;
//       auto cmd = ss.str();
//       auto stream = popen(cmd.c_str(), "r");
//       if (stream) {
//         pclose(stream);
//       }
// #endif
//     }
//   }
// }
}  // namespace

class GraphVisualizer : public MixedModeVisitor {
 public:
  explicit GraphVisualizer(IRModule module) : module_(module) {}

  void Visualize(const Expr& expr) {
    std::cout << __FILE__ << "   " << __LINE__ << " start\n";
    IndexedGraph<Expr> indexed_graph = CreateIndexedGraph(expr);
    // operator()(expr);
    std::cout << __FILE__ << "   " << __LINE__ << "\n";
    // First populate the node name map so that outputs are valid
    for (auto node : indexed_graph.topological_order_) {
      node_name_map_[node->ref_.get()] = NextNodeName(node->ref_.get());
    }
    for (auto node : indexed_graph.topological_order_) {
      if (auto call = node->ref_.as<CallNode>()) {
        if (const OpNode* op = call->op.as<OpNode>()){
          std::cout << __FILE__ << " " << __LINE__ << " " << GetNodeName(node->ref_) << " " << op->name << std::endl;
          for (auto input : node->inputs_){
            std::cout << "input " << GetNodeName(input->ref_) << std::endl;
          }
          for (auto output : node->outputs_){
            std::cout << "output " << GetNodeName(output->ref_) << std::endl;
          }
        }
      }
    }
    std::cout << __FILE__ << "   " << __LINE__ << " end\n";
  }

  using MixedModeVisitor::VisitExpr_;

  void VisitExpr_(const VarNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const ConstantNode* op) override {
    // std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const GlobalVarNode* op) override {
    std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
    ExprVisitor::VisitExpr_(op);
  }
  void VisitExpr_(const OpNode* op) override {
    // std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << std::endl;
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
  void VisitExpr_(const CallNode* call) override {
    // if (const OpNode* op = call->op.as<OpNode>()){
    //   std::cout << __FILE__ << " " << __LINE__ << " " << op->name << std::endl;
    // }

    // std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(call) << std::endl;

    // for (Type ty_arg : call->type_args) {
    // }

    for (Expr arg : call->args) {
      node_inputs_[call].push_back(arg.get());
      // std::string name = GetNodeName(arg);
      // std::cout << __FILE__ << " " << __LINE__ << " arg " << name << std::endl;
    }

    ExprVisitor::VisitExpr_(call);
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
    // std::cout << __FILE__ << " " << __LINE__ << " " << NextNodeName(op) << " " << op->index << std::endl;
    // std::cout << __FILE__ << " " << __LINE__ << " " << GetNodeName(op->tuple) << " " << op->index << std::endl;
    std::cout << __FILE__ << " " << __LINE__ << " " << std::endl;
    if (auto tuple = op->tuple.as<TupleNode>()) {
      std::cout << __FILE__ << " " << __LINE__ << " is a tuple node" << std::endl;

    }
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
  std::map<const void*, std::vector<const void*>> node_inputs_;
  std::map<const void*, std::vector<const void*>> node_outputs_;
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
  gv.Visualize(expr);
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
