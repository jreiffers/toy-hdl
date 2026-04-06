#ifndef PRINT_H__
#define PRINT_H__

#include <unordered_map>

#include "gate_lib.h"
#include "transistor_lib.h"

void print_graphviz(const Network& net);
void print_graphviz(GateNetwork& net,
                    const std::unordered_map<GateTerminal, bool>& colors = {});
void print_ngspice(const Network& net, NodeId out);

#endif
