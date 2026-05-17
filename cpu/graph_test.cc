#include "cpu/graph.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>

#include "absl/strings/str_join.h"
#include "cpu/alu.h"
#include "cpu/gate_opt.h"
#include "cpu/register_file.h"

namespace graph {
namespace {

GateTerminal Find(
    GateNetwork& net, std::string_view scope, GateKind kind,
    const std::function<bool(Gate&)>& filter = [](Gate& g) { return true; }) {
  std::optional<GateTerminal> res = std::nullopt;
  net.WalkUnordered([&](int, Gate& gate) {
    if (gate.kind() == kind && absl::StrJoin(gate.scope(), "/") == scope &&
        filter(gate)) {
      if (res) ADD_FAILURE() << "More than one gate found.";
      res = gate.output();
    }
  });
  if (!res)
    ADD_FAILURE() << "No gate found for " << scope << ", type "
                  << to_string(kind) << ".";
  return *res;
}

TEST(GraphTest, TestRegisterSccs) {
  GateNetwork net;
  auto reset = net.AddInput<1>();
  auto clk = net.AddInput<1>();
  auto addr = net.AddInput<2>();
  auto rda1 = net.AddInput<2>();
  auto rda2 = net.AddInput<2>();
  auto wa = net.AddInput<2>();
  auto data = net.AddInput<4>();

  auto out = MakeRegister(net, reset, clk, addr, rda1, rda2, wa, data);
  net.DeclareOutput(out.read_port_1);
  net.DeclareOutput(out.read_port_2);

  Sccs sccs(net);
  ASSERT_EQ(sccs.sccs().size(), 4);

  for (const auto& scc : sccs.sccs()) {
    EXPECT_EQ(scc.members.size(), 7);  // flip flop + mux
    EXPECT_EQ(scc.sinks.size(), 5);    // mux, 3/4 stage0, 1/2 stage1.
    EXPECT_EQ(scc.sources.size(), 1);  // 1/2 stage1
  }
}

TEST(GraphTest, TestAlu) {
  GateNetwork net;

  GateReg<1> cin = net.AddInput<1>();
  GateReg<1> neg_b = net.AddInput<1>();
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> b = net.AddInput<1>();

  Alu<1> alu = MakeAlu<1>(net, a, b, cin, neg_b, kLowGate, kLowGate, kLowGate);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.zero);

  // Toposort/PDT don't support constant inputs.
  FoldGates(net, {});

  auto carry_out = net.GetOutput(0)[0];

  graph::TopoSort toposort(net, net.all_inputs(), {net.sink()});
  graph::PostDominatorTree pdt(net, toposort);
  pdt.dump();

  GateTerminal pick_mux = Find(net, "alu/pick_b/mux/bit0", GateKind::kMux);
  GateTerminal pick_not_b = Find(net, "alu/pick_b", GateKind::kNot,
                                 [&](Gate& g) { return g.input(0) == b[0]; });
  GateTerminal abc_nand = Find(net, "alu/adder/bit0/abc", GateKind::kNand);

  EXPECT_FALSE(pdt.Check(/*dominator=*/carry_out, a[0]));
  EXPECT_FALSE(pdt.Check(/*dominator=*/carry_out, b[0]));
  EXPECT_FALSE(pdt.Check(/*dominator=*/pick_not_b, pick_mux));
  EXPECT_FALSE(pdt.Check(/*dominator=*/carry_out, pick_not_b));

  EXPECT_TRUE(pdt.Check(/*dominator=*/pick_mux, pick_not_b));
  EXPECT_TRUE(pdt.Check(/*dominator=*/pick_mux, neg_b[0]));
  EXPECT_TRUE(pdt.Check(/*dominator=*/carry_out, abc_nand));
}

TEST(GraphTest, TestChain) {
  GateNetwork net;

  GateReg<1> in = net.AddInput<1>();
  auto n1 = net.Not(in);
  auto n2 = net.Not(n1);
  auto n3 = net.Not(n2);
  net.DeclareOutput(n3);

  graph::TopoSort toposort(net, net.all_inputs(), {net.sink()});
  graph::PostDominatorTree pdt(net, toposort);

  pdt.dump();

  std::vector<GateTerminal> ts{in[0], n1[0], n2[0], n3[0]};
  for (int i = 0; i < ts.size(); ++i) {
    for (int j = 0; j < ts.size(); ++j) {
      EXPECT_TRUE(pdt.Check(/*dominator=*/ts[i], ts[j]) == (i >= j))
          << i << ", " << j;
    }
  }
}

}  // namespace
}  // namespace graph
