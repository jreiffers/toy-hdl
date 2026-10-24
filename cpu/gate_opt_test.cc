#include "cpu/gate_opt.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cpu/fpga.h"
#include "cpu/gate_text_format.h"
#include "cpu/gate_text_format_matchers.h"

void OptNoTG(GateNetwork& net) {
  CseGates(net);
  FoldGatesOpts fold_opts;
  fold_opts.lower_mux = true;
  FoldGates(net, fold_opts);
  CseGates(net);
  MergeGates(net);
}

TEST(GateOptTest, OptimizeNfNandInNor) {
  GateNetwork net;
  ParseNetwork(net, "a, b, c -> nor(c, nand(a, ~b))");

  EXPECT_TRUE(OptimizeNF(net));
  FoldGates(net, {});
  EXPECT_THAT(net, NetworkIs("a, b, c -> nor(c, ~a, b)"));
  EXPECT_FALSE(OptimizeNF(net));
}

TEST(GateOptTest, OptimizeNfMultiUseNand) {
  GateNetwork net;
  ParseNetwork(net, "a, b, c -> let d = nand(a, ~b) in (d, nor(c, d))");

  EXPECT_FALSE(OptimizeNF(net));
  EXPECT_THAT(net, NetworkIs(R"(a, b, c ->
    let d = nand(a, ~b) in (d, nor(c, d)))"));
}

TEST(GateOptTest, OptimizeNfNorInNand) {
  GateNetwork net;
  ParseNetwork(net, "a, b, c -> nand(c, nor(a, ~b))");

  EXPECT_TRUE(OptimizeNF(net));
  FoldGates(net, {});
  EXPECT_THAT(net, NetworkIs("a, b, c -> nand(c, ~a, b)"));
  EXPECT_FALSE(OptimizeNF(net));
}

TEST(GateOptTest, OptimizeNfVsFactoring) {
  GateNetwork net;
  ParseNetwork(net, R"(a, b, c, d, e, f, g, h ->
    (nor(a, b, c, d, e, f), nor(a, b, c, d, e, g), nor(a, b, c, d, e, h)))");
  RunGateOptPipeline(net, {});
  EXPECT_THAT(net, NetworkIs(R"(a, b, c, d, e, f, g, h ->
                 let i = nor(e, d, c, b, a) in
                 (~nand(i, ~f), ~nand(i, ~g), ~nand(i, ~h)))"));
  EXPECT_FALSE(RunGateOptPipeline(net, {}));
}

class OptimizeNfAbsorptionTest
    : public ::testing::TestWithParam<
          std::pair<std::string_view, std::string_view>> {};

TEST_P(OptimizeNfAbsorptionTest, Simplifies) {
  const auto& [source, expected] = GetParam();
  GateNetwork net;
  ParseNetwork(net, source);
  EXPECT_EQ(OptimizeNF(net), source != expected);
  FoldGates(net, {});
  EXPECT_THAT(net, NetworkIs(expected));
  EXPECT_FALSE(OptimizeNF(net));
}

INSTANTIATE_TEST_SUITE_P(
    GateOpt, OptimizeNfAbsorptionTest,
    ::testing::ValuesIn(
        std::vector<std::pair<std::string_view, std::string_view>>{
            {"a, b, c -> nand(nand(a, b), nand(a, b, c))",
             "a, b, c -> ~nand(a, b)"},
            {"a, b, c -> nor(nor(a, b), nor(a, b, c))",
             "a, b, c -> ~nor(a, b)"},
            {"a, b, c -> nand(nand(~a, b), nand(~a, b, c))",
             "a, b, c -> ~nand(~a, b)"},
            {"a, b -> nand(nand(a, b), ~a)", "a, b -> a"},
            {"a, b -> nor(nor(a, b), ~a)", "a, b -> a"},
            {"a, b, c -> nand(nand(a, b), nand(a, ~b), nand(a, c))",
             "a, b, c -> a"},
            {"a, b, c -> nand(nand(a, b), nand(~a, b, c))",
             "a, b, c -> nand(nand(a, b), nand(~a, b, c))"}}));

TEST(GateOptTest, MuxToXor) {
  GateNetwork net;
  ParseNetwork(net, "a, s -> mux(s, ~s, ~a, a)");

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "xor");
  EXPECT_EQ(to_string(out.input(0)), "input[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[1]");
}

TEST(GateOptTest, MuxToEq) {
  GateNetwork net;
  ParseNetwork(net, "a, s -> mux(s, ~s, a, ~a)");

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "eq");
  EXPECT_EQ(to_string(out.input(0)), "input[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[1]");
}

TEST(GateOptTest, RegressionTestNoExistingNot) {
  GateNetwork net;
  ParseNetwork(net,
               "a, b, c -> let x = nand(a, b) in "
               "nand(nand(c, x), ~nor(c, x))");

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "eq");
  EXPECT_EQ(to_string(out.input(0)), "nand[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[2]");
  EXPECT_EQ(to_string(out.input(2)), "not[0]");
  EXPECT_EQ(to_string(out.input(3)), "not[0]");
}

TEST(GateOptTest, LowerOversizedFpgaNand) {
  auto lower = [](int arity, std::string_view expected) {
    SCOPED_TRACE(arity);
    GateNetwork net;
    auto in = net.AddInput<9>();
    absl::InlinedVector<GateTerminal, 2> inputs;
    for (int i = 0; i < arity; ++i) inputs.push_back(in[i]);
    auto nand = net.Nand(inputs);
    net.DeclareOutput(nand);
    net.DeclareOutput(net.Not(nand));
    FpgaSpec spec{.bus_width = 4, .nor_arity = 4};

    EXPECT_TRUE(LowerFpgaGates(net, spec));
    EXPECT_THAT(net, NetworkIs(expected));
  };

  lower(3,
        R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(~a, ~b, ~c) in
                 (~j, j))");
  lower(4,
        R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(~a, ~b, ~c, ~d) in
                 (~j, j))");
  lower(5,
        R"(a, b, c, d, e, f, g, h, i ->
                 let j = nand(nor(~a, ~b, ~c, ~d), e) in
                 (j, ~j))");
  lower(9,
        R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(nand(nor(~a, ~b, ~c, ~d), nor(~e, ~f, ~g, ~h)), ~i) in
                 (~j, j))");
}
