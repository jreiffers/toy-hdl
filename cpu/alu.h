#ifndef ALU_H__
#define ALU_H__

#include "cpu/gate_lib.h"
#include "cpu/types.h"

template <template <int> class Ty>
struct AluFlags {
  Ty<1> a_enable;
  Ty<2> b_lut;
  Ty<1> c_enable;

  Ty<1> carry_in;
  Ty<1> compute_and;
  Ty<1> not_out;
  Ty<1> shr;
};

template <int bw>
struct Alu {
  template <template <int> class Ty>
  struct Outs {
    Ty<bw> res;
    Ty<1> carry_out;
    Ty<1> zero;
  };

  template <template <int> class Ty>
  struct Args {
    Ty<bw> a;
    Ty<bw> b;
    Ty<bw> c;
    AluFlags<Ty> flags;
  };

  static Outs<Integer> spec(Args<Integer> args) {
    const auto& flags = args.flags;

    Integer<bw> lhs = flags.a_enable.value() ? args.a : 0;
    Integer<bw> rhs = 0;

    Integer<bw> b = flags.c_enable.value() ? args.c : args.b;
    switch (flags.b_lut.value()) {
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
        rhs = ~rhs;
        break;
    }

    Integer<bw + 1> res(flags.compute_and.value() ? (lhs & rhs).value()
                                                  : (lhs.value() + rhs.value() +
                                                     flags.carry_in.value()));
    if (flags.not_out.value()) res = ~res;
    res = res >> flags.shr.value();

    auto val = res.template Slice<0, bw>();
    return {val, res[bw], val == 0};
  }

  static Outs<GateReg> Build(GateNetwork& net, const Args<GateReg>& a) {
    ScopeGuard scope(net, "alu");
    const auto& f = a.flags;
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
    Outs<GateReg> alu;
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
