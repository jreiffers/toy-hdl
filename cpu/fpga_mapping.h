#ifndef FPGA_MAPPING_H__
#define FPGA_MAPPING_H__

#include <bitset>
#include <vector>

#include "cpu/fpga.h"
#include "cpu/gate_lib.h"

template <FpgaSpec spec>
struct FpgaMapping {
  struct FpgaChipConfig {
    // outputs -> bus
    std::array<std::bitset<spec.bus_width>, spec.num_out_signals()> outputs;

    // bus -> inputs
    std::array<std::bitset<spec.bus_width>, spec.num_in_signals()> inputs;

    std::array<std::bitset<4>, spec.num_lut2s> luts;
  };

  std::vector<FpgaChipConfig> chips;

  static FpgaMapping Map(const GateNetwork& net) { return {}; }
};

#endif
