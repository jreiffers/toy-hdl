#ifndef EVAL_H__
#define EVAL_H__

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "export.h"
#include "gate_lib.h"
#include "transistor_lib.h"

enum class PinState {
  kUndefined,  // The pin is floating.
  kLow,        // The pin is driven low.
  kHigh,       // The pin is driven high.
  kShort,      // The pin is driven both low and high.
};

std::string to_string(PinState state);

// Simplistic circuit evaluator. Stops evaluating once a short is found.
// Note: I don't know if this actually implements a reasonable approximation of
// how transistors work.
absl::flat_hash_map<NodeId, PinState> Evaluate(
    const Network& net, absl::flat_hash_map<NodeId, PinState> inputs);

// Evaluates all inputs. Returns nullopt if a short or undefined output was
// found. The row in the result corresponds to a bit string formed by the
// inputs, where input 0 is the LSB.
std::optional<std::vector<uint32_t>> EvaluateAll(
    const Network& net, const std::vector<NodeId>& outputs);

// Evaluates all inputs and prints a truth table. Prints shorts and undefined
// outputs as well.
void PrintTruthTable(const Network& net, const std::vector<NodeId>& outputs,
                     int next_input = 0,
                     absl::flat_hash_map<NodeId, PinState> state = {});

// Verifies tha the given network satisfies the given spec.
absl::Status VerifySpec(const Network& net, absl::Span<const dyn_reg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec);

absl::Status VerifySpec(const GateNetwork& net,
                        absl::Span<const DynGateReg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec);

// Propagates the values of `inputs` through the network and updates `state`.
enum class GateTerminalState { kLow, kHigh, kZ };

absl::Status EvaluateStep(
    const GateNetwork& net,
    absl::flat_hash_map<GateTerminal, GateTerminalState>& state,
    absl::Span<const uint32_t> inputs);

template <int bw>
std::optional<uint32_t> GetNum(
    const absl::flat_hash_map<GateTerminal, GateTerminalState>& state,
    const GateReg<bw>& reg) {
  // TODO: unify with the dynamic version, or just delete this one.
  static_assert(bw <= 32);
  uint32_t res = 0;
  for (int i = 0; i < bw; ++i) {
    GateTerminalState s = state.at(reg[i]);
    if (s == GateTerminalState::kZ) return std::nullopt;
    if (s == GateTerminalState::kHigh) {
      res |= 1ul << i;
    }
  }
  return res;
}

std::optional<uint32_t> GetNum(
    GateNetwork& net,
    const absl::flat_hash_map<GateTerminal, GateTerminalState>& state,
    int output_index);

template <typename T, int num_inputs>
struct GateSpec {
 public:
  // T should implement:
  //
  //   T()
  //   T transition(... inputs) const
  //   absl::InlinedVector<std::optional<uint32_t>, 4> outputs() const
  //   operator==
  //   absl hashing and stringification
  //
  //
  // T may have arbitrary internal state. `transition` may change that state
  // in arbitrary ways. `outputs` must be a pure function of the state.

  static absl::Status Verify(GateNetwork& net);
};

template <typename T, int num_inputs>
absl::Status GateSpec<T, num_inputs>::Verify(GateNetwork& net) {
  using GateState = absl::flat_hash_map<GateTerminal, GateTerminalState>;

  // The aggregate state is the gate state and the simulation state. The mapping
  // will not be 1:1, since the GateState doesn't just contain the internal
  // state.
  using State = std::pair<GateState, T>;
  using Inputs = std::array<uint32_t, num_inputs>;
  using Source = std::pair<std::optional<State>, Inputs>;

  int reset = -1;
  int clk = -1;
  for (int i = 0; i < net.num_inputs(); ++i) {
    if (net.input_label(i) == "reset") reset = i;
    if (net.input_label(i) == "clk") clk = i;
  }

  if (reset == -1 || clk == -1) {
    return absl::InternalError("Didn't find reset and/or clock inputs.");
  }

  // The set of states whose outgoing transitions we have enqueued.
  absl::flat_hash_set<State> enqueued;

  // The inputs that led to this state.
  absl::flat_hash_map<State, Source> reached_through;

  std::deque<State> queue;

  GateState init;

  // Pull reset low for a cycle so we get a defined initial state.
  EvaluateStep(net, init, Inputs{0});
  T init_spec = std::apply(
      [&](auto... params) { return T().transition(params...); }, Inputs{0});

  queue.push_back({init, init_spec});
  enqueued.emplace(init, init_spec);
  reached_through[{init, init_spec}] = {std::nullopt, Inputs{0}};

  auto a = [&](auto x) -> std::string { return x ? absl::StrCat(*x) : "Z"; };

  auto get_trajectory = [&](const State& state) {
    const State* s = &state;
    std::string result;
    int i = 0;
    while (s && (++i < 6)) {
      absl::StrAppend(
          &result, "Spec ", s->second, " outputs: ",
          absl::StrJoin(s->second.outputs(), ", ",
                        [&](std::string* out, std::optional<uint32_t> v) {
                          absl::StrAppend(out, a(v));
                        }),
          "\n");
      auto& prev = reached_through[*s];
      absl::StrAppend(&result, "  Reached through inputs ",
                      absl::StrJoin(prev.second, ", "), "\n");
      if (prev.first) {
        s = &prev.first.value();
      } else {
        s = nullptr;
      }
    }
    return result;
  };

  std::function<absl::Status(const State&, bool,
                             std::array<uint32_t, num_inputs>, int)>
      explore = [&](const State& in, bool changed_reset_clk,
                    std::array<uint32_t, num_inputs> inputs,
                    int next_input) -> absl::Status {
    if (next_input == num_inputs) {
      // Generate the new simulation state.
      T next = std::apply(
          [&](auto... params) { return in.second.transition(params...); },
          inputs);

      // Simulate a step of the gate network.
      auto gate_state = in.first;
      auto status = EvaluateStep(net, gate_state, inputs);

      if (!status.ok()) {
        return status;
      }
      if (!enqueued.emplace(gate_state, next).second) {
        return absl::OkStatus();
      }

      assert(gate_state != in.first || !(next == in.second));
      bool inserted =
          reached_through
              .try_emplace(State{gate_state, next}, Source{in, inputs})
              .second;
      (void)inserted;
      assert(inserted);

      auto expected_out = next.outputs();
      for (int i = 0; i < net.num_outputs(); ++i) {
        auto actual = GetNum(net, gate_state, i);
        if (expected_out[i] != actual) {
          static int counter = 0;

          std::ofstream s(absl::StrCat("/tmp/out", counter++, ".dot"));
          absl::flat_hash_map<GateTerminal, std::optional<bool>> colors;
          for (auto [key, val] : gate_state) {
            if (val == GateTerminalState::kZ) {
              colors[key] = std::nullopt;
            } else {
              colors[key] = val == GateTerminalState::kHigh;
            }
          }
          print_graphviz(net, colors, s);

          std::string error =
              absl::StrCat("Mismatched output ", i, ". expected ",
                           a(expected_out[i]), ", got ", a(actual), ".\n");

          absl::StrAppend(&error, get_trajectory({gate_state, next}));

          std::cerr << error << "\n";
          return absl::InternalError(error);
        }
      }

      queue.push_back({gate_state, next});
      return absl::OkStatus();
    }

    if (next_input == clk || next_input == reset) {
      uint32_t prev_val = reached_through[in].second[next_input];
      if (changed_reset_clk) {
        inputs[next_input] = prev_val;
        return explore(in, true, inputs, next_input + 1);
      }

      for (int i = 0; i < 2; ++i) {
        inputs[next_input] = i;
        auto status = explore(in, i != prev_val, inputs, next_input + 1);
        if (!status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }

    int bw = net.input_bitwidths()[next_input];
    for (uint32_t i = 0; i < (1ul << bw); ++i) {
      inputs[next_input] = i;
      auto status = explore(in, changed_reset_clk, inputs, next_input + 1);
      if (!status.ok()) {
        return status;
      }
    }

    return absl::OkStatus();
  };

  while (!queue.empty()) {
    auto front = queue.front();
    queue.pop_front();
    auto result = explore(front, false, {}, 0);
    if (!result.ok()) return result;
  }

  return absl::OkStatus();
}

#endif
