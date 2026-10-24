#ifndef GATE_TEXT_FORMAT_MATCHERS_H_
#define GATE_TEXT_FORMAT_MATCHERS_H_

#include <gmock/gmock.h>

#include <string>

#include "cpu/gate_text_format.h"
#include "jank/lexer.h"

MATCHER_P(NetworkIs, expected_str,
          "has network text " + ::testing::PrintToString(expected_str)) {
  jank::Context actual_ctx(
      PrintNetwork(/*yikes*/ const_cast<GateNetwork&>(arg)));
  jank::Context expected_ctx{std::string(expected_str)};
  jank::Tokenizer actual(actual_ctx), expected(expected_ctx);
  *result_listener << "whose text format is:\n" << actual_ctx.source_code();
  while (actual && expected) {
    auto a = actual.get();
    auto b = expected.get();
    if (a.type != b.type || a.text != b.text) return false;
  }
  return actual_ctx.errors().empty() && expected_ctx.errors().empty() &&
         !actual && !expected;
}

#endif  // GATE_TEXT_FORMAT_MATCHERS_H_
