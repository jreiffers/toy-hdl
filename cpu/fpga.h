#ifndef FPGA_H__
#define FPGA_H__

#include "cpu/gate_lib.h"

enum class FpgaResource {
  kInput,
  kNorGate,
  kNandGate,
  kLut2Gate,
  kFlipFlop,
  kOutput,
};

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

  constexpr int capacity(FpgaResource res) const {
    switch (res) {
      case FpgaResource::kInput:
        return num_inputs;
      case FpgaResource::kNorGate:
        return num_nors;
      case FpgaResource::kNandGate:
        return num_nands;
      case FpgaResource::kLut2Gate:
        return num_lut2s;
      case FpgaResource::kFlipFlop:
        return num_ffs;
      case FpgaResource::kOutput:
        return num_outputs;
    }
  }

  constexpr int output_index(FpgaResource res, int res_index, bool neg) const {
    switch (res) {
      case FpgaResource::kInput:
        return 2 * res_index + neg;
      case FpgaResource::kNorGate:
        return output_index(FpgaResource::kInput, num_inputs, false) +
               2 * res_index + neg;
      case FpgaResource::kNandGate:
        return output_index(FpgaResource::kNorGate, num_nors, false) +
               2 * res_index + neg;
      case FpgaResource::kLut2Gate:
        return output_index(FpgaResource::kNandGate, num_nands, false) +
               2 * res_index + neg;
      case FpgaResource::kFlipFlop:
        return output_index(FpgaResource::kLut2Gate, num_lut2s, false) +
               2 * res_index + neg;
      case FpgaResource::kOutput:
        throw std::logic_error("Outputs don't have outputs.");
    }
  }

  constexpr int input_index(FpgaResource res, int res_index, int input) const {
    switch (res) {
      case FpgaResource::kInput:
        throw std::logic_error("Inputs don't have inputs.");
      case FpgaResource::kNorGate:
        return nor_arity * res_index + input;
      case FpgaResource::kNandGate:
        return input_index(FpgaResource::kNorGate, num_nors, 0) +
               2 * res_index + input;
      case FpgaResource::kLut2Gate:
        return input_index(FpgaResource::kNandGate, num_nands, 0) +
               2 * res_index + input;
      case FpgaResource::kFlipFlop:
        return input_index(FpgaResource::kLut2Gate, num_lut2s, 0) +
               2 * res_index + input;
      case FpgaResource::kOutput:
        return input_index(FpgaResource::kFlipFlop, num_ffs, 0) +
               2 * res_index + input;
    }
  }

  constexpr int num_out_signals() const {
    return output_index(FpgaResource::kFlipFlop, num_ffs, false);
  }

  constexpr int num_in_signals() const {
    return input_index(FpgaResource::kOutput, num_outputs, 0);
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
