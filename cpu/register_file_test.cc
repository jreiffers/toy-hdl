#include "register_file.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "eval.h"

using ::absl_testing::IsOk;
using ::testing::ElementsAre;
using ::testing::Optional;

TEST(RegisterTest, Test) {
  GateNetwork net;
  auto reset = net.AddInput<1>();
  auto clk = net.AddInput<1>();
  auto we = net.AddInput<1>();
  auto data = net.AddInput<4>();

  auto out = MakeRegister(net, reset, clk, we, data);
  std::unordered_map<GateTerminal, bool> state;

  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 0, 0}), IsOk());
  EXPECT_EQ(GetNum(state, out), 0);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0, 15}), IsOk());
  EXPECT_EQ(GetNum(state, out), 0);

  EXPECT_THAT(EvaluateStep(net, state, {0, 1, 0, 15}), IsOk());
  EXPECT_EQ(GetNum(state, out), 0);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0, 15}), IsOk());
  EXPECT_EQ(GetNum(state, out), 0);

  EXPECT_THAT(EvaluateStep(net, state, {0, 1, 1, 15}), IsOk());
  EXPECT_EQ(GetNum(state, out), 0);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 1, 15}), IsOk());
  EXPECT_EQ(GetNum(state, out), 15);

  EXPECT_THAT(EvaluateStep(net, state, {0, 1, 0, 0}), IsOk());
  EXPECT_EQ(GetNum(state, out), 15);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0, 0}), IsOk());
  EXPECT_EQ(GetNum(state, out), 15);
}
