#include "cpu/fpga_mapping.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/compiler.h"
#include "cpu/decoder.h"
#include "cpu/eval.h"
#include "cpu/fpga_mapping_z3.h"
#include "cpu/gate_opt.h"

using F = FpgaResource;

FpgaSpec SmallSpec() {
  return {
      .resources =
          {
              {F::kIn, F::kIn, F::kIn},
              {F::kNor, F::kNand, F::kNor},
              {F::kOut, F::kOut, F::kOut},
          },
      .bus_width = 2,
      .nor_arity = 3,
  };
}

TEST(RouteClusterTest, SimpleNor) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});

  auto spec = SmallSpec();

  GateCluster cluster{
      .inputs = {in[0], in[1], in[2]}, .gates = {nor}, .outputs = {nor}};
  auto placed = RouteCluster(spec, cluster, net);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->Build().to_ascii() << "\n";
  EXPECT_EQ(placed->Build().to_ascii(), R"(
         A  A   A
        ..  ..  ..
        ..  ..  ..
         |  |   |
        Aa  |   |
       b..--+.  ..
       c..--..--+.
        |
        a
        ..  ..  ..
        ..  ..  ..)");
}

TEST(RouteClusterTest, ReserveLut) {
  GateNetwork net;
  auto in = net.AddInput<2>();
  auto lookup = net.Xor(in[0], in[1]);
  auto nor = net.Nor({lookup, in[0]});
  net.DeclareOutput(nor);
  FpgaSpec spec{.resources = {{F::kIn, F::kIn},
                              {F::kLut2, F::kNor},
                              {F::kOut, F::kNand}},
                .bus_width = 4,
                .nor_arity = 4};
  GateCluster cluster{
      .inputs = {in[0], in[1]}, .gates = {lookup, nor}, .outputs = {nor}};
  // If we didn't reserve the lut, we'd place the nor in its slot (because
  // it's closer to the output), causing routing to fail.
  auto routed = RouteCluster(spec, cluster, net);
  ASSERT_TRUE(routed);
  EXPECT_EQ(routed->num_used(F::kLut2), 1);
  EXPECT_EQ(routed->num_used(F::kNor), 1);
  routed->Build();
}

TEST(RouteClusterTest, NorIntoNand) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});
  auto nand = net.Nand({nor, in[2]});

  auto spec = SmallSpec();

  GateCluster cluster{
      .inputs = {in[0], in[1], in[2]}, .gates = {nor, nand}, .outputs = {nand}};
  auto placed = RouteCluster(spec, cluster, net);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->Build().to_ascii() << "\n";
  EXPECT_EQ(placed->Build().to_ascii(), R"(
        A       A
        .+-A..  ..
        ..  ..  ..
        ||      |
        ab  A   |
       A..-a..  ..
       c..-b..--+.
            |
            a
        ..  ..  ..
        ..  ..  ..)");
}

TEST(RouteClusterTest, NorIntoNor) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});
  auto nor2 = net.Nor({nor, in[0]});

  auto spec = SmallSpec();
  // Leave enough lanes for the internal signals and the cluster output.
  spec.bus_width = 3;

  GateCluster cluster{
      .inputs = {in[0], in[1], in[2]}, .gates = {nor, nor2}, .outputs = {nor2}};
  auto placed = RouteCluster(spec, cluster, net);
  ASSERT_TRUE(placed.has_value());

  EXPECT_EQ(placed->num_used(F::kIn), 3);
  EXPECT_EQ(placed->num_used(F::kNor), 2);
  EXPECT_EQ(placed->num_used(F::kOut), 1);

  EXPECT_EQ(placed->Build().to_ascii(), R"(
                  A
        ... A...--.+.
       A...--...--..+
        ...  ...  ...
                  |||
        A         |bc
       a...--...-A...
       b...--...-a+..
        ...  ...  ...
        |
        a
        ...  ...  ...
        ...  ...  ...
        ...  ...  ...)");
}

TEST(RouteClusterTest, Complements) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({net.Not(in[0]), net.Not(in[1]), in[2]});
  auto nor2 = net.Nor({net.Not(nor), in[0]});

  auto spec = SmallSpec();

  GateCluster cluster{
      .inputs = {in[0], in[1], in[2]}, .gates = {nor, nor2}, .outputs = {nor2}};
  auto placed = RouteCluster(spec, cluster, net);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->Build().to_ascii() << "\n";
  EXPECT_EQ(placed->Build().to_ascii(), R"(
         A  A    B
       B..--..--+.
        ..  ..  ..
         |  |   ||
        Ab  |   ab
       a..--..-B..
        ..  +.-c..
        |
        a
        ..  ..  ..
        ..  ..  ..)");
}

TEST(FpgaMappingTest, Decoder) {
  GateNetwork net;
  net.Build<Decoder>();

  constexpr int kNorArity = 4;

  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true,
                                        .maximum_nor_arity = kNorArity,
                                        .maximum_nand_arity = 2});
  ABSL_EXPECT_OK(VerifySpec<Decoder>(net));

  FpgaSpec spec{
      .resources =
          {
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kFF, F::kNand, F::kNor, F::kNand, F::kNor, F::kNor},
              {F::kNand, F::kNor, F::kNand, F::kNor, F::kNand, F::kNor},
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNand, F::kNor, F::kNor, F::kNor, F::kNor, F::kLut2},
              {F::kNor, F::kNor, F::kLut2, F::kNand, F::kLut2, F::kNor},
              {F::kOut, F::kOut, F::kOut, F::kOut, F::kOut, F::kOut},
          },
      .bus_width = 4,
      .nor_arity = kNorArity,
  };

  auto clusters = ClusterGates(spec, net, 8);
  ASSERT_FALSE(clusters.empty());
  int routed_clusters = 0;
  for (const auto& cluster : clusters) {
    auto builder = RouteCluster(spec, cluster, net);
    if (builder) {
      ++routed_clusters;
      std::cerr << builder->Build().to_ascii() << "\n\n";
    }
  }
  // TODO do something meaningful
  EXPECT_GT(routed_clusters, 0);
}
