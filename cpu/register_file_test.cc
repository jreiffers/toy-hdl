#include "register_file.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "eval.h"

using ::absl_testing::IsOk;
using ::testing::ElementsAre;
using ::testing::Optional;

template <int bw, int register_address>
struct RegisterFileSpec : GateSpec<RegisterFileSpec<bw, register_address>, 6> {
  constexpr static uint32_t kRd1Mask = 1ul << 31;
  constexpr static uint32_t kRd2Mask = 1ul << 30;
  constexpr static uint32_t kClkMask = 1ul << 29;
  constexpr static uint32_t kDataMask = (1ul << 10) - 1;
  constexpr static uint32_t kStagedDataMask = kDataMask << 10;

  uint32_t d = 0;

  bool operator==(const RegisterFileSpec& rhs) const { return rhs.d == d; }

  RegisterFileSpec transition(bool reset, bool clk, uint32_t rda1,
                              uint32_t rda2, uint32_t wa, uint32_t data) const {
    uint32_t rmask_clk = (rda1 == register_address ? kRd1Mask : 0) +
                         (rda2 == register_address ? kRd2Mask : 0) +
                         (clk ? kClkMask : 0);
    if (!reset) {
      return {{}, rmask_clk};
    }

    uint32_t staged_data = (d & kStagedDataMask) >> 10;
    uint32_t value = d & kDataMask;
    bool prev_clk = (d & kClkMask) > 0;

    if (clk) {
      if (!prev_clk) {
        value = staged_data;
      }
    } else {
      staged_data = wa == register_address ? data : value;
    }

    return {{}, rmask_clk + value + (staged_data << 10)};
  }

  absl::InlinedVector<std::optional<uint32_t>, 4> outputs() const {
    absl::InlinedVector<std::optional<uint32_t>, 4> r{std::nullopt,
                                                      std::nullopt};
    uint32_t data = d & kDataMask;
    if (d & kRd1Mask) {
      r[0] = data;
    }
    if (d & kRd2Mask) {
      r[1] = data;
    }
    return r;
  }

  template <typename H>
  friend H AbslHashValue(H h, const RegisterFileSpec& spec) {
    return H::combine(std::move(h), spec.d);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const RegisterFileSpec& p) {
    absl::Format(&sink, "(re0: %d, re1: %d, clk: %d, staged: %d, value: %d)",
                 (p.d & kRd1Mask) > 0, (p.d & kRd2Mask) > 0,
                 (p.d & kClkMask) > 0, (p.d & kStagedDataMask) >> 10,
                 p.d & kDataMask);
  }
};

template <int addr>
absl::Status VerifyRegister() {
  GateNetwork net;
  auto reset = net.AddInput<1>("reset");
  auto clk = net.AddInput<1>("clk");

  GateReg<2> register_addr{
      {addr & 1 ? kHighGate : kLowGate, addr & 2 ? kHighGate : kLowGate}};

  auto rda1 = net.AddInput<2>("rda1");
  auto rda2 = net.AddInput<2>("rda2");
  auto wa = net.AddInput<2>("wa");

  auto data = net.AddInput<2>("data");

  auto out = MakeRegister(net, reset, clk, register_addr, rda1, rda2, wa, data);
  net.DeclareOutput(out.read_port_1);
  net.DeclareOutput(out.read_port_2);

  return RegisterFileSpec<2, addr>::Verify(net);
}

TEST(RegisterTest, TestSpec0) { EXPECT_THAT(VerifyRegister<0>(), IsOk()); }

TEST(RegisterTest, TestSpec1) { EXPECT_THAT(VerifyRegister<1>(), IsOk()); }

TEST(RegisterTest, TestSpec2) { EXPECT_THAT(VerifyRegister<2>(), IsOk()); }

TEST(RegisterTest, TestSpec3) { EXPECT_THAT(VerifyRegister<3>(), IsOk()); }

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
  absl::flat_hash_map<GateTerminal, GateTerminalState> state;

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
