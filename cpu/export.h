#ifndef EXPORT_H__
#define EXPORT_H__

#include <unordered_map>

#include "cpu/format.pb.h"
#include "cpu/gate_lib.h"
#include "cpu/transistor_lib.h"

void print_graphviz(const Network& net);
void print_graphviz(GateNetwork& net,
                    const std::unordered_map<GateTerminal, bool>& colors = {});
void print_ngspice(const Network& net, NodeId out);

// Prints the network in a custom ad-hoc netlist format.
toyhdl::serialization::Network ExportNetlist(const Network& net);

#endif
