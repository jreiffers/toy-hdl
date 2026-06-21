#include "decoder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "cpu/compiler.h"
#include "cpu/eval.h"
#include "cpu/gate_opt.h"
#include "cpu/lower_gates.h"
#include "cpu/transistor_lib.h"

TEST(DecoderTest, TestSpec) {
  GateNetwork net;
  net.Build<Decoder>();
  RunGateOptPipeline(net, FoldGatesOpts{.lower_mux = true});

  ABSL_EXPECT_OK(VerifySpec(net, net.GetOutputs(), Decoder::spec));

  auto transistor_net = Compile(net);
  ABSL_EXPECT_OK(
      VerifySpec(transistor_net, transistor_net.outputs(), Decoder::spec));
}
