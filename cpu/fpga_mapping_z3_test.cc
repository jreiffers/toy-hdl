#include "fpga_mapping_z3.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cpu/decoder.h"
#include "cpu/export.h"
#include "cpu/gate_opt.h"

using F = FpgaResource;

FpgaSpec SmallSpec() {
  return {
      .resources =
          {
              {F::kIn, F::kIn, F::kIn},
              {F::kNor, F::kLut2, F::kNor},
              {F::kOut, F::kOut, F::kOut},
          },
      .bus_width = 4,
      .nor_arity = 4,
  };
}

TEST(ClusteringTest, SimpleNorOr) {
  GateNetwork net;
  net.Build<Decoder>();

  constexpr int kNorArity = 4;

  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true,
                                        .maximum_nor_arity = kNorArity,
                                        .maximum_nand_arity = 2});
  // ABSL_EXPECT_OK(VerifySpec<Decoder>(net));

  FpgaSpec spec{
      .resources =
          {
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNor, F::kNand, F::kNor, F::kNand, F::kNor, F::kNor},
              //             {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNand, F::kNor, F::kNand, F::kNor, F::kNand, F::kNor},
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNor, F::kNor, F::kNand, F::kNor, F::kNor, F::kLut2},
              {F::kFF, F::kNor, F::kLut2, F::kNand, F::kLut2, F::kNor},
              {F::kOut, F::kOut, F::kOut, F::kOut, F::kOut, F::kOut},
          },
      .bus_width = 4,
      .nor_arity = kNorArity,
  };

  auto clusters = ClusterGates2(spec, net, 6);

  absl::flat_hash_map<GateTerminal, std::string> gate_colors;
  std::vector<std::string> colors = {"yellowgreen", "yellow",         "orange",
                                     "red",         "deeppink",       "purple",
                                     "blue",        "cornflowerblue", "aqua",
                                     "springgreen", "green"};

  for (int i = 0; i < std::min(colors.size(), clusters.size()); ++i) {
    for (auto t : clusters[i].gates) {
      if (t.first && t.first->kind() != GateKind::kNot) {
        gate_colors[t] = colors[i];
      }
    }
  }

  // print_graphviz(net, "", gate_colors);
}
