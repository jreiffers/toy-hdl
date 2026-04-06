#include "lower_gates.h"

#include <functional>

#include "absl/container/inlined_vector.h"
#include "absl/types/span.h"

Network Lower(GateNetwork& net) {
  Network result;

  std::unordered_map<GateTerminal, NodeId> lowered;

  int num_inputs = net.input_bitwidths().size();

  for (int i = 0; i < num_inputs; ++i) {
    DynGateReg input = net.GetInput(i);
    dyn_reg lowered_input = result.make_input(input.bitwidth());
    for (int b = 0; b < input.bitwidth(); ++b) {
      lowered[input[b]] = lowered_input[b];
    }
  }

  lowered[kLowGate] = kVss;
  lowered[kHighGate] = kVdd;

  int depth = 0;
  std::function<void(Gate&)> lower = [&](Gate& gate) {
    if (lowered.count({&gate, 0})) return;

    ++depth;

    absl::InlinedVector<NodeId, 2> inputs;
    for (auto input : gate.inputs()) {
      lower(*input.first);
      inputs.push_back(lowered.at(input));
    }

    absl::InlinedVector<NodeId, 1> outputs;
    switch (gate.kind()) {
      case GateKind::kDead:
        assert(!"encountered a use of a dead gate");
        break;
      case GateKind::kNot:
        assert(inputs.size() == 1);
        outputs.push_back(make_not(result, inputs[0]));
        break;
      case GateKind::kMux:
        assert(inputs.size() == 4);
        outputs.push_back(
            make_mux(result, inputs[0], inputs[1], inputs[2], inputs[3]));
        break;
      case GateKind::kNand:
        outputs.push_back(make_nand(result, inputs));
        break;
      case GateKind::kNor:
        outputs.push_back(make_nor(result, inputs));
        break;
      case GateKind::kLookup:
        assert(inputs.size() == 4);
        outputs.push_back(make_lookup(result, inputs[0], inputs[1], inputs[2],
                                      inputs[3], gate.lookup_data()));
        break;
    }

    for (int i = 0; i < gate.num_outputs(); ++i) {
      lowered[gate.output(i)] = outputs[i];
    }

    --depth;
  };

  std::vector<dyn_reg> lowered_outputs;
  for (int oi = 0; oi < net.num_outputs(); ++oi) {
    auto o = net.GetOutput(oi);
    std::vector<NodeId> ids;
    for (int i = 0; i < o.bitwidth(); ++i) {
      lower(*o[i].first);
      ids.push_back(lowered.at(o[i]));
    }
    result.DeclareOutput(dyn_reg(std::move(ids)));
  }

  return result;
}
