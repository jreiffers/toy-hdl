#include "cpu/gate_text_format.h"

#include <gtest/gtest.h>

#include "cpu/gate_text_format_matchers.h"

TEST(GateTextFormatTest, ParseSingleUseNodes) {
  GateNetwork net;
  ParseNetwork(net, "a, b, c -> ~nand(a, b, ~c)");
  EXPECT_EQ(net.total_input_bits(), 3);
  EXPECT_EQ(net.num_outputs(), 1);
  EXPECT_THAT(net, NetworkIs("a, b, c -> ~nand(a, b, ~c)"));
}

TEST(GateTextFormatTest, ParseReusedNodes) {
  GateNetwork net;
  ParseNetwork(net, "a, b, c -> let d = ~nand(a, b) in ~nor(d, ~nand(d, c))");
  auto* nor_gate = net.GetOutput(0)[0].first->input(0).first;
  auto d = nor_gate->input(0);
  EXPECT_EQ(d, nor_gate->input(1).first->input(0).first->input(0));
  EXPECT_THAT(
      net, NetworkIs("a, b, c -> let d = ~nand(a, b) in ~nor(d, ~nand(d, c))"));
}

TEST(GateTextFormatTest, ParseMultipleBindingsAndOutputs) {
  GateNetwork net;
  ParseNetwork(net, "x -> let y = ~x in let z = nand(x, y) in (y, z, y)");
  ASSERT_EQ(net.num_outputs(), 3);
  EXPECT_EQ(net.GetOutput(0)[0], net.GetOutput(2)[0]);
  EXPECT_EQ(net.GetOutput(1)[0].first->input(1), net.GetOutput(0)[0]);
  auto printed = PrintNetwork(net);
  GateNetwork reparsed;
  ParseNetwork(reparsed, printed);
  EXPECT_THAT(reparsed, NetworkIs(printed));
}

TEST(GateTextFormatTest, Lut) {
  GateNetwork net;
  auto in = net.AddInput<2>();
  auto& gate = net.AddGate(GateKind::kLookup,
                           {in[0], in[1], net.Not(in[0]), net.Not(in[1])});
  gate.set_lookup_data(7);
  net.DeclareOutput(gate.output());
  auto printed = PrintNetwork(net);
  GateNetwork parsed;
  ParseNetwork(parsed, printed);
  EXPECT_THAT(parsed, NetworkIs("a, b -> lut[7](a, b, ~a, ~b)"));
}

TEST(GateTextFormatTest, PrintConstants) {
  GateNetwork net;
  ParseNetwork(net, "a -> (nand(a, 0), 0, 1)");
  auto printed = PrintNetwork(net);
  EXPECT_THAT(net, NetworkIs("a -> (nand(a, 0), 0, 1)"));
  GateNetwork parsed;
  ParseNetwork(parsed, printed);
  EXPECT_THAT(parsed, NetworkIs(printed));
}

TEST(GateTextFormatTest, RejectGarbageTokens) {
  GateNetwork net;
  ParseNetwork(net, "a, b -> nand(a, b)");
  EXPECT_THAT(net, ::testing::Not(NetworkIs("a, b -> nand(a, b) @")));
}
