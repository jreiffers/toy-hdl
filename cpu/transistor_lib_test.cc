#include "transistor_lib.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "eval.h"

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Optional;
using ::testing::Pair;

TEST(LibTest, TestNand) {
  Network net;
  NodeId a = net.make_input<1>()[0];
  NodeId b = net.make_input<1>()[0];
  NodeId c = net.make_input<1>()[0];
  NodeId x = make_nand(net, {a, b, c});

  EXPECT_THAT(EvaluateAll(net, {x}),
              Optional(ElementsAre(1, 1, 1, 1, 1, 1, 1, 0)));
}

TEST(LibTest, TestMux) {
  Network net;
  NodeId a = net.make_input<1>()[0];
  NodeId b = net.make_input<1>()[0];
  NodeId sel = net.make_input<1>()[0];
  NodeId not_sel = make_not(net, sel);

  NodeId x = make_mux(net, sel, not_sel, b, a);

  EXPECT_THAT(EvaluateAll(net, {x}), Optional(ElementsAre(
                                         /*sel=0*/ 0, 1, 0, 1,
                                         /*sel=1*/ 0, 0, 1, 1)));
}

TEST(LibTest, TestTriStateBuffer) {
  Network net;
  NodeId data = net.make_input<1>()[0];
  NodeId enable = net.make_input<1>()[0];

  NodeId not_enable = make_not(net, enable);
  NodeId out = make_tri_state_buffer(net, enable, not_enable, data);

  EXPECT_THAT(  //
      Evaluate(net, {{data, PinState::kLow}, {enable, PinState::kLow}}),
      Contains(Pair(out, PinState::kUndefined)));
  EXPECT_THAT(
      Evaluate(net, {{data, PinState::kLow}, {enable, PinState::kHigh}}),
      Contains(Pair(out, PinState::kLow)));
  EXPECT_THAT(
      Evaluate(net, {{data, PinState::kHigh}, {enable, PinState::kLow}}),
      Contains(Pair(out, PinState::kUndefined)));
  EXPECT_THAT(
      Evaluate(net, {{data, PinState::kHigh}, {enable, PinState::kHigh}}),
      Contains(Pair(out, PinState::kHigh)));
}
