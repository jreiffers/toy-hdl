#ifndef EXPORT_H__
#define EXPORT_H__

#include <iostream>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "cpu/format.pb.h"
#include "cpu/gate_lib.h"
#include "cpu/transistor_lib.h"

void print_graphviz(const Network& net);
void print_graphviz(
    GateNetwork& net,
    const absl::flat_hash_map<GateTerminal, std::optional<bool>>& colors = {},
    std::ostream& stream = std::cout);
void print_ngspice(const Network& net, NodeId out);

// Prints the network in a custom ad-hoc netlist format.
toyhdl::serialization::Network ExportNetlist(const Network& net);

#endif
