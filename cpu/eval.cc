#include "eval.h"

#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

std::string to_string(PinState state) {
  switch (state) {
    case PinState::kUndefined:
      return "Z";
    case PinState::kLow:
      return "0";
    case PinState::kHigh:
      return "1";
    case PinState::kShort:
      return "X";
  }
}

std::unordered_map<NodeId, PinState> Evaluate(
    const Network& net, std::unordered_map<NodeId, PinState> inputs) {
  inputs[kVdd] = PinState::kHigh;
  inputs[kVss] = PinState::kLow;

  std::queue<NodeId> queue;
  for (auto [input, _] : inputs) {
    queue.push(input);
  }

  auto update_pin = [&](NodeId pin, PinState new_state) {
    auto& state = inputs[pin];
    if (state == new_state) {
      return;
    }

    queue.push(pin);
    if (state != PinState::kUndefined) {
      state = PinState::kShort;
      return;
    }

    state = new_state;
    TransistorId t(pin);

    PinState gate_state = inputs[t.gate()];
    if (gate_state == PinState::kUndefined) {
      return;
    }

    PinState source_state = inputs[t.source()];
    PinState& drain_state = inputs[t.drain()];

    if ((source_state == PinState::kShort || gate_state == PinState::kShort) &&
        (drain_state != PinState::kShort)) {
      drain_state = PinState::kShort;
      queue.push(t.drain());
      return;
    }

    if (net.transistor_type(t) == TransistorType::kNChannel) {
      if (source_state == PinState::kLow && gate_state == PinState::kHigh &&
          drain_state != PinState::kLow) {
        if (drain_state == PinState::kUndefined) {
          drain_state = PinState::kLow;
        } else {
          drain_state = PinState::kShort;
        }
        queue.push(t.drain());
      }
    } else {
      if (source_state == PinState::kHigh && gate_state == PinState::kLow &&
          drain_state != PinState::kHigh) {
        if (drain_state == PinState::kUndefined) {
          drain_state = PinState::kHigh;
        } else {
          drain_state = PinState::kShort;
        }
        queue.push(t.drain());
      }
    }
  };

  while (!queue.empty()) {
    NodeId node = queue.front();

    queue.pop();
    auto node_state = inputs[node];

    auto connections = net.connected_nodes(node);
    for (auto to : connections) {
      update_pin(to, node_state);
    }
  }

  return inputs;
}

namespace {

bool EvaluateAllRec(
    const Network& net,
    std::function<bool(const std::unordered_map<NodeId, PinState>&)> callback,
    int next_input, std::unordered_map<NodeId, PinState> state) {
  if (next_input == -1) {
    state = Evaluate(net, std::move(state));
    if (!callback(state)) return false;
    return true;
  }

  NodeId id = net.get_input_bit(next_input);
  state[id] = PinState::kLow;
  if (!EvaluateAllRec(net, callback, next_input - 1, state)) {
    return false;
  }
  state[id] = PinState::kHigh;
  return EvaluateAllRec(net, callback, next_input - 1, std::move(state));
}

}  // namespace

std::optional<std::vector<uint32_t>> EvaluateAll(
    const Network& net, const std::vector<NodeId>& outputs) {
  assert(net.num_input_bits() <= 32);
  assert(outputs.size() <= 32);

  std::vector<uint32_t> out;
  auto callback = [&](const std::unordered_map<NodeId, PinState>& state) {
    uint32_t& out_bitstring = out.emplace_back();
    for (int i = 0; i < outputs.size(); ++i) {
      PinState out_state = state.at(outputs[i]);
      if (out_state == PinState::kHigh) {
        out_bitstring |= 1 << i;
      } else if (out_state != PinState::kLow) {
        return false;
      }
    }
    return true;
  };

  if (!EvaluateAllRec(net, callback, /*next_input=*/net.num_input_bits() - 1,
                      /*state=*/{})) {
    return std::nullopt;
  }
  return out;
}

void PrintTruthTable(const Network& net, const std::vector<NodeId>& outputs,
                     int next_input,
                     std::unordered_map<NodeId, PinState> state) {
  if (next_input == net.num_input_bits()) {
    state = Evaluate(net, std::move(state));
    for (int i = 0; i < net.num_input_bits(); ++i) {
      std::cout << to_string(state[net.get_input_bit(i)]);
    }
    std::cout << " -> ";
    for (NodeId o : outputs) {
      std::cout << to_string(state[o]) << " ";
    }
    std::cout << "\n";
    return;
  }

  NodeId id = net.get_input_bit(next_input);
  state[id] = PinState::kLow;
  PrintTruthTable(net, outputs, next_input + 1, state);
  state[id] = PinState::kHigh;
  PrintTruthTable(net, outputs, next_input + 1, std::move(state));
}

absl::Status VerifySpec(const Network& net, absl::Span<const dyn_reg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec) {
  std::vector<dyn_reg> inputs;
  for (int input_id = 0; input_id < net.num_inputs(); ++input_id) {
    inputs.push_back(net.get_input(input_id));
  }

  std::vector<std::string> errors;
  auto encode_num = [&](const std::unordered_map<NodeId, PinState>& state,
                        const dyn_reg& reg) -> std::optional<uint32_t> {
    uint32_t result = 0;
    for (int b = 0; b < reg.bitwidth(); ++b) {
      auto pin_state_it = state.find(reg[b]);
      if (pin_state_it == state.end()) {
        errors.push_back(
            absl::StrCat("no state found for pin ", reg[b].id, ". "));
        return std::nullopt;
      }
      auto pin_state = state.at(reg[b]);
      if (pin_state == PinState::kHigh) {
        result |= (1ull << b);
      } else if (pin_state != PinState::kLow) {
        errors.push_back(absl::StrCat("detected invalid pin state at ",
                                      reg[b].id, ": ", to_string(pin_state)));
        return std::nullopt;
      }
    }
    return result;
  };

  auto callback = [&](const std::unordered_map<NodeId, PinState>& state) {
    auto encode_nums = [&](absl::Span<const dyn_reg> regs) {
      absl::InlinedVector<uint32_t, 4> ret;
      for (const auto& reg : regs) {
        auto maybe_val = encode_num(state, reg);
        if (!maybe_val) {
          return ret;
        }
        ret.push_back(*maybe_val);
      }
      return ret;
    };

    auto input_vals = encode_nums(inputs);
    // TODO: Better error messages.
    if (input_vals.size() != inputs.size()) return false;

    auto expected = spec(input_vals);
    auto actual = encode_nums(outputs);

    if (expected != actual) {
      errors.push_back(
          absl::StrCat("At input ", absl::StrJoin(input_vals, ", "),
                       " expected outputs ", absl::StrJoin(expected, ", "),
                       " but got ", absl::StrJoin(actual, ", "), ". "));
    }

    return expected == actual;
  };

  bool success =
      EvaluateAllRec(net, callback, net.num_input_bits() - 1, /*state=*/{});
  if (!success) {
    return absl::InternalError(absl::StrCat(
        "Implementation does not match spec: ", absl::StrJoin(errors, "")));
  }

  return absl::OkStatus();
}

absl::Status VerifySpec(const GateNetwork& net,
                        absl::Span<const DynGateReg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec) {
  if (net.total_input_bits() >= 32) {
    return absl::InvalidArgumentError("Too many input bits.");
  }

  std::unordered_map<GateTerminal, bool> vals;

  std::vector<std::string> errors;
  std::function<bool(uint32_t, GateTerminal)> get;
  get = [&](uint32_t mask, GateTerminal t) -> bool {
    if (t == kHighGate) {
      return true;
    }
    if (t == kLowGate) {
      return false;
    }

    if (t.first == nullptr) {
      return (mask >> t.second) & 1;
    }

    auto [it, inserted] = vals.try_emplace(t, false);
    if (inserted) {
      auto& gate = *t.first;
      bool r;
      switch (gate.kind()) {
        case GateKind::kDead:
          assert(false);
          break;
        case GateKind::kNot:
          it->second = !get(mask, gate.input(0));
          break;
        case GateKind::kNand:
          r = true;
          for (int i = 0; r && i < gate.num_inputs(); ++i) {
            r &= get(mask, gate.input(i));
          }
          it->second = !r;
          break;
        case GateKind::kMux: {
          bool c = get(mask, gate.input(0));
          bool not_c = get(mask, gate.input(1));
          if (c == not_c) {
            errors.push_back("Inconsistent mux gate inputs.");
          }
          it->second = get(mask, c ? gate.input(2) : gate.input(3));
          break;
        }
        case GateKind::kNor:
          r = false;
          for (int i = 0; !r && i < gate.num_inputs(); ++i) {
            r |= get(mask, gate.input(i));
          }
          it->second = !r;
          break;
        case GateKind::kLookup:
          uint64_t p = 0;
          for (int i = 0; i < gate.num_inputs() / 2; ++i) {
            bool v = get(mask, gate.input(i));
            bool not_v = get(mask, gate.input(i + gate.num_inputs() / 2));
            if (v == not_v) {
              errors.push_back("Inconsistent lookup gate inputs.");
            }

            if (get(mask, gate.input(i))) {
              p |= 1 << i;
            }
          }
          it->second = (gate.lookup_data() >> p) & 1;
          break;
      }
    }
    return it->second;
  };

  auto encode_regs = [&](uint32_t mask, absl::Span<const DynGateReg> regs) {
    absl::InlinedVector<uint32_t, 4> result;
    for (const DynGateReg& reg : regs) {
      uint32_t r = 0;
      for (int b = 0; b < reg.bitwidth(); ++b) {
        r |= (get(mask, reg[b]) ? 1 : 0) << b;
      }
      result.push_back(r);
    }
    return result;
  };

#ifdef DEBUG_EVAL
  std::function<void(uint32_t, GateTerminal, int)> print;
  print = [&](uint32_t mask, GateTerminal t, int d) {
    std::cerr << std::string(d * 2, ' ');
    if (t == kHighGate) {
      std::cerr << "1\n";
      return;
    }
    if (t == kLowGate) {
      std::cerr << "0\n";
      return;
    }

    if (t.first == nullptr) {
      std::cerr << "i" << t.second;
    } else {
      std::cerr << to_string(*t.first);
    }
    std::cerr << ": " << (get(mask, t) & 1) << "\n";

    if (t.first) {
      for (int i = 0; i < t.first->num_inputs(); ++i) {
        print(mask, t.first->input(i), d + 1);
      }
    }
  };
#endif

  absl::InlinedVector<DynGateReg, 2> inputs;
  for (int input_id = 0; input_id < net.num_inputs(); ++input_id) {
    inputs.push_back(net.GetInput(input_id));
  }

  uint32_t max = 1ul << net.total_input_bits();
  for (uint32_t i = 0; i < max; ++i) {
    vals.clear();
    auto input_vals = encode_regs(i, inputs);

    auto expected = spec(input_vals);
    auto actual = encode_regs(i, outputs);

    if (expected != actual) {
#ifdef DEBUG_EVAL
      for (auto& o : outputs) {
        for (int b = 0; b < o.bitwidth(); ++b) {
          std::cerr << "---output---\n";
          print(i, o[b], 0);
        }
      }
#endif

      errors.push_back(
          absl::StrCat("At input ", absl::StrJoin(input_vals, ", "),
                       " expected outputs ", absl::StrJoin(expected, ", "),
                       " but got ", absl::StrJoin(actual, ", "), ". "));
      break;
    }
  }

  if (errors.empty()) {
    return absl::OkStatus();
  }

  return absl::InternalError(absl::StrCat(
      "Implementation does not match spec:\n", absl::StrJoin(errors, "\n")));
}

absl::Status EvaluateStep(const GateNetwork& net,
                          std::unordered_map<GateTerminal, bool>& state,
                          absl::Span<const uint32_t> inputs) {
  if (inputs.size() != net.num_inputs()) {
    return absl::InvalidArgumentError("Mismatched number of inputs.");
  }
  std::unordered_set<GateTerminal> seen;
  std::deque<GateTerminal> queue;

  auto compute_value = [&](GateTerminal terminal) -> std::optional<bool> {
    if (terminal == kLowGate) return false;
    if (terminal == kHighGate) return true;
    if (terminal.first == nullptr) return state[terminal];

    auto& gate = *terminal.first;
    if (gate.kind() == GateKind::kDead) return std::nullopt;

    absl::InlinedVector<std::optional<bool>, 4> inputs;

    for (int i = 0; i < gate.num_inputs(); ++i) {
      auto it = state.find(gate.input(i));
      if (it == state.end()) {
        inputs.push_back(std::nullopt);
      } else {
        inputs.push_back(it->second);
      }
    }

    switch (gate.kind()) {
      case GateKind::kDead:
        return std::nullopt;
      case GateKind::kNot:
        if (inputs[0]) {
          return !*inputs[0];
        }
        return std::nullopt;
      case GateKind::kMux:
        if (inputs[2] == inputs[3]) return inputs[2];

        if (!inputs[0].has_value()) {
          return std::nullopt;
        }

        return *inputs[0] ? inputs[2] : inputs[3];
      case GateKind::kNand:
        if (absl::c_contains(inputs, false)) return true;
        if (absl::c_contains(inputs, std::nullopt)) return std::nullopt;
        return false;
      case GateKind::kNor:
        if (absl::c_contains(inputs, true)) return false;
        if (absl::c_contains(inputs, std::nullopt)) return std::nullopt;
        return true;
      case GateKind::kLookup:
        if (absl::c_contains(inputs, std::nullopt)) return std::nullopt;

        uint32_t index = 0;
        for (int i = 0; i < inputs.size() / 2; ++i) {
          if (*inputs[i]) {
            index |= 1ul << i;
          }
        }

        return (gate.lookup_data() >> index) & 1;
    }
  };

  auto enqueue_ready_users = [&](GateTerminal terminal) -> absl::Status {
    for (auto user : net.GetUsers(terminal)) {
      auto output = user.first->output();

      auto val = compute_value(output);
      if (!val) {
        continue;
      }


      auto inserted = seen.insert(output).second;
      if (!inserted) {
        if (state[output] == *val) continue;
        //     return absl::InternalError("Network failed to converge.");
      }

      state[output] = *val;
      queue.push_back(output);
    }
    return absl::OkStatus();
  };

  for (int i = 0; i < net.num_inputs(); ++i) {
    auto input = net.GetInput(i);
    for (int b = 0; b < input.bitwidth(); ++b) {
      state[input[b]] = (inputs[i] >> b) & 1;
      seen.insert(input[b]);
      queue.push_back(input[b]);
    }
  }

  while (!queue.empty()) {
    auto next = queue.front();
    queue.pop_front();
    auto status = enqueue_ready_users(next);
    if (!status.ok()) return status;
  }

#ifdef DEBUG_EVAL
  const_cast<GateNetwork&>(net).WalkUnordered([&](int id, Gate& gate) {
    auto it = state.find(gate.output());
    if (it == state.end()) {
      std::cerr << "gate " << to_string(gate) << " has no value\n";
    } else {
      if (compute_value(gate.output()) != it->second) {
        assert(!"failed");
      }
    }
  });
#endif
  // TODO check if all outputs are determined?

  return absl::OkStatus();
}
