#include "eval.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "export.h"

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Optional;

TEST(EvalTest, DetectsShort) {
  Network net;

  NodeId a = net.make_input(1)[0];
  NodeId not_a = make_not(net, a);

  TransistorId n = net.make_transistor(TransistorType::kNChannel);
  net.connect(n.source(), kVdd);
  net.connect(n.gate(), a);

  TransistorId p = net.make_transistor(TransistorType::kPChannel);
  net.connect(n.source(), kVss);
  net.connect(n.gate(), not_a);

  net.connect(n.drain(), p.drain());

  std::unordered_map<NodeId, PinState> state{{a, PinState::kLow}};
  state = Evaluate(net, std::move(state));

  EXPECT_EQ(state[n.drain()], PinState::kShort);
}

TEST(EvalTest, WorksForXor) {
  Network net;
  NodeId a = net.make_input(1)[0];
  NodeId b = net.make_input(1)[0];
  NodeId x = make_xor(net, a, b);

  EXPECT_THAT(EvaluateAll(net, {x}), Optional(ElementsAre(0, 1, 1, 0)));
}

TEST(EvalTest, VerifySpec) {
  Network net;
  NodeId a = net.make_input(1)[0];
  NodeId b = net.make_input(1)[0];
  NodeId x = make_xor(net, a, b);
  NodeId y = make_nand(net, {a, b});
  NodeId z = make_nor(net, {make_not(net, a), b});

  std::vector<dyn_reg> outputs{dyn_reg({x, y}), dyn_reg({z})};

  int num_calls = 0;
  ABSL_EXPECT_OK(
      VerifySpec(net, outputs, [&](absl::Span<const uint32_t> inputs) {
        EXPECT_EQ(inputs.size(), 2);
        ++num_calls;
        uint32_t a = inputs[0];
        uint32_t b = inputs[1];

        return absl::InlinedVector<uint32_t, 4>{(a ^ b) | ((a & b) ^ 1) << 1,
                                                ((a ^ 1) | b) ^ 1};
      }));

  EXPECT_EQ(num_calls, 4);
}

TEST(EvalTest, IncorrectSpec) {
  Network net;
  NodeId b = net.make_input(1)[0];
  NodeId a = net.make_input(1)[0];
  NodeId x = make_xor(net, a, b);
  NodeId y = make_nand(net, {a, b});
  NodeId z = make_nor(net, {make_not(net, a), b});

  std::vector<dyn_reg> outputs{dyn_reg({x, y}), dyn_reg({z})};

  EXPECT_FALSE(VerifySpec(net, outputs, [&](absl::Span<const uint32_t> inputs) {
                 EXPECT_EQ(inputs.size(), 2);
                 uint32_t a = inputs[0];
                 uint32_t b = inputs[1];

                 return absl::InlinedVector<uint32_t, 4>{
                     (a ^ b) | ((a & b) ^ 1) << 1, ((a ^ 1) | b) ^ 1};
               }).ok());
}

TEST(EvalTest, VerifySpecGates) {
  GateNetwork net;
  auto in1 = net.AddInput<3>();
  auto in2 = net.AddInput<3>();

  auto [sum, carry] = MakeAdder(net, in1, in2);
  auto res = net.Mux(carry, in1, sum);

  net.DeclareOutput(res);
  net.DeclareOutput(sum);

  std::vector<DynGateReg> outputs{res, sum};
  ABSL_EXPECT_OK(VerifySpec(net, outputs,
                            [](absl::Span<const uint32_t> inputs)
                                -> absl::InlinedVector<uint32_t, 4> {
                              uint32_t sum = inputs[0] + inputs[1];
                              bool carry = sum > 7;
                              sum &= 7;
                              return {carry ? inputs[0] : sum, sum};
                            }));
}

TEST(EvalTest, VerifySpecGatesLowHigh) {
  GateNetwork net;

  net.DeclareOutput(DynGateReg({kLowGate}));
  net.DeclareOutput(DynGateReg({kHighGate}));

  std::vector<DynGateReg> outputs{DynGateReg({kLowGate}),
                                  DynGateReg({kHighGate})};
  ABSL_EXPECT_OK(
      VerifySpec(net, outputs,
                 [](absl::Span<const uint32_t> inputs)
                     -> absl::InlinedVector<uint32_t, 4> { return {0, 1}; }));
}

TEST(EvalTest, EvalSrLatch) {
  GateNetwork net;
  auto r = net.AddInput<1>()[0];
  auto s = net.AddInput<1>()[0];
  GateReg<2> qnotq = MakeSrLatch(net, s, r, kLowGate);
  net.DeclareOutput(qnotq);

  auto q = qnotq[0];
  auto notq = qnotq[1];

  std::unordered_map<GateTerminal, GateTerminalState> state;
  auto expect_state = [&](bool qval) {
    EXPECT_TRUE(state.count(q) &&
                state[q] == static_cast<GateTerminalState>(qval));
    EXPECT_TRUE(state.count(notq) &&
                state[notq] == static_cast<GateTerminalState>(!qval));
  };

  EXPECT_THAT(EvaluateStep(net, state, {1, 0}), IsOk());
  expect_state(false);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0}), IsOk());
  expect_state(false);

  EXPECT_THAT(EvaluateStep(net, state, {0, 1}), IsOk());
  expect_state(true);

  EXPECT_THAT(EvaluateStep(net, state, {0, 0}), IsOk());
  expect_state(true);
}

TEST(EvalTest, EvalDFlipFlop) {
  GateNetwork net;
  auto d = net.AddInput<1>()[0];
  auto clk = net.AddInput<1>()[0];
  auto reset = net.AddInput<1>()[0];
  auto q = MakeDFlipFlop(net, d, net.Not(clk), net.Not(reset))[0];

  std::unordered_map<GateTerminal, GateTerminalState> state;
  EXPECT_THAT(EvaluateStep(net, state, {1, 1, 0}), IsOk());
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 0}), IsOk());
  ASSERT_TRUE(state.count(q) == 1);
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {0, 1, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {0, 1, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {0, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {1, 1, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 0}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kHigh);
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 1}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {1, 1, 1}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
  EXPECT_THAT(EvaluateStep(net, state, {1, 0, 1}), IsOk());
  EXPECT_EQ(state[q], GateTerminalState::kLow);
}
