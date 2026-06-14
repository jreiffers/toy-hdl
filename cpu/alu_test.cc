#include "alu.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "compiler.h"
#include "eval.h"
#include "gate_opt.h"
#include "lower_gates.h"
#include "transistor_lib.h"

using ::testing::ElementsAre;
using ::testing::Optional;

int CountLiveGates(GateNetwork& net) {
  int result = 0;
  net.WalkUnordered([&](int, Gate& g) { ++result; });
  return result;
}

TEST(AluTest, TestSpec) {
  GateNetwork net;
  auto a = net.AddInput<2>();
  auto b = net.AddInput<2>();
  auto c = net.AddInput<2>();
  auto a_enable = net.AddInput<1>();
  auto b_lut = net.AddInput<2>();
  auto c_enable = net.AddInput<1>();
  auto carry_in = net.AddInput<1>();
  auto compute_and = net.AddInput<1>();
  auto not_out = net.AddInput<1>();
  auto shr = net.AddInput<1>();
  Alu<2> alu = MakeAlu<2>(net, a, b, c, a_enable, b_lut, c_enable, carry_in,
                          compute_and, not_out, shr);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.zero);

  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true});

  ABSL_EXPECT_OK(
      VerifySpec(net, {net.GetOutput(0), net.GetOutput(1), net.GetOutput(2)},
                 Alu<2>::spec));

  auto transistor_net = Compile(net);
  ABSL_EXPECT_OK(
      VerifySpec(transistor_net, transistor_net.outputs(), Alu<2>::spec));
}
