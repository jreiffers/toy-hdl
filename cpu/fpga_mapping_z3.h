#ifndef FPGA_MAPPING_Z3_H__
#define FPGA_MAPPING_Z3_H__

#include <set>
#include <vector>

#include "cpu/fpga_mapping.h"
#include "cpu/gate_lib.h"

// Cluster gates without checking feasibility of routing.
std::vector<GateCluster> ClusterGates(const FpgaSpec& spec, GateNetwork& net,
                                      int max_clusters);

#endif  // FPGA_MAPPING_Z3_H__
