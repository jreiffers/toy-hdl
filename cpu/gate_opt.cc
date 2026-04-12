#include "gate_opt.h"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "absl/algorithm/container.h"

namespace {

bool InputIs(Gate& gate, GateKind kind, int input_id = 0) {
  return gate.input(input_id).first &&
         gate.input(input_id).first->kind() == kind;
}

std::optional<GateTerminal> SkipNot(Gate& gate, int input_id = 0) {
  if (!InputIs(gate, GateKind::kNot, input_id)) {
    return std::nullopt;
  }
  return gate.input(input_id).first->input(0);
}

}  // namespace

bool FoldGates(GateNetwork& net, const FoldGatesOpts& opts) {
  bool changed;
  bool ever_changed = false;

  assert(net.current_scope().empty());

  do {
    changed = false;
    net.WalkUnordered([&](int, Gate& gate) {
      bool canonicalized = gate.Canonicalize();
      if (gate.kind() == GateKind::kNot) {
        if (auto in = SkipNot(gate); in) {
          gate.ReplaceAllUsesWith(*in);
          changed = true;
        } else if (gate.input(0) == kLowGate) {
          gate.ReplaceAllUsesWith(kHighGate);
          changed = true;
        } else if (gate.input(0) == kHighGate) {
          gate.ReplaceAllUsesWith(kLowGate);
          changed = true;
        }
      }

      if (opts.lower_mux && gate.kind() == GateKind::kMux) {
        ScopeGuard guard(net, gate.scope());
        auto sel = gate.input(0);
        auto not_sel = gate.input(1);
        auto hi = gate.input(2);
        auto lo = gate.input(3);
        gate.ReplaceAllUsesWith(
            net.Nand({net.Nand({sel, hi}), net.Nand({not_sel, lo})}));
        changed = true;
      }

      if (gate.num_inputs() == 2) {
        if (gate.kind() == GateKind::kNor) {
          if (auto a = SkipNot(gate, 0), b = SkipNot(gate, 1); a && b) {
            ScopeGuard guard(net, gate.scope());
            gate.ReplaceAllUsesWith(net.And(*a, *b));
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kLowGate)) {
            gate.EraseInputs(kLowGate);
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kHighGate)) {
            gate.ReplaceAllUsesWith(kLowGate);
            changed = true;
          }
        }

        if (gate.kind() == GateKind::kNand) {
          if (auto a = SkipNot(gate, 0), b = SkipNot(gate, 1); a && b) {
            ScopeGuard guard(net, gate.scope());
            gate.ReplaceAllUsesWith(net.Or(*a, *b));
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kLowGate)) {
            gate.ReplaceAllUsesWith(kHighGate);
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kHighGate)) {
            gate.EraseInputs(kHighGate);
            changed = true;
          }
        }
      }

      if (gate.kind() == GateKind::kLookup && gate.num_inputs() == 2) {
        changed = true;
        if (gate.lookup_data() == 0) {
          gate.ReplaceAllUsesWith(kLowGate);
        } else if (gate.lookup_data() == 1) {
          gate.ReplaceAllUsesWith(gate.input(0));
        } else if (gate.lookup_data() == 2) {
          gate.ReplaceAllUsesWith(gate.input(1));
        } else {
          assert(gate.lookup_data() == 3);
          gate.ReplaceAllUsesWith(kHighGate);
        }
      }

      opts.callback();
      changed |= canonicalized;
    });

    ever_changed |= changed;
  } while (changed);

  return ever_changed;
}

bool CseGates(GateNetwork& net) {
  bool changed;
  bool ever_changed = false;
  do {
    std::map<Gate, GateTerminal> seen_gates;
    changed = false;
    net.WalkUnordered([&](int, Gate& gate) {
      // TODO fix scope
      auto [it, inserted] = seen_gates.try_emplace(gate, gate.output());
      if (!inserted) {
        changed = true;
        gate.ReplaceAllUsesWith(it->second);
        gate.Erase();
      }
    });
    ever_changed |= changed;
  } while (changed);

  return ever_changed;
}
