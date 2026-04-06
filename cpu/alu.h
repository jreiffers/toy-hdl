#ifndef ALU_H__
#define ALU_H__

#include "gate_lib.h"

template <int bw>
struct Alu {
  GateReg<bw> res;
  GateReg<1> carry_out;
  GateReg<1> zero;

  static absl::InlinedVector<uint32_t, 4> spec(
      absl::Span<const uint32_t> inputs) {
    constexpr uint32_t mask = ((1ull << bw) - 1);
    bool neg_b = inputs[3];
    uint32_t a = inputs[0];
    uint32_t b = (neg_b ? ~inputs[1] : inputs[1]) & mask;
    uint32_t res = a + b + inputs[2];
    return {res & mask, res > mask, !(res & mask)};
  }
};

template <int bw>
Alu<bw> MakeAlu(GateNetwork& net, GateReg<bw> a, GateReg<bw> b,
                GateReg<1> carry_in, GateReg<1> neg_b) {
  GateReg<bw> not_b = net.Not(b);
  GateReg<bw> picked_b = net.Mux(neg_b[0], not_b, b);

  Alu<bw> alu;
  std::tie(alu.res, alu.carry_out[0]) =
      MakeAdder(net, a, picked_b, carry_in[0]);
  alu.zero[0] = net.Nor(alu.res.vals);
  return alu;
}

#endif
