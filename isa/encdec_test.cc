#include "isa/encdec.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

namespace isa {
namespace {

TEST(EncdecTest, TestRoundtrip) {
  for (uint16_t i = 0; i < 2048; ++i) {
    Encoder enc;
    DecodeInstruction(i, enc.visitor());
    EXPECT_THAT(enc.instructions(), testing::ElementsAre(i));
  }
}

}  // namespace
}  // namespace isa
