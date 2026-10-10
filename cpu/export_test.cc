#include "cpu/export.h"

#include <gtest/gtest.h>

TEST(ExportTest, PrintSingleUseNodes) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  net.DeclareOutput(net.Not(net.Nand({in[0], in[1], net.Not(in[2])})));
  EXPECT_EQ(PrintNetwork(net, 14), R"(a, b, c ->
              ~nand(a, b, ~c))");
}

TEST(ExportTest, PrintReusedNodes) {
  GateNetwork net;
  auto in = net.AddInput<3>();
  auto d = net.Not(net.Nand({in[0], in[1]}));
  net.DeclareOutput(net.Not(net.Nor({d, net.Not(net.Nand({d, in[2]}))})));
  EXPECT_EQ(PrintNetwork(net, 17),
            R"(a, b, c ->
                 let d = ~nand(a, b) in
                 ~nor(d, ~nand(d, c)))");
}
