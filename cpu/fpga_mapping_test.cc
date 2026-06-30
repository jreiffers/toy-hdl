#include "cpu/fpga_mapping.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/compiler.h"
#include "cpu/decoder.h"
#include "cpu/eval.h"
#include "cpu/gate_opt.h"

TEST(FpgaMappingTest, Test) {
  GateNetwork net;
  net.Build<Decoder>();

  constexpr int kNorArity = 4;

  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true,
                                        .maximum_nor_arity = kNorArity,
                                        .maximum_nand_arity = 2});
  ABSL_EXPECT_OK(VerifySpec<Decoder>(net));

  constexpr FpgaSpec spec{
      .num_inputs = 8,
      .num_nors = 8,
      .num_ffs = 1,
      .num_nands = 4,
      .num_lut2s = 2,
      .num_outputs = 4,
      .bus_width = 8,
      .nor_arity = kNorArity,
  };

  FpgaMapping<spec>::Map(net);
}
