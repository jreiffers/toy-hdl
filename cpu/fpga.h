#ifndef FPGA_H__
#define FPGA_H__

#include "cpu/gate_lib.h"

enum class FpgaResource {
  kIn,    // 2
  kNor,   // nor_arity * 2 + 2
  kNand,  // 6
  kLut2,  // 14
  kFF,    // 12?
  kOut,   // 12

  // TODO add a couple muxes
};

//
struct FpgaSpec {
  std::vector<std::vector<FpgaResource>> resources;
  int bus_width;  // horizontal / vertical, per col/row.
  int nor_arity;

  int rows() const { return resources.size(); }
  int cols() const { return resources.front().size(); }

  int arity(FpgaResource res) const {
    switch (res) {
      case FpgaResource::kIn:
        return 0;
      case FpgaResource::kNor:
        return nor_arity;
      default:
        return 2;
    }
  }

  int capacity(FpgaResource res) const {
    int count = 0;
    for (const auto& row : resources) {
      for (auto col : row) {
        if (col == res) ++count;
      }
    }
    return count;
  }
};

/*
// FPGA is short for finger-programmable gate array.
// The configuration "circuitry" isn't here; it'll be done in python.
template <FpgaSpec spec>
struct Fpga {
  template <template <int> class Ty>
  struct Outs {
    Ty<spec.capacity(FpgaResource::kInput> not_inputs;

    Ty<spec.capacity(FpgaResource::kNorGate)> nors;
    Ty<spec.capacity(FpgaResource::kNorGate)> ors;

    Ty<spec.capacity(FpgaResource::kNandGate)> nands;
    Ty<spec.capacity(FpgaResource::kNandGate)> ands;

    Ty<spec.capacity(FpgaResource::kLut2Gate)> luts;
    Ty<spec.capacity(FpgaResource::kLut2Gate)> not_luts;
  };

  template <template <int> class Ty>
  struct Args {
    Ty<1> clk;
    Ty<1> reset;
    Ty<spec.capacity(FpgaResource::kInput)> inputs;
    Ty<spec.capacity(FpgaResource::kNorGate) * spec.nor_arity> nor_ins;
    Ty<spec.capacity(FpgaResource::kNandGate) * 2> nand_ins;
    Ty<spec.capacity(FpgaResource::kFlipFlop)> write_enable;
    Ty<spec.capacity(FpgaResource::kFlipFlop)> flipflop_vals;
    Ty<spec.capacity(FpgaResource::kLut2Gate) * 4> lut_bits;
    Ty<spec.capacity(FpgaResource::kLut2Gate) * 2> lut_ins;
    Ty<spec.capacity(FpgaResource::kOutput)> output_enable;
    Ty<spec.capacity(FpgaResource::kOutput)> output_data;
  };

  static Outs<GateReg> Build(GateNetwork& net, const Args<GateReg>& a) {
    Outs<GateReg> res;
    return res;
  }
};
*/

#endif  // FPGA_H__
