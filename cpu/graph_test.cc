#include "cpu/graph.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>

#include "absl/strings/str_join.h"
#include "cpu/alu.h"

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

TEST(GraphTest, TestAlu) {
  GateNetwork net;

  GateReg<1> cin = net.AddInput<1>();
  GateReg<1> neg_b = net.AddInput<1>();
  GateReg<1> a = net.AddInput<1>();
  GateReg<1> b = net.AddInput<1>();

  Alu<1> alu = MakeAlu<1>(net, a, b, cin, neg_b);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.res);

  graph::TopoSort toposort(net, net.all_inputs(), {net.sink()});
  graph::PostDominatorTree pdt(net, toposort);
  pdt.dump();

  GateTerminal pick_mux = Find(net, "alu/pick_b/mux/bit0", GateKind::kMux);
  GateTerminal pick_not_b = Find(net, "alu/pick_b", GateKind::kNot,
                                 [&](Gate& g) { return g.input(0) == b[0]; });
  GateTerminal abc_nand = Find(net, "alu/adder/bit0/abc", GateKind::kNand);

  EXPECT_FALSE(pdt.Check(/*dominator=*/alu.carry_out[0], a[0]));
  EXPECT_FALSE(pdt.Check(/*dominator=*/alu.carry_out[0], b[0]));
  EXPECT_FALSE(pdt.Check(/*dominator=*/pick_not_b, pick_mux));
  EXPECT_FALSE(pdt.Check(/*dominator=*/alu.carry_out[0], pick_not_b));

  EXPECT_TRUE(pdt.Check(/*dominator=*/pick_mux, pick_not_b));
  EXPECT_TRUE(pdt.Check(/*dominator=*/pick_mux, neg_b[0]));
  EXPECT_TRUE(pdt.Check(/*dominator=*/alu.carry_out[0], abc_nand));
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
