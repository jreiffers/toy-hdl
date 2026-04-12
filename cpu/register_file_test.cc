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

  GateReg<2> register_addr{{kHighGate, kLowGate}};

  auto rda1 = net.AddInput<2>();
  auto rda2 = net.AddInput<2>();
  auto wa = net.AddInput<2>();

  auto data = net.AddInput<4>();

  auto out = MakeRegister(net, reset, clk, register_addr, rda1, rda2, wa, data);
  std::unordered_map<GateTerminal, GateTerminalState> state;

  auto tick = [&](bool reset, bool clock, uint32_t ra1, uint32_t ra2,
                  uint32_t wa, uint32_t wdata) {
    EXPECT_THAT(
        EvaluateStep(net, state,
                     {static_cast<uint32_t>(!reset),
                      static_cast<uint32_t>(!clock), ra1, ra2, wa, wdata}),
        IsOk());
  };

  tick(true, false, 0, 0, 0, 0);
  EXPECT_EQ(GetNum(state, out.read_port_1), std::nullopt);
  EXPECT_EQ(GetNum(state, out.read_port_2), std::nullopt);

  tick(false, false, 1, 0, 0, 15);
  EXPECT_EQ(GetNum(state, out.read_port_1), 0);
  EXPECT_EQ(GetNum(state, out.read_port_2), std::nullopt);

  tick(false, true, 1, 0, 0, 15);
  EXPECT_EQ(GetNum(state, out.read_port_1), 0);

  tick(false, false, 1, 0, 0, 15);  // Falling clk, but wrong write addr.
  EXPECT_EQ(GetNum(state, out.read_port_1), 0);

  tick(false, true, 1, 0, 1, 15);
  EXPECT_EQ(GetNum(state, out.read_port_1), 0);

  tick(false, false, 1, 0, 1, 15);
  EXPECT_EQ(GetNum(state, out.read_port_1), 15);

  tick(false, true, 0, 1, 0, 0);  // Read on the other port.
  EXPECT_EQ(GetNum(state, out.read_port_1), std::nullopt);
  EXPECT_EQ(GetNum(state, out.read_port_2), 15);

  tick(false, false, 1, 1, 0, 0);  // Read on both ports.
  EXPECT_EQ(GetNum(state, out.read_port_1), 15);
  EXPECT_EQ(GetNum(state, out.read_port_2), 15);
}
