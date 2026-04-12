#ifndef GATE_OPT_H__
#define GATE_OPT_H__

#include <functional>

#include "absl/types/span.h"
#include "gate_lib.h"

struct FoldGatesOpts {
  // Lower mux to logic gates (avoids transmission gates).
  bool lower_mux = false;

  std::function<void()> callback = +[]() {};
};

bool FoldGates(GateNetwork& net, const FoldGatesOpts& opts);
bool CseGates(GateNetwork& net);

#endif
