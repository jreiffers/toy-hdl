#include "compiler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "eval.h"

template <int bw>
absl::Status VerifyAdd1() {
  GateNetwork net;
  GateReg<bw> zero;
  for (int i = 0; i < bw; ++i) {
    zero[i] = kLowGate;
  }

  GateReg<bw> in = net.AddInput<bw>();
  GateReg<bw> out = MakeAdder(net, in, zero, /*carry_in=*/kHighGate).first;
  net.DeclareOutput(out);

  auto compiled = Compile(net);
  return VerifySpec(compiled, compiled.outputs(),
                    [](absl::Span<const uint32_t> inputs) {
                      return absl::InlinedVector<uint32_t, 4>{(inputs[0] + 1) &
                                                              ((1 << bw) - 1)};
                    });
}

TEST(CompilerTest, RegressionTestAdd1_1) { ABSL_EXPECT_OK(VerifyAdd1<1>()); }

TEST(CompilerTest, RegressionTestAdd1_2) { ABSL_EXPECT_OK(VerifyAdd1<2>()); }

TEST(CompilerTest, RegressionTestAdd1_3) { ABSL_EXPECT_OK(VerifyAdd1<3>()); }

TEST(CompilerTest, RegressionTestAdd1_4) { ABSL_EXPECT_OK(VerifyAdd1<4>()); }

absl::Status VerifyMux(const CompileOpts& opts, int expected_transistors) {
  GateNetwork net;
  GateReg<1> sel = net.AddInput<1>();
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> b = net.AddInput<1>();
  GateTerminal out = net.Mux(sel[0], a[0], b[0]);
  net.DeclareOutput(DynGateReg({out}));

  auto compiled = Compile(net, opts);
  if (compiled.num_transistors() != expected_transistors) {
    return absl::InternalError(
        absl::StrCat("Unexpected number of transistors. ",
                     compiled.num_transistors(), " != ", expected_transistors));
  }

  return VerifySpec(
      compiled, compiled.outputs(), [](absl::Span<const uint32_t> inputs) {
        return absl::InlinedVector<uint32_t, 4>{inputs[0] ? inputs[1]
                                                          : inputs[2]};
      });
}

TEST(CompilerTest, LowerMux) {
  CompileOpts opts;
  opts.avoid_transmission_gates = false;
  ABSL_EXPECT_OK(VerifyMux(opts, 6));
}

TEST(CompilerTest, LowerMuxNoTg) {
  CompileOpts opts;
  opts.avoid_transmission_gates = true;
  ABSL_EXPECT_OK(VerifyMux(opts, 14));
}

TEST(CompilerTest, LowerFlipFlop) {
  GateNetwork net;
  auto d = net.AddInput<1>()[0];
  auto clk = net.AddInput<1>()[0];
  auto reset = net.AddInput<1>()[0];
  auto out = MakeDFlipFlop(net, d, clk, reset);
  net.DeclareOutput(out);
  // Just verify this doesn't crash for now. There's no stateful transistor
  // network evaluator yet.
  Compile(net);
}
