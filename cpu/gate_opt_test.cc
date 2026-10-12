#include "cpu/gate_opt.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cpu/export.h"
#include "cpu/fpga.h"

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
  auto in = net.AddInput<3>();
  auto a = in[0], b = in[1], c = in[2];
  net.DeclareOutput(net.Nor({c, net.Nand({a, net.Not(b)})}));

  EXPECT_TRUE(OptimizeNF(net));
  FoldGates(net, {});
  EXPECT_EQ(PrintNetwork(net, 14), R"(a, b, c ->
              nor(c, ~a, b))");
  EXPECT_FALSE(OptimizeNF(net));
}

TEST(GateOptTest, OptimizeNfMultiUseNand) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto shared = net.Nand({in[0], net.Not(in[1])});
  net.DeclareOutput(shared);
  net.DeclareOutput(net.Nor({in[2], shared}));

  EXPECT_FALSE(OptimizeNF(net));
  EXPECT_EQ(PrintNetwork(net, 17),
            R"(a, b, c ->
                 let d = nand(a, ~b) in
                 (d, nor(c, d)))");
}

TEST(GateOptTest, OptimizeNfNorInNand) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto a = in[0], b = in[1], c = in[2];
  net.DeclareOutput(net.Nand({c, net.Nor({a, net.Not(b)})}));

  EXPECT_TRUE(OptimizeNF(net));
  FoldGates(net, {});
  EXPECT_EQ(PrintNetwork(net, 14), R"(a, b, c ->
              nand(c, ~a, b))");
  EXPECT_FALSE(OptimizeNF(net));
}

TEST(GateOptTest, OptimizeNfVsFactoring) {
  GateNetwork net;
  auto in = net.AddInput<8>();
  for (int i = 5; i < 8; ++i)
    net.DeclareOutput(net.Nor({in[0], in[1], in[2], in[3], in[4], in[i]}));
  RunGateOptPipeline(net, {});
  EXPECT_EQ(PrintNetwork(net, 17),
            R"(a, b, c, d, e, f, g, h ->
                 let i = nor(e, d, c, b, a) in
                 (~nand(i, ~f), ~nand(i, ~g), ~nand(i, ~h)))");
  EXPECT_FALSE(RunGateOptPipeline(net, {}));
}

TEST(GateOptTest, MuxToXor) {
  GateNetwork net;
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> sel = net.AddInput<1>();

  GateReg<1> not_a = net.Not(a);
  net.DeclareOutput(net.Mux(sel[0], not_a, a));

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "xor");
  EXPECT_EQ(to_string(out.input(0)), "input[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[1]");
}

TEST(GateOptTest, MuxToEq) {
  GateNetwork net;
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> sel = net.AddInput<1>();

  GateReg<1> not_a = net.Not(a);
  net.DeclareOutput(net.Mux(sel[0], a, not_a));

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "eq");
  EXPECT_EQ(to_string(out.input(0)), "input[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[1]");
}

TEST(GateOptTest, RegressionTestNoExistingNot) {
  GateNetwork net;
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> b = net.AddInput<1>();
  GateReg<1> c = net.AddInput<1>();

  GateTerminal x = net.Nand({a, b});
  GateTerminal r = net.Nand({net.Nand({c, x}), net.Not(net.Nor({c, x}))});
  net.DeclareOutput(r);

  OptNoTG(net);

  Gate& out = *net.GetOutput(0)[0].first;
  EXPECT_EQ(to_string(out), "eq");
  EXPECT_EQ(to_string(out.input(0)), "nand[0]");
  EXPECT_EQ(to_string(out.input(1)), "input[2]");
  EXPECT_EQ(to_string(out.input(2)), "not[0]");
  EXPECT_EQ(to_string(out.input(3)), "not[0]");
}

TEST(GateOptTest, LowerOversizedFpgaNand) {
  auto lower = [](int arity) {
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
    return PrintNetwork(net, 17);
  };

  EXPECT_EQ(lower(3),
            R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(~a, ~b, ~c) in
                 (~j, j))");
  EXPECT_EQ(lower(4),
            R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(~a, ~b, ~c, ~d) in
                 (~j, j))");
  EXPECT_EQ(lower(5),
            R"(a, b, c, d, e, f, g, h, i ->
                 let j = nand(nor(~a, ~b, ~c, ~d), e) in
                 (j, ~j))");
  EXPECT_EQ(lower(9),
            R"(a, b, c, d, e, f, g, h, i ->
                 let j = nor(nand(nor(~a, ~b, ~c, ~d), nor(~e, ~f, ~g, ~h)), ~i) in
                 (~j, j))");
}
