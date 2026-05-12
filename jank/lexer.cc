#include "lexer.h"

namespace jank {

std::string_view to_string(TokenType id) {
  if (id == TokenType::kIdentifier) {
    return "identifier";
  }
  if (id == TokenType::kIntLiteral) {
    return "integer literal";
  }

#define JANK_VISITOR(id, is_variable, val) \
  case TokenType::id:                      \
    return val;

  switch (id) { JANK_VISIT_TOKENS(JANK_VISITOR); }
#undef JANK_VISITOR
}

void Tokenizer::Tokenize() {
  auto advance = [&](int num) {
    for (int i = 0; i < num; ++i) {
      if (s_[0] == '\n') {
        ++loc_.line_number;
        loc_.column_number = 0;
      } else {
        ++loc_.column_number;
      }
      s_ = s_.substr(1);
    }
  };

  auto error = [&](std::string msg) { ctx_.AddError(std::move(msg), loc_); };

  auto skip_to_including = [&](std::string_view prefix, bool optional) {
    while (s_.size() >= prefix.size()) {
      if (s_.starts_with(prefix)) {
        advance(prefix.size());
        return;
      } else {
        advance(1);
      }
    }

    if (!optional) {
      error(absl::StrCat("Expected '", prefix, "', but found end of file."));
    }
  };

  auto skip_whitespace_and_comments = [&]() {
    start:
      if (s_.empty()) return;

      while (!s_.empty() && s_[0] <= ' ') {
        advance(1);
      }

      if (s_[0] == '/' && s_.size() > 1) {
        if (s_[1] == '/') {
          skip_to_including("\n", /*optional=*/true);
          goto start;
        } else if (s_[1] == '*') {
          advance(2);
          skip_to_including("*/", /*optional=*/false);
          goto start;
        }
      }
  };

  auto is_identifier_char = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') ||
           (c >= '0' && c <= '9');
  };

  next_token_ = std::nullopt;
  skip_whitespace_and_comments();

  if (s_.empty()) return;

    // clang-format off
#define JANK_VISITOR(id, is_variable, val) \
  if (!is_variable && s_.starts_with(val)) { \
    next_token_ = Token{ TokenType::id, val, loc_ }; \
    advance(std::string_view(val).size()); \
  } else
  JANK_VISIT_TOKENS(JANK_VISITOR)
#undef JANK_VISITOR
  // clang-format on
  if (s_[0] >= '0' && s_[0] <= '9') {
    int i = 0;
    while (s_.size() > i && s_[i] >= '0' && s_[i] <= '9') {
      ++i;
    }
    next_token_ = Token{TokenType::kIntLiteral, s_.substr(0, i), loc_};
    advance(i);

    if (s_.size() > 0 && is_identifier_char(s_[0])) {
      error(absl::StrCat("Invalid digit '", s_.substr(0, 1), "'."));
    }
  } else if (is_identifier_char(s_[0])) {
    int i = 0;
    while (s_.size() > i && is_identifier_char(s_[i])) {
      ++i;
    }
    next_token_ = Token{TokenType::kIdentifier, s_.substr(0, i), loc_};
    advance(i);
  } else {
    error(absl::StrCat("Unexpected '", s_.substr(0, 1), "'."));
  }
}

}  // namespace jank
