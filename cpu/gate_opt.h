#ifndef GATE_OPT_H__
#define GATE_OPT_H__

#include <functional>

#include "absl/types/span.h"
#include "gate_lib.h"

struct FoldGatesOpts {
  // Lower mux to logic gates (avoids transmission gates).
  bool lower_mux = false;

  std::optional<int> maximum_nor_arity = std::nullopt;
  std::optional<int> maximum_nand_arity = std::nullopt;

  std::function<void()> callback = +[]() {};
};

bool FoldGates(GateNetwork& net, const FoldGatesOpts& opts);
bool CseGates(GateNetwork& net);
bool MergeGates(GateNetwork& net);
bool FactorGates(GateNetwork& net);
bool OptimizeCNF(GateNetwork& net);
bool RunCanonicalizer(GateNetwork& net);

bool RunGateOptPipeline(GateNetwork& net, const FoldGatesOpts& opts);

#endif
