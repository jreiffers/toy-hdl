#include "cpu/fpga_mapping.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/compiler.h"
#include "cpu/decoder.h"
#include "cpu/eval.h"
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

TEST(ChipBuilderTest, SimpleNor) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});

  auto spec = SmallSpec();
  ChipBuilder b(spec);

  auto placed = b.TryPlace(net, *nor.first);
  ASSERT_TRUE(placed.has_value());
  EXPECT_EQ(placed->first, 23);

  std::cerr << placed->second.to_ascii() << "\n";
  EXPECT_EQ(placed->second.to_ascii(), R"(
        A       A
        .+-A..  ..
        ..  ..  ..
        ||      |
        ab      |
       c..--..--+.
        ..  ..  ..


        ..  ..  ..
        ..  ..  ..)");
}

TEST(ChipBuilderTest, NorIntoNand) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});
  auto nand = net.Nand({nor, in[2]});

  auto spec = SmallSpec();
  ChipBuilder b(spec);

  auto placed = b.TryPlace(net, *nor.first);
  ASSERT_TRUE(placed.has_value());
  placed = placed->second.TryPlace(net, *nand.first);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->second.to_ascii() << "\n";
  EXPECT_EQ(placed->second.to_ascii(), R"(
        A       A
        .+-A..  ..
        ..  ..  ..
        ||      |
        ab      |
       c..-b..--+.
       A..-a..  ..


        ..  ..  ..
        ..  ..  ..)");
}

TEST(ChipBuilderTest, NorIntoNor) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({in[0], in[1], in[2]});
  auto nor2 = net.Nor({nor, in[0]});

  auto spec = SmallSpec();
  ChipBuilder b(spec);

  auto placed = b.TryPlace(net, *nor.first);
  ASSERT_TRUE(placed.has_value());
  placed = placed->second.TryPlace(net, *nor2.first);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->second.to_ascii() << "\n";
  EXPECT_EQ(placed->second.to_ascii(), R"(
        A       A
        .+-A..  ..
        +.--..--.+
        ||      ||
        ab      |b
       c..--..--+.
       A..--..-a..


        ..  ..  ..
        ..  ..  ..)");
}

TEST(ChipBuilderTest, Complements) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto nor = net.Nor({net.Not(in[0]), net.Not(in[1]), in[2]});
  auto nor2 = net.Nor({net.Not(nor), in[0]});

  auto spec = SmallSpec();
  ChipBuilder b(spec);

  auto placed = b.TryPlace(net, *nor.first);
  ASSERT_TRUE(placed.has_value());
  placed = placed->second.TryPlace(net, *nor2.first);
  ASSERT_TRUE(placed.has_value());

  std::cerr << placed->second.to_ascii() << "\n";
  EXPECT_EQ(placed->second.to_ascii(), R"(
        B       A
        .+-B..  ..
       A..--..--.+
        ||      ||
        ab      |b
       c..--..--+.
       B..--..-a..


        ..  ..  ..
        ..  ..  ..)");
}

TEST(FpgaMappingTest, Test) {
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
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNor, F::kNand, F::kNor, F::kNand, F::kNor},
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNand, F::kNor, F::kNand, F::kNor, F::kNand},
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kNand, F::kNor, F::kNor, F::kNor, F::kNand},
              {F::kIn, F::kIn, F::kIn, F::kIn, F::kIn},
              {F::kFF, F::kLut2, F::kNor, F::kLut2, F::kFF},
              {F::kOut, F::kOut, F::kOut, F::kOut, F::kOut},
          },
      .bus_width = 4,
      .nor_arity = kNorArity,
  };

  auto mapping = FpgaMapping::Map(spec, net);
  for (auto& builder : mapping.chips) {
    std::cerr << builder.to_ascii()
              << "\nbottleneck: " << builder.bottleneck_resource() << "\n\n";
  }
}
