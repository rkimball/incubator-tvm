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

#include <fstream>

#include "../printer/text_printer.h"
#include "../support/utils.h"

namespace tvm {
namespace ir {

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

class GraphVisualizer {
 public:
  void Visualize(const std::vector<NodeInfo*>& node_info, std::string output_path) {
    std::unordered_map<const NodeInfo*, HeightMap> height_maps;

    for (const NodeInfo* node : node_info) {
      height_maps[node] = HeightMap();
    }
    const NodeInfo* result_node = node_info[node_info.size() - 1];
    height_maps[result_node] = HeightMap({result_node});

    for (auto it = node_info.rbegin(); it != node_info.rend(); ++it) {
      // const NodeInfo* node = *it;
      // for (const IndexedGraph<Expr>::Node* output : node.outputs_) {
      //   for (const IndexedGraph<Expr>::Node* input : output->inputs_) {
      //     // auto target_node = input.get_node();
      //     height_maps[&node.ref_].absorb(height_maps[&input->ref_]);
      //   }
      // }
    }

    size_t fake_node_ctr = 0;
    for (const NodeInfo* node : node_info) {
      add_node_arguments(node, height_maps, fake_node_ctr);
    }

    render(output_path);
  }

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

  static std::string label_edge(const EdgeInfo* edge, int64_t jump_distance) {
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

  void add_node_arguments(const NodeInfo* node,
                          std::unordered_map<const NodeInfo*, HeightMap>& height_maps,
                          size_t& fake_node_ctr) {
    for (const EdgeInfo* input_value : node->GetInputs()) {
      CHECK(input_value);
      const NodeInfo* arg = input_value->GetInfo();
      size_t jump_distance = height_maps[input_value->GetInfo()].max_jump_to(height_maps[node]);
      if (arg->GetName() == "constant" || arg->GetName() == "var") {
        auto clone_name = "CLONE_" + std::to_string(fake_node_ctr);
        auto color = (arg->GetName() == "constant" ? "blue" : "green3");
        m_ss << "    " << clone_name << "[shape=\"box\" style=\"dashed,filled\" color=\"" << color
             << "\" fillcolor=\"white\" label=\"" << arg->GetName() << "\"]\n";
        m_ss << "    " << clone_name << " -> " << node->GetUniqueName()
             << label_edge(input_value, jump_distance) << "\n";
        fake_node_ctr++;
      } else if (jump_distance > max_jump_distance) {
        m_ss << add_attributes(input_value->GetInfo());
        m_ss << add_attributes(node);
        auto recv_node_name = "RECV_" + std::to_string(fake_node_ctr);
        auto send_node_name = "SEND_" + std::to_string(fake_node_ctr);
        m_ss << "    " << recv_node_name
             << "[shape=\"box\" style=\"solid,filled\" "
                "fillcolor=\"#ffcccc\" label=\"Receive["
             << arg->GetUniqueName() << "]\"]\n";
        m_ss << "    " << send_node_name
             << "[shape=\"box\" style=\"solid,filled\" "
                "fillcolor=\"#ccffcc\" label=\"Send["
             << node->GetUniqueName() << "]\"]\n";
        m_ss << "    " << arg->GetUniqueName() << " -> " << send_node_name
             << label_edge(input_value, jump_distance) << "\n";
        m_ss << "    " << recv_node_name << " -> " << node->GetUniqueName()
             << label_edge(input_value, jump_distance) << "\n";
        fake_node_ctr++;
      } else {
        m_ss << add_attributes(input_value->GetInfo());
        m_ss << add_attributes(node);
        m_ss << "    " << arg->GetUniqueName() << " -> " << node->GetUniqueName()
             << label_edge(input_value, jump_distance) << "\n";
      }
    }
  }

  std::string add_attributes(const NodeInfo* node) {
    std::string rc;
    // if (m_nodes_with_attributes.find(node) == m_nodes_with_attributes.end()) {
    //   m_nodes_with_attributes.insert(node);
    //   rc = get_attributes(node);
    // }
    rc = get_attributes(node);
    return rc;
  }

  std::string get_attributes(const NodeInfo* node) {
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
          << node->GetName() << "</td></tr>";
    size_t table_index = 0;
    // size_t tuple_index = 0;
    // if (const TupleGetItemNode* tgi = node->ref_.as<TupleGetItemNode>()) {
    //   tuple_index = tgi->index;
    // }
    const std::string td_start = "<td><font point-size=\"10\" face=\"courier\">";
    const std::string td_end = "</font></td>";
    std::vector<std::string> rows;
    std::vector<std::string> row_compare;
    for (const EdgeInfo* input : node->GetInputs()) {
      CHECK(input);
      std::stringstream row_ss;
      std::stringstream row_compare_ss;
      row_ss << "<tr>";
      row_ss << td_start << "I[" << table_index++ << "]" << td_end;
      row_compare_ss << td_start << input->GetType() << td_end;
      row_compare_ss << td_start << input->GetShape() << td_end;
      row_ss << row_compare_ss.str() << "</tr>";
      rows.push_back(row_ss.str());
      row_compare.push_back("I" + row_compare_ss.str());
    }
    table_index = 0;
    // Array<Type> types;
    // if (const TupleTypeNode* tuple_node = node->ref_->checked_type_.as<TupleTypeNode>()) {
    //   types = tuple_node->fields;
    // } else {
    //   types.push_back(node->ref_->checked_type_);
    // }
    for (const EdgeInfo* output : node->GetOutputs()) {
      std::stringstream row_ss;
      std::stringstream row_compare_ss;
      row_ss << "<tr>";
      row_ss << td_start << "O[" << table_index++ << "]" << td_end;
      row_compare_ss << td_start << output->GetType() << td_end;
      row_compare_ss << td_start << output->GetShape() << td_end;
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
    ss << "    " << node->GetUniqueName() << " [" << tvm::support::Join(attributes, " ") << "]\n";

    return ss.str();
  }

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
    out.close();
    remove(dot_file.c_str());
  }

 private:
  std::stringstream m_ss;
};

void VisualizeGraph(const std::vector<NodeInfo*>& node_info, std::string output_path) {
  GraphVisualizer().Visualize(node_info, output_path);
}

}  // namespace ir
}  // namespace tvm
