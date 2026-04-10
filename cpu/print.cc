#include "print.h"

#include <iostream>
#include <unordered_map>

#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "gate_lib.h"
#include "transistor_lib.h"

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

void print_graphviz(const Network& net) {
  auto out = Flatten(net.outputs());
  std::cout << "graph d {\n";
  for (int i = 0; i < net.num_transistors(); ++i) {
    TransistorId tid(i);
    std::cout << "  subgraph cluster_t" << i << " {\n";
    std::cout << "    label = \"t" << i
              << (net.transistor_type(tid) == TransistorType::kNChannel ? "N"
                                                                        : "P")
              << "\";\n";
    std::cout << "    " << label(tid.drain()) << " [label = \"drain\"];\n";
    std::cout << "    " << label(tid.gate()) << " [label = \"gate\"];\n";
    std::cout << "    " << label(tid.source()) << " [label = \"source\"];\n";
    std::cout << "  }\n";
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

void print_graphviz(GateNetwork& net,
                    const std::unordered_map<GateTerminal, bool>& colors) {
  std::unordered_map<Gate*, int> ids;
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

  std::cout << "digraph d {\n";
  net.WalkUnordered([&](int id, Gate& gate) {
    std::cout << "  subgraph cluster_g" << id << " {\n";
    auto color = colors.find(gate.output());
    if (color != colors.end()) {
      std::cout << "    style = \"filled\";\n";
      std::cout << "    color = \"";
      if (color->second) {
        std::cout << "blue";
      } else {
        std::cout << "red";
      }
      std::cout << "\";\n";
    }
    std::cout << "    label = \"" << to_string(gate) << "\";\n";
    for (int j = 0; j < gate.num_inputs(); ++j) {
      std::cout << "    g" << id << "i" << j << " [label = \"i" << j << "\"];";
    }
    for (int j = 0; j < gate.num_outputs(); ++j) {
      std::cout << "    " << terminal_node(gate.output(j)) << " [label = \"o"
                << j << "\"];\n";
    }
    std::cout << "  }\n";
  });

  net.WalkUnordered([&](int id, Gate& gate) {
    for (int i = 0; i < gate.num_inputs(); ++i) {
      std::cout << "    " << terminal_node(gate.input(i)) << " -> g" << id
                << "i" << i << ";\n";
    }
  });

  for (int i = 0; i < net.num_inputs(); ++i) {
    auto input = net.GetInput(i);
    for (int b = 0; b < input.bitwidth(); ++b) {
      std::cout << terminal_node(input[b]) << " [label = \""
                << net.input_label(i);
      if (input.bitwidth() > 1) {
        std::cout << "_" << b;
      }
      std::cout << "\"];\n";
    }
  }

  for (int i = 0; i < net.num_outputs(); ++i) {
    auto output = net.GetOutput(i);
    for (int b = 0; b < output.bitwidth(); ++b) {
      std::cout << terminal_node(output[b]) << "->" << "o" << i << "_" << b
                << ";\n";
      std::cout << "o" << i << "_" << b << " [label = \""
                << net.output_label(i);
      if (output.bitwidth() > 1) {
        std::cout << "_" << b;
      }
      std::cout << "\"];\n";
    }
  }

  std::cout << "}\n";
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

void print_netlist(const Network& net) {
  std::cout << "{";

  std::unordered_map<NodeId, std::string> input_names;

  auto print_list = [](const std::string& label, int num, auto print_item) {
    // std::format doesn't seem to exist in my standard library.
    std::cout << absl::Substitute(R"("$0":[)", label);
    for (int i = 0; i < num; ++i) {
      if (i > 0) std::cout << ",";
      print_item(i);
    }
    std::cout << "]";
  };

  print_list("inputs", net.num_inputs(), [&](int input_id) {
    const std::string& label = net.input_label(input_id);
    int bw = net.input_bitwidth(input_id);

    std::cout << absl::Substitute(R"({"label":"$0","bitwidth":$1})", label, bw);
    if (bw == 1) {
      input_names[net.get_input(input_id)[0]] = label;
    } else {
      for (int b = 0; b < bw; ++b) {
        input_names[net.get_input(input_id)[b]] = absl::StrCat(label, "_", b);
      }
    }
  });

  std::cout << R"(,"transistors":")";
  for (int i = 0; i < net.num_transistors(); ++i) {
    TransistorId tid(i);
    if (net.transistor_type(tid) == TransistorType::kNChannel) {
      std::cout << "n";
    } else {
      std::cout << "p";
    }
  }
  std::cout << R"(",)";

  auto netlist_label = [&](NodeId id) -> std::string {
    if (id == kVdd) return "vdd";
    if (id == kVss) return "vss";

    if (id.is_input()) {
      return input_names[id];
    }

    TransistorId tid(id);
    return "t" + std::to_string(tid.drain().id) +
           (id == tid.drain()       ? "d"
            : id == tid.gate() == 1 ? "g"
                                    : "s");
  };

  std::vector<std::pair<NodeId, NodeId>> connections(
      net.ordered_connections().begin(), net.ordered_connections().end());
  print_list("connections", connections.size(), [&](int connection_id) {
    auto [a, b] = connections[connection_id];
    std::cout << absl::Substitute(R"(["$0", "$1"])", netlist_label(a),
                                  netlist_label(b));
  });
  std::cout << ",";

  print_list("outputs", net.num_outputs(), [&](int output_id) {
    const dyn_reg& output = net.outputs()[output_id];
    std::cout << absl::Substitute(R"({"label":"$0","bitwidth":$1,)",
                                  net.output_label(output_id),
                                  output.bitwidth());
    print_list("nodes", output.bitwidth(), [&](int bit_id) {
      std::cout << absl::Substitute(R"("$0")", netlist_label(output[bit_id]));
    });
    std::cout << "}";
  });

  std::cout << "}\n";
}
