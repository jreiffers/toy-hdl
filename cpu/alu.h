#ifndef ALU_H__
#define ALU_H__

#include "gate_lib.h"

struct AluFlags {
  GateReg<1> a_enable;
  GateReg<2> b_lut;
  GateReg<1> c_enable;

  GateReg<1> carry_in;
  GateReg<1> compute_and;
  GateReg<1> not_out;
  GateReg<1> shr;
};

template <int bw>
struct Alu {
  GateReg<bw> res;
  GateReg<1> carry_out;
  GateReg<1> zero;

  static absl::InlinedVector<uint32_t, 4> spec(
      absl::Span<const uint32_t> inputs) {
    constexpr uint32_t mask = ((1ull << bw) - 1);
    constexpr uint32_t wmask = mask | (1ull << bw);

    uint32_t lhs = inputs[3] ? inputs[0] : 0;
    uint32_t rhs = 0;

    uint32_t b = inputs[5] ? inputs[2] : inputs[1];
    switch (inputs[4]) {
      case 0:
        rhs = 0;
        break;
      case 1:
        rhs = ~b;
        break;
      case 2:
        rhs = b;
        break;
      case 3:
        rhs = mask;
        break;
    }
    rhs &= mask;

    uint32_t cin = inputs[6];
    bool compute_and = inputs[7];
    bool not_out = inputs[8];
    bool shr = inputs[9];

    uint32_t res = compute_and ? (lhs & rhs) : (lhs + rhs + cin);
    if (not_out) res = res ^ wmask;
    if (shr) {
      res >>= 1;
    }

    return {res & mask, res > mask, !(res & mask)};
  }

  struct Args {
    GateReg<bw> a;
    GateReg<bw> b;
    GateReg<bw> c;
    AluFlags flags;
  };

  static Alu Build(GateNetwork& net, const Args& a) {
    ScopeGuard scope(net, "alu");
    const AluFlags& f = a.flags;
    GateReg<bw> rhs;
    {
      ScopeGuard pick(net, "rhs");
      for (int i = 0; i < bw; ++i) {
        ScopeGuard bit(net, absl::StrCat("bit", i));
        rhs[i] = net.Mux(net.Mux(f.c_enable, a.c[i], a.b[i]), f.b_lut[1],
                         f.b_lut[0]);
      }
    }

    GateReg<bw> lhs;
    {
      ScopeGuard pick(net, "lhs");
      for (int i = 0; i < bw; ++i) {
        ScopeGuard bit(net, absl::StrCat("bit", i));
        lhs[i] = net.And(a.a[i], f.a_enable);
      }
    }

    GateReg<bw> sum;
    GateReg<bw> and_;
    GateReg<1> carry_out;

    std::tie(sum, carry_out[0]) = MakeAdder(net, lhs, rhs, f.carry_in[0]);

    for (int i = 0; i < bw; ++i) {
      ScopeGuard s1(net, "and");
      ScopeGuard s2(net, absl::StrCat("bit", i));
      and_[i] = net.And(lhs[i], rhs[i]);
    }

    // Carry out should never be used when shr / compute_and is active.
    // TODO: remove the parts that are unnecessary.
    Alu alu;
    alu.carry_out =
        net.Xor(net.And(carry_out, net.Not(f.compute_and)), f.not_out[0]);
    auto and_or_sum = net.Mux(f.compute_and, and_, sum);

    for (int i = 0; i < bw; ++i) {
      alu.res[i] = net.Xor(and_or_sum[i], f.not_out[0]);
    }

    for (int i = 0; i < bw; ++i) {
      GateTerminal hi_bit = i == bw - 1 ? alu.carry_out[0] : alu.res[i + 1];
      alu.res[i] = net.Mux(f.shr[0], hi_bit, alu.res[i]);
    }

    alu.carry_out = net.And(alu.carry_out, net.Not(f.shr));

    ScopeGuard zero(net, "is_zero");
    alu.zero[0] = net.Nor(alu.res.vals);
    return alu;
  }
};

#endif
