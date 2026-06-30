#ifndef FPGA_H__
#define FPGA_H__

#include "cpu/gate_lib.h"

struct FpgaSpec {
  // A giant matrix with jumper positions for all of the in/out combinations
  // would be nice, but it'll probably be too large. Therefore, there's an
  // internal bus of configurable width instead (maybe later split into segments
  // with different lengths).
  int num_inputs;
  int num_nors;
  int num_ffs;
  int num_nands;
  int num_lut2s;
  int num_outputs;
  int bus_width;
  int nor_arity;

  constexpr int num_out_signals() const {
    // We have the complement of each signal.
    return (num_inputs + num_nors + num_ffs + num_nands + num_lut2s) * 2;
  }

  constexpr int num_in_signals() const {
    return nor_arity * num_nors + num_ffs * 2 /* write_enable, data */ +
           num_nands * 2 + num_lut2s * 2 + num_outputs * 2 /* enable, data */;
  }
};

// FPGA is short for finger-programmable gate array.
// The configuration "circuitry" isn't here; it'll be done in python.
template <FpgaSpec spec>
struct Fpga {
  template <template <int> class Ty>
  struct Outs {
    Ty<spec.num_inputs> not_inputs;

    Ty<spec.num_nors> nors;
    Ty<spec.num_nors> ors;

    Ty<spec.num_nands> nands;
    Ty<spec.num_nands> ands;

    Ty<spec.num_lut2s> luts;
    Ty<spec.num_lut2s> not_luts;
  };

  template <template <int> class Ty>
  struct Args {
    Ty<spec.num_inputs> inputs;
    Ty<spec.num_nors * spec.nor_arity> nor_ins;
    Ty<spec.num_nands * 2> nand_ins;
    Ty<1> clk;
    Ty<1> reset;
    Ty<spec.num_ffs> write_enable;
    Ty<spec.num_ffs> flipflop_vals;
    Ty<spec.num_lut2s * 4> lut_bits;
    Ty<spec.num_lut2s * 2> lut_ins;
    Ty<spec.num_outputs> output_enable;
    Ty<spec.num_outputs> output_data;
  };

  static Outs<GateReg> Build(GateNetwork& net, const Args<GateReg>& a) {
    Outs<GateReg> res;
    return res;
  }
};

#endif  // FPGA_H__
