#include "transistor_opt.h"

#include <map>
#include <tuple>
#include <unordered_map>

#include "transistor_lib.h"

bool RunCse(Network& net, int max_iters) { return true; }

bool RunDte(Network& net) {
  Network repl;
  for (int i : net.input_bitwidths()) {
    repl.make_input(i);
  }

  int erased = 0;
  std::unordered_map<NodeId, NodeId> mapping;
  for (int i = 0; i < net.num_transistors(); ++i) {
    TransistorId t(i);
    if (net.connected_nodes(t.gate()).empty()) {
      ++erased;
      continue;
    }
    TransistorId nt = repl.make_transistor(net.transistor_type(t));
    mapping[t.source()] = nt.source();
    mapping[t.drain()] = nt.drain();
    mapping[t.gate()] = nt.gate();
  }

  auto lookup = [&](NodeId node) {
    auto mapped = mapping.find(node);
    if (mapped != mapping.end()) {
      return mapped->second;
    }
    return node;
  };

  if (erased == 0) return false;

  for (auto [f, t] : net.ordered_connections()) {
    repl.connect(lookup(f), lookup(t));
  }
  for (int output_index = 0; output_index < net.num_outputs(); ++output_index) {
    const auto& output = net.outputs()[output_index];
    for (int i = 0; i < output.bitwidth(); ++i) {
      output[i] = lookup(output[i]);
    }
    repl.DeclareOutput(output, net.output_label(output_index));
  }

  net = std::move(repl);
  return true;
}
