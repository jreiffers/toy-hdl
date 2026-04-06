#include "compiler.h"

#include <iostream>

#include "gate_opt.h"
#include "lower_gates.h"
#include "transistor_opt.h"

Network Compile(GateNetwork& net, const std::function<void()>& callback) {
  CseGates(net);
  FoldGates(net, callback);
  CseGates(net);
  Network transistor_net = Lower(net);
  std::cerr << "  Number of transistors: " << transistor_net.num_transistors()
            << "\n";
  return transistor_net;
}
