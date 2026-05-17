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
    constexpr uint32_t wmask = mask | (1ull << bw);

    uint32_t a = inputs[0];
    uint32_t b = inputs[1];
    uint32_t cin = inputs[2];
    bool neg_b = inputs[3];
    bool compute_and = inputs[4];
    bool not_out = inputs[5];
    bool shr = inputs[6];

    if (neg_b) b = b ^ mask;

    uint32_t res = compute_and ? (a & b) : (a + b + cin);
    if (not_out) res = res ^ wmask;
    if (shr) {
      res >>= 1;
    }

    return {res & mask, res > mask, !(res & mask)};
  }
};

template <int bw>
Alu<bw> MakeAlu(GateNetwork& net, GateReg<bw> a, GateReg<bw> b,
                GateReg<1> carry_in, GateReg<1> not_b, GateReg<1> compute_and,
                GateReg<1> not_out, GateReg<1> shr) {
  // TODO: think about where to handle zero-lhs.
  ScopeGuard scope(net, "alu");
  GateReg<bw> picked_b;
  {
    ScopeGuard pick(net, "pick_b");
    picked_b = net.Mux(not_b[0], net.Not(b), b);
  }

  Alu<bw> alu;

  GateReg<bw> sum;
  GateReg<bw> and_;
  GateReg<1> carry_out;

  std::tie(sum, carry_out[0]) = MakeAdder(net, a, picked_b, carry_in[0]);

  for (int i = 0; i < bw; ++i) {
    ScopeGuard s1(net, "and");
    ScopeGuard s2(net, absl::StrCat("bit", i));
    and_[i] = net.And(a[i], picked_b[i]);
  }

  // Carry out should never be used when shr / compute_and is active.
  // TODO: remove the parts that are unnecessary.
  alu.carry_out = net.Xor(net.And(carry_out, net.Not(compute_and)), not_out[0]);
  auto and_or_sum = net.Mux(compute_and, and_, sum);

  for (int i = 0; i < bw; ++i) {
    alu.res[i] = net.Xor(and_or_sum[i], not_out[0]);
  }

  for (int i = 0; i < bw; ++i) {
    GateTerminal hi_bit = i == bw - 1 ? alu.carry_out[0] : alu.res[i + 1];
    alu.res[i] = net.Mux(shr[0], hi_bit, alu.res[i]);
  }

  alu.carry_out = net.And(alu.carry_out, net.Not(shr));

  ScopeGuard zero(net, "is_zero");
  alu.zero[0] = net.Nor(alu.res.vals);
  return alu;
}

#endif
