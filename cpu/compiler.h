#ifndef COMPILER_H__
#define COMPILER_H__

#include <functional>

#include "gate_lib.h"
#include "transistor_lib.h"

struct CompileOpts {
  bool avoid_transmission_gates = false;

  std::function<void()> callback = +[]() {};
};

Network Compile(GateNetwork& net, const CompileOpts& opts = {});

#endif
