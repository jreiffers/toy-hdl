#ifndef COMPILER_H__
#define COMPILER_H__

#include <functional>

#include "cpu/fpga.h"
#include "cpu/gate_lib.h"
#include "cpu/transistor_lib.h"

struct CompileOpts {
  bool avoid_transmission_gates = false;
  std::optional<FpgaSpec> fpga_spec = std::nullopt;

  std::function<void()> callback = +[]() {};
};

Network Compile(GateNetwork& net, const CompileOpts& opts = {});

#endif
