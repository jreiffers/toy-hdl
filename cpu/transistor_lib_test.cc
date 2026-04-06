#include "transistor_lib.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "eval.h"

using ::testing::ElementsAre;
using ::testing::Optional;

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
