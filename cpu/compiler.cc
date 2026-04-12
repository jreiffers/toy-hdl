#include "compiler.h"

#include <iostream>

#include "gate_opt.h"
#include "lower_gates.h"
#include "transistor_opt.h"

Network Compile(GateNetwork& net, const CompileOpts& opts) {
  CseGates(net);
  FoldGatesOpts fold_opts;
  fold_opts.lower_mux = opts.avoid_transmission_gates;
  fold_opts.callback = opts.callback;

  FoldGates(net, fold_opts);
  CseGates(net);
  Network transistor_net = Lower(net);
  std::cerr << "  Number of transistors: " << transistor_net.num_transistors()
            << "\n";
  return transistor_net;
}
