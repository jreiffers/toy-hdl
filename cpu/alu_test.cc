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

TEST(AluTest, Test) {
  GateNetwork net;
  auto a = net.AddInput<2>();
  auto b = net.AddInput<2>();
  auto carry_in = net.AddInput<1>();
  auto neg_b = net.AddInput<1>();
  Alu<2> alu = MakeAlu<2>(net, a, b, carry_in, neg_b, kLowGate, kLowGate,
                          kLowGate, kLowGate, kLowGate);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.zero);

  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true});
  EXPECT_EQ(CountLiveGates(net), 24);

  Network transistor_net = Lower(net);

  EXPECT_THAT(
      EvaluateAll(transistor_net, Flatten(transistor_net.outputs())),
      Optional(ElementsAre(
          /*neg_b=0*/
          /*carry_in=0*/
          0b1000, 0b0001, 0b0010, 0b0011, 0b0001, 0b0010, 0b0011, 0b1100,
          0b0010, 0b0011, 0b1100, 0b0101, 0b0011, 0b1100, 0b0101, 0b0110,
          /*carry_in=1*/
          0b0001, 0b0010, 0b0011, 0b1100, 0b0010, 0b0011, 0b1100, 0b0101,
          0b0011, 0b1100, 0b0101, 0b0110, 0b1100, 0b0101, 0b0110, 0b0111,
          /*neg_b=1*/
          /*carry_in=0*/
          0b0011, 0b1100, 0b0101, 0b0110, 0b0010, 0b0011, 0b1100, 0b0101,
          0b0001, 0b0010, 0b0011, 0b1100, 0b1000, 0b0001, 0b0010, 0b0011,
          /*carry_in=1*/
          0b1100, 0b0101, 0b0110, 0b0111, 0b0011, 0b1100, 0b0101, 0b0110,
          0b0010, 0b0011, 0b1100, 0b0101, 0b0001, 0b0010, 0b0011, 0b1100)));
}

TEST(AluTest, TestSpec) {
  GateNetwork net;
  auto a = net.AddInput<2>();
  auto b = net.AddInput<2>();
  auto carry_in = net.AddInput<1>();
  auto neg_b = net.AddInput<1>();
  auto compute_and = net.AddInput<1>();
  auto not_out = net.AddInput<1>();
  auto shr = net.AddInput<1>();
  auto zero_lhs = net.AddInput<1>();
  auto zero_rhs = net.AddInput<1>();
  Alu<2> alu = MakeAlu<2>(net, a, b, carry_in, neg_b, compute_and, not_out, shr,
                          zero_lhs, zero_rhs);
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
