#include "export.h"

#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "cpu/format.pb.h"
#include "cpu/gate_lib.h"
#include "cpu/graph.h"
#include "cpu/transistor_lib.h"

std::string PrintNetwork(GateNetwork& net, int indent) {
  auto name = [](int index) {
    std::string result;
    do {
      result.insert(result.begin(), 'a' + (index % 26));
      index = index / 26 - 1;
    } while (index >= 0);
    return result;
  };

  graph::TopoSort sort(net, net.all_inputs(), {net.sink()});
  std::vector<GateTerminal> outputs;
  for (auto output : net.GetOutputs()) {
    for (int i = 0; i < output.bitwidth(); ++i) {
      outputs.push_back(output[i]);
    }
  }

  std::map<GateTerminal, std::string> exprs{{kLowGate, "0"}, {kHighGate, "1"}};
  std::vector<std::string> inputs;
  for (int i = 0; i < net.total_input_bits(); ++i) {
    inputs.push_back(name(i));
    exprs[net.input_bit(i)] = inputs.back();
  }
  std::vector<std::string> lines{
      absl::StrCat(absl::StrJoin(inputs, ", "), " ->")};
  int next_name = inputs.size();
  for (auto t : sort.order(false, false)) {
    auto& gate = *t.first;
    if (gate.kind() == GateKind::kNot) {
      exprs[t] = absl::StrCat("~", exprs.at(gate.input(0)));
    } else {
      std::string args = absl::StrJoin(
          gate.inputs(), ", ", [&](std::string* out, GateTerminal input) {
            absl::StrAppend(out, exprs.at(input));
          });
      exprs[t] = absl::StrCat(to_string(gate), "(", args, ")");
    }
    if (net.GetUsers(t).size() != 1) {
      std::string n = name(next_name++);
      lines.push_back(absl::StrCat("let ", n, " = ", exprs[t], " in"));
      exprs[t] = n;
    }
  }
  if (outputs.size() == 1) {
    lines.push_back(exprs.at(outputs[0]));
  } else {
    std::string outs = absl::StrJoin(
        outputs, ", ", [&](std::string* out, GateTerminal output) {
          absl::StrAppend(out, exprs.at(output));
        });
    lines.push_back(absl::StrCat("(", outs, ")"));
  }
  return absl::StrJoin(lines, "\n" + std::string(indent, ' '));
}

std::string label(NodeId id) {
  if (id == kVdd) return "1";
  if (id == kVss) return "0";
  if (id == kClk) return "clk";

  if (id.is_input()) {
    return absl::StrCat("i", id.input_index());
  }

  TransistorId tid(id);
  return "t" + std::to_string(tid.drain().id) +
         (id == tid.drain()       ? "d"
          : id == tid.gate() == 1 ? "g"
                                  : "s");
}

std::string ngspice_label(NodeId id) {
  if (id == kVdd) return "vdd";
  if (id == kVss) return "vss";
  return label(id);
}

static int graph_counter = 0;

void print_graphviz(const Network& net) {
  auto out = Flatten(net.outputs());
  std::cout << "graph d" << (graph_counter++) << " {\n";
  for (int i = 0; i < net.num_transistors(); ++i) {
    TransistorId tid(i);
    int d = 2;
    for (const auto& scope : net.transistor_scope(tid)) {
      std::cout << std::string(d, ' ') << "subgraph cluster_" << scope
                << " {\n";
      std::cout << std::string(d, ' ') << "  label = \"" << scope << "\";\n";
      d += 2;
    }

    std::cout << std::string(d, ' ') << "subgraph cluster_t" << i << " {\n";
    std::cout << std::string(d, ' ') << "  label = \"t" << i
              << (net.transistor_type(tid) == TransistorType::kNChannel ? "N"
                                                                        : "P")
              << "\";\n";
    std::cout << std::string(d, ' ') << "  " << label(tid.drain())
              << " [label = \"drain\"];\n";
    std::cout << std::string(d, ' ') << "  " << label(tid.gate())
              << " [label = \"gate\"];\n";
    std::cout << std::string(d, ' ') << "  " << label(tid.source())
              << " [label = \"source\"];\n";
    std::cout << std::string(d, ' ') << "}\n";

    while (d > 2) {
      d -= 2;
      std::cout << std::string(d, ' ') << "}\n";
    }
  }

  for (auto [f, t] : net.ordered_connections()) {
    if (f.id < 0) {
      std::cout << "  _0_" << t.id << " [label=\"" << label(f) << "\"];\n";
      std::cout << "  _0_" << t.id << "--" << label(t) << ";\n";
    } else {
      std::cout << label(f) << "--" << label(t) << ";\n";
    }
  }

  for (int i = 0; i < out.size(); ++i) {
    std::cout << label(out[i]) << "--" << "o" << i << ";\n";
  }

  std::cout << "}\n";
}

void print_graphviz(
    GateNetwork& net, std::string_view label,
    const absl::flat_hash_map<GateTerminal, std::string>& colors,
    std::ostream& stream) {
  absl::flat_hash_map<Gate*, int> ids;
  auto terminal_node = [&](GateTerminal t) -> std::string {
    if (t == kLowGate) {
      return "false";
    }
    if (t == kHighGate) {
      return "true";
    }

    auto id = ids.try_emplace(t.first, ids.size());
    std::stringstream os;
    os << "n" << id.first->second << "_" << t.second;
    return os.str();
  };

  stream << "digraph d" << (graph_counter++) << " {\n";
  if (!label.empty()) {
    stream << "  label = \"" << label << "\";\n";
  }
  net.WalkUnordered([&](int id, Gate& gate) {
    int d = 2;
    // for (const auto& scope : gate.scope()) {
    //   stream << std::string(d, ' ') << "subgraph cluster_" << scope << "
    //   {\n"; stream << std::string(d, ' ') << "  label = \"" << scope <<
    //   "\";\n"; d += 2;
    // }

    stream << std::string(d, ' ') << "subgraph cluster_g" << id << " {\n";
    d += 2;

    auto color = colors.find(gate.output());
    if (color != colors.end()) {
      stream << std::string(d, ' ') << "style = \"filled\";\n";
      stream << std::string(d, ' ') << "color = \"" << color->second << "\";\n";
    }
    stream << std::string(d, ' ') << "label = \"" << to_string(gate) << "\";\n";
    for (int j = 0; j < gate.num_inputs(); ++j) {
      stream << std::string(d, ' ') << "g" << id << "i" << j << " [label = \"i"
             << j << "\"];";
    }
    for (int j = 0; j < gate.num_outputs(); ++j) {
      stream << std::string(d, ' ') << terminal_node(gate.output(j))
             << " [label = \"o" << j << "\"];\n";
    }
    while (d > 2) {
      d -= 2;
      stream << std::string(d, ' ') << "}\n";
    }
  });

  net.WalkUnordered([&](int id, Gate& gate) {
    for (int i = 0; i < gate.num_inputs(); ++i) {
      stream << "    " << terminal_node(gate.input(i)) << " -> g" << id << "i"
             << i << ";\n";
    }
  });

  for (int i = 0; i < net.num_inputs(); ++i) {
    auto input = net.GetInput(i);
    for (int b = 0; b < input.bitwidth(); ++b) {
      stream << terminal_node(input[b]) << " [label = \"" << net.input_label(i);
      if (input.bitwidth() > 1) {
        stream << "_" << b;
      }
      stream << "\"];\n";
    }
  }

  for (int i = 0; i < net.num_outputs(); ++i) {
    auto output = net.GetOutput(i);
    for (int b = 0; b < output.bitwidth(); ++b) {
      stream << terminal_node(output[b]) << "->" << "o" << i << "_" << b
             << ";\n";
      stream << "o" << i << "_" << b << " [label = \"" << net.output_label(i);
      if (output.bitwidth() > 1) {
        stream << "_" << b;
      }
      stream << "\"];\n";
    }
  }

  stream << "}\n";
}

void print_ngspice(const Network& net, NodeId out) {
  std::cout << "* NGSpice ignores the first line\n";
  std::cout << ".model nmos_model nmos (level=1 vto=0.4 kp=200u lambda=0.02)\n";
  std::cout
      << ".model pmos_model pmos (level=1 vto=-0.4 kp=100u lambda=0.02)\n";

  for (int i = 0; i < net.num_transistors(); ++i) {
    TransistorId tid(i);
    std::cout << "M" << i << " " << ngspice_label(tid.drain()) << " "
              << ngspice_label(tid.gate()) << " "
              << ngspice_label(tid.source());
    if (net.transistor_type(tid) == TransistorType::kNChannel) {
      std::cout << " vss nmos_model";
    } else {
      std::cout << " vdd pmos_model";
    }
    std::cout << " L=0.18u W=0.54u\n";
  }

  int i = 0;
  for (auto [f, t] : net.ordered_connections()) {
    std::cout << "Rbridge" << i << " " << ngspice_label(f) << " "
              << ngspice_label(t) << " 0.01\n";
    ++i;
  }

  std::cout << "Vdd vdd 0 DC 1.8V\n";
  std::cout << "Vin1 A 0 pulse(0 1.9 1n 100p 100p 5n 10n)\n";
  std::cout << "Vin2 B 0 pulse(0 1.9 11n 100p 100p 10n 20n)\n";

  std::cout << ".tran 0.1n 20n\n";
  std::cout << ".control\n";
  std::cout << "  run\n";
  std::cout << "  plot v(A) v(B) v(" << ngspice_label(out) << ")\n";
  std::cout << ".endc\n";
  std::cout << ".end\n";
}

toyhdl::serialization::Network ExportNetlist(const Network& net) {
  namespace s = toyhdl::serialization;
  std::cout << "{";

  absl::flat_hash_map<NodeId, std::string> input_names;

  auto convert_list = [](auto* out, int num, auto generate_item) {
    for (int i = 0; i < num; ++i) {
      generate_item(out->Add(), i);
    }
  };

  s::Network out;
  convert_list(out.mutable_inputs(), net.num_inputs(),
               [&](s::Input* input, int input_id) {
                 input->set_name(net.input_label(input_id));
                 input->set_bitwidth(net.input_bitwidth(input_id));

                 if (input->bitwidth() == 1) {
                   input_names[net.get_input(input_id)[0]] = input->name();
                 } else {
                   for (int b = 0; b < input->bitwidth(); ++b) {
                     input_names[net.get_input(input_id)[b]] =
                         absl::StrCat(input->name(), ".", b);
                   }
                 }
               });

  convert_list(out.mutable_transistors(), net.num_transistors(),
               [&](s::Transistor* transistor, int id) {
                 TransistorId tid(id);
                 if (net.transistor_type(tid) == TransistorType::kNChannel) {
                   transistor->set_kind(s::Transistor::kNChannel);
                 } else {
                   transistor->set_kind(s::Transistor::kPChannel);
                 }
                 for (const std::string& scope : net.transistor_scope(tid)) {
                   transistor->add_scope(scope);
                 }
               });

  auto netlist_label = [&](NodeId id) -> std::string {
    if (id == kVdd) return "vdd";
    if (id == kVss) return "vss";

    if (id.is_input()) {
      return input_names[id];
    }

    TransistorId tid(id);
    return std::to_string(tid.id) + (id == tid.drain()  ? ".d"
                                     : id == tid.gate() ? ".g"
                                                        : ".s");
  };

  std::vector<std::pair<NodeId, NodeId>> connections(
      net.ordered_connections().begin(), net.ordered_connections().end());
  convert_list(out.mutable_connections(), connections.size(),
               [&](s::Connection* connection, int connection_id) {
                 auto [a, b] = connections[connection_id];
                 connection->set_node_a(netlist_label(a));
                 connection->set_node_b(netlist_label(b));
               });

  convert_list(out.mutable_outputs(), net.num_outputs(),
               [&](s::Output* s_output, int output_id) {
                 const dyn_reg& output = net.outputs()[output_id];
                 s_output->set_name(net.output_label(output_id));
                 convert_list(s_output->mutable_terminals(), output.bitwidth(),
                              [&](std::string* terminal, int bit_id) {
                                *terminal = netlist_label(output[bit_id]);
                              });
               });

  std::vector<std::pair<NodeId, std::string>> tags(net.tagged_nodes().begin(),
                                                   net.tagged_nodes().end());
  convert_list(out.mutable_tagged_nodes(), tags.size(),
               [&](s::TaggedNode* tag, int tag_id) {
                 tag->set_node(netlist_label(tags[tag_id].first));
                 tag->set_tag(tags[tag_id].second);
               });

  return out;
}
