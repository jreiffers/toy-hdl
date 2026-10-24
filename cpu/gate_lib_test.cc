#include "gate_lib.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/eval.h"
#include "cpu/gate_text_format.h"
#include "cpu/gate_text_format_matchers.h"

using ::testing::ElementsAre;
using ::testing::Optional;

TEST(GateLibTest, DeclareOutputs) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  net.DeclareOutput(DynGateReg({in[1], in[2]}));
  net.DeclareOutput(DynGateReg({in[0]}));

  EXPECT_EQ(net.GetOutput(0)[0], in[1]);
  EXPECT_EQ(net.GetOutput(0)[1], in[2]);
  EXPECT_EQ(net.GetOutput(1)[0], in[0]);
}

TEST(GateLibTest, ReplaceOutput) {
  GateNetwork net;
  ParseNetwork(net, "a -> ~a");
  auto in = net.input_bit(0);
  auto not_in = net.GetOutput(0)[0];

  EXPECT_EQ(net.GetOutput(0)[0], not_in);
  not_in.first->ReplaceAllUsesWith(in);

  EXPECT_EQ(net.GetOutput(0)[0], in);
}

TEST(GateLibTest, CanonicalizeLookup) {
  GateNetwork net;
  ParseNetwork(net, "a -> lut[6](a, 1, ~a, 0)");
  ASSERT_TRUE(net.GetOutput(0)[0].first->Canonicalize());
  EXPECT_THAT(net, NetworkIs("a -> lut[1](a, ~a)"));
}

TEST(GateLibTest, CanonicalizeLookup2) {
  GateNetwork net;
  ParseNetwork(net, "a -> lut[6](1, a, 0, ~a)");
  ASSERT_TRUE(net.GetOutput(0)[0].first->Canonicalize());

  ABSL_EXPECT_OK(VerifySpec(
      net, net.GetOutputs(),
      [](absl::Span<const uint32_t> inputs)
          -> absl::InlinedVector<uint32_t, 4> { return {!inputs[0]}; }));

  EXPECT_THAT(net, NetworkIs("a -> lut[1](a, ~a)"));
}

TEST(GateLibTest, CanonicalizeNand) {
  GateNetwork net;
  ParseNetwork(net, "a -> nand(a, 1)");
  ASSERT_TRUE(net.GetOutput(0)[0].first->Canonicalize());
  EXPECT_THAT(net, NetworkIs("a -> ~a"));
}

TEST(GateLibTest, DoNotCanonicalizeNand) {
  GateNetwork net;
  ParseNetwork(net, "a -> nand(a, 0)");
  ASSERT_FALSE(net.GetOutput(0)[0].first->Canonicalize());
  EXPECT_THAT(net, NetworkIs("a -> nand(a, 0)"));
}

TEST(GateLibTest, CanonicalizeNor) {
  GateNetwork net;
  ParseNetwork(net, "a -> nor(a, 0)");
  ASSERT_TRUE(net.GetOutput(0)[0].first->Canonicalize());
  EXPECT_THAT(net, NetworkIs("a -> ~a"));
}
