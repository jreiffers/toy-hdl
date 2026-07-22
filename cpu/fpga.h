#ifndef FPGA_H__
#define FPGA_H__

#include "absl/types/span.h"
#include "cpu/gate_lib.h"

enum class FpgaResource {
  kIn,
  kNor,
  kNand,
  kLut2,
  kFF,
  kMux,
  kOut,
};

absl::Span<const FpgaResource> AllFpgaResources();

inline std::string to_string(FpgaResource resource) {
  switch (resource) {
    case FpgaResource::kIn:
      return "input";
    case FpgaResource::kNor:
      return "nor";
    case FpgaResource::kNand:
      return "nand";
    case FpgaResource::kLut2:
      return "lut";
    case FpgaResource::kFF:
      return "flipflop";
    case FpgaResource::kOut:
      return "output";
    case FpgaResource::kMux:
      return "mux";
  }
}

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
      case FpgaResource::kMux:
        return 3;
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

// FPGA is short for finger-programmable gate array. This is just one of
// each resource type.
template <int nor_arity>
struct FpgaGates {
  template <template <int> class Ty>
  struct Outs {
    Ty<2> in;
    Ty<2> nor;
    Ty<2> nand;
    Ty<2> lut;
    Ty<2> ff;
    Ty<1> out;
    Ty<2> mux;
  };

  template <template <int> class Ty>
  struct Args {
    Ty<1> clk;
    Ty<1> reset;

    Ty<1> input;
    Ty<nor_arity> nor_inputs;
    Ty<2> nand_inputs;
    Ty<2> lut_inputs;
    Ty<4> lut_bits;
    Ty<2> ff_inputs;
    Ty<2> tri_state_inputs;
    Ty<3> mux_inputs;  // sel, hi, lo
  };

  static Outs<GateReg> Build(GateNetwork& net, const Args<GateReg>& a) {
    Outs<GateReg> res;

    {
      ScopeGuard g(net, "input");
      res.in[0] = a.input[0];
      res.in[1] = net.Not(res.in[0]);
    }

    {
      ScopeGuard g(net, "nor");
      absl::InlinedVector<GateTerminal, 2> nor_ins;
      for (int i = 0; i < nor_arity; ++i) {
        nor_ins.push_back(a.nor_inputs[i]);
      }
      res.nor[0] = net.Nor(nor_ins);
      res.nor[1] = net.Not(res.nor[0]);
    }

    {
      ScopeGuard g(net, "nand");
      res.nand[0] = net.Nand({a.nand_inputs[0], a.nand_inputs[1]});
      res.nand[1] = net.Not(res.nand[0]);
    }

    {
      ScopeGuard g(net, "lut");
      res.lut[0] = net.Mux(
          a.lut_inputs[0],
          /*a*/
          net.Mux(a.lut_inputs[1], /*b*/ a.lut_bits[3], /*~b*/ a.lut_bits[2]),
          /*~a*/
          net.Mux(a.lut_inputs[1], /*b*/ a.lut_bits[1], /*~b*/ a.lut_bits[0]));
      res.lut[1] = net.Not(res.lut[0]);
    }

    {
      ScopeGuard g(net, "ff");
      auto write_data = net.Mux(a.ff_inputs[1], a.ff_inputs[0], a.ff_inputs[0]);
      res.ff = MakeDFlipFlop(net, write_data, a.clk, a.reset);
      write_data.first->SetInput(3, res.ff[0]);
    }

    {
      ScopeGuard g(net, "out");
      res.out =
          net.TriStateBuffer<1>(a.tri_state_inputs[0], a.tri_state_inputs[1]);
    }

    {
      ScopeGuard g(net, "mux");
      res.mux[0] = net.Mux(a.mux_inputs[0], a.mux_inputs[1], a.mux_inputs[2]);
      res.mux[1] = net.Not(res.mux[0]);
    }

    return res;
  }
};

#endif  // FPGA_H__
