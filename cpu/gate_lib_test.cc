#include "gate_lib.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
  GateTerminal in = net.AddInput<1>()[0];
  GateTerminal not_in = net.Not(in);
  net.DeclareOutput(DynGateReg({not_in}));

  EXPECT_EQ(net.GetOutput(0)[0], not_in);
  not_in.first->ReplaceAllUsesWith(in);

  EXPECT_EQ(net.GetOutput(0)[0], in);
}

TEST(GateLibTest, CanonicalizeLookup) {
  GateNetwork net;
  GateTerminal in = net.AddInput<1>()[0];
  Gate& lookup = *net.Xor(in, kHighGate).first;
  net.DeclareOutput(DynGateReg({lookup.output()}));

  ASSERT_EQ(lookup.kind(), GateKind::kLookup);
  ASSERT_TRUE(lookup.Canonicalize());

  ASSERT_EQ(lookup.num_inputs(), 2);
  EXPECT_EQ(lookup.input(0), in);
  EXPECT_EQ(lookup.lookup_data(), 2);
}

TEST(GateLibTest, CanonicalizeLookup2) {
  GateNetwork net;
  GateTerminal in = net.AddInput<1>()[0];
  Gate& lookup = *net.Xor(kHighGate, in).first;
  net.DeclareOutput(DynGateReg({lookup.output()}));

  ASSERT_EQ(lookup.kind(), GateKind::kLookup);
  ASSERT_TRUE(lookup.Canonicalize());

  ASSERT_EQ(lookup.num_inputs(), 2);
  EXPECT_EQ(lookup.input(0), in);
  EXPECT_EQ(lookup.lookup_data(), 2);
}

TEST(GateLibTest, CanonicalizeNand) {
  GateNetwork net;
  GateTerminal in = net.AddInput<1>()[0];
  Gate& nand = *net.Nand({in, kHighGate}).first;
  net.DeclareOutput(DynGateReg({nand.output()}));

  ASSERT_EQ(nand.kind(), GateKind::kNand);
  ASSERT_TRUE(nand.Canonicalize());

  ASSERT_EQ(nand.num_inputs(), 1);
  EXPECT_EQ(nand.input(0), in);
  EXPECT_EQ(nand.kind(), GateKind::kNot);
}

TEST(GateLibTest, DoNotCanonicalizeNand) {
  GateNetwork net;
  GateTerminal in = net.AddInput<1>()[0];
  Gate& nand = *net.Nand({in, kLowGate}).first;
  net.DeclareOutput(DynGateReg({nand.output()}));

  ASSERT_EQ(nand.kind(), GateKind::kNand);
  ASSERT_FALSE(nand.Canonicalize());
}

TEST(GateLibTest, CanonicalizeNor) {
  GateNetwork net;
  GateTerminal in = net.AddInput<1>()[0];
  Gate& nand = *net.Nor({in, kLowGate}).first;
  net.DeclareOutput(DynGateReg({nand.output()}));

  ASSERT_EQ(nand.kind(), GateKind::kNor);
  ASSERT_TRUE(nand.Canonicalize());

  ASSERT_EQ(nand.num_inputs(), 1);
  EXPECT_EQ(nand.input(0), in);
  EXPECT_EQ(nand.kind(), GateKind::kNot);
}
