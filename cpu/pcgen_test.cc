#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "compiler.h"
#include "eval.h"
#include "pc_gen.h"

using ::testing::ElementsAre;
using ::testing::Optional;

TEST(PcGenTest, TestSpec1) {
  GateNetwork net;
  auto pc = net.AddInput<1>();
  auto do_jump = net.AddInput<1>();
  auto jump_addr = net.AddInput<1>();
  PcGen<1> pcgen = MakePcGen<1>(net, pc, do_jump, jump_addr);
  net.DeclareOutput(pcgen.next_pc);

  auto transistor_net = Compile(net);
  ABSL_EXPECT_OK(
      VerifySpec(transistor_net, transistor_net.outputs(), PcGen<1>::spec));
}

TEST(PcGenTest, TestSpec) {
  GateNetwork net;
  auto pc = net.AddInput<5>();
  auto do_jump = net.AddInput<1>();
  auto jump_addr = net.AddInput<5>();
  PcGen<5> pcgen = MakePcGen<5>(net, pc, do_jump, jump_addr);
  net.DeclareOutput(pcgen.next_pc);

  auto transistor_net = Compile(net, [&]() {
    assert(VerifySpec(net, {net.GetOutput(0)}, PcGen<5>::spec).ok());
  });
  ABSL_EXPECT_OK(
      VerifySpec(transistor_net, transistor_net.outputs(), PcGen<5>::spec));
}

TEST(PcGenTest, TestGateSpec) {
  constexpr int bw = 1;
  GateNetwork net;
  auto pc = net.AddInput<bw>();
  auto do_jump = net.AddInput<1>();
  auto jump_addr = net.AddInput<bw>();
  PcGen<bw> pcgen = MakePcGen<bw>(net, pc, do_jump, jump_addr);
  net.DeclareOutput(pcgen.next_pc);

  ABSL_EXPECT_OK(VerifySpec(net, {net.GetOutput(0)}, PcGen<bw>::spec));
}
