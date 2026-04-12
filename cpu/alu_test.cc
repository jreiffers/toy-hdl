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

TEST(AluTest, Test) {
  GateNetwork net;
  auto a = net.AddInput<2>();
  auto b = net.AddInput<2>();
  auto carry_in = net.AddInput<1>();
  auto neg_b = net.AddInput<1>();
  Alu<2> alu = MakeAlu<2>(net, a, b, carry_in, neg_b);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.zero);

  FoldGates(net, FoldGatesOpts());

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
  Alu<2> alu = MakeAlu<2>(net, a, b, carry_in, neg_b);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.zero);

  ABSL_EXPECT_OK(
      VerifySpec(net, {net.GetOutput(0), net.GetOutput(1), net.GetOutput(2)},
                 Alu<2>::spec));

  auto transistor_net = Compile(net);
  ABSL_EXPECT_OK(
      VerifySpec(transistor_net, transistor_net.outputs(), Alu<2>::spec));
}
