#include "cpu/gate_opt.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/eval.h"
#include "cpu/export.h"

void OptNoTG(GateNetwork& net) {
  CseGates(net);
  FoldGatesOpts fold_opts;
  fold_opts.lower_mux = true;
  FoldGates(net, fold_opts);
  CseGates(net);
  MergeGates(net);
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
