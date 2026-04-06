#ifndef EVAL_H__
#define EVAL_H__

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"
#include "gate_lib.h"
#include "transistor_lib.h"

enum PinState {
  kUndefined,  // The pin is floating.
  kLow,        // The pin is driven low.
  kHigh,       // The pin is driven high.
  kShort,      // The pin is driven both low and high.
};

std::string to_string(PinState state);

// Simplistic circuit evaluator. Stops evaluating once a short is found.
// Note: I don't know if this actually implements a reasonable approximation of
// how transistors work.
std::unordered_map<NodeId, PinState> Evaluate(
    const Network& net, std::unordered_map<NodeId, PinState> inputs);

// Evaluates all inputs. Returns nullopt if a short or undefined output was
// found. The row in the result corresponds to a bit string formed by the
// inputs, where input 0 is the LSB.
std::optional<std::vector<uint32_t>> EvaluateAll(
    const Network& net, const std::vector<NodeId>& outputs);

// Evaluates all inputs and prints a truth table. Prints shorts and undefined
// outputs as well.
void PrintTruthTable(const Network& net, const std::vector<NodeId>& outputs,
                     int next_input = 0,
                     std::unordered_map<NodeId, PinState> state = {});

// Verifies tha the given network satisfies the given spec.
absl::Status VerifySpec(const Network& net, absl::Span<const dyn_reg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec);

absl::Status VerifySpec(const GateNetwork& net,
                        absl::Span<const DynGateReg> outputs,
                        const std::function<absl::InlinedVector<uint32_t, 4>(
                            absl::Span<const uint32_t> inputs)>& spec);

// Propagates the values of `inputs` through the network and updates `state`.
absl::Status EvaluateStep(const GateNetwork& net,
                          std::unordered_map<GateTerminal, bool>& state,
                          absl::Span<const uint32_t> inputs);

#endif
