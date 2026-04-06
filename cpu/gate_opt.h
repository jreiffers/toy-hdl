#ifndef GATE_OPT_H__
#define GATE_OPT_H__

#include <functional>

#include "absl/types/span.h"
#include "gate_lib.h"

bool FoldGates(
    GateNetwork& net, const std::function<void()>& callback = +[]() {});
bool CseGates(GateNetwork& net);

#endif
