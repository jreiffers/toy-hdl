#include "alu.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "compiler.h"
#include "eval.h"
#include "gate_opt.h"
#include "lower_gates.h"
#include "transistor_lib.h"

using ::testing::ElementsAre;
using ::testing::Optional;

int CountLiveGates(GateNetwork& net) {
  int result = 0;
  net.WalkUnordered([&](int, Gate& g) { ++result; });
  return result;
}

TEST(AluTest, TestSpec) {
  GateNetwork net;
  net.Build<Alu<2>>();
  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true});

  ABSL_EXPECT_OK(VerifySpec<Alu<2>>(net));

  auto transistor_net = Compile(net);
  ABSL_EXPECT_OK(VerifySpec<Alu<2>>(transistor_net));
}
