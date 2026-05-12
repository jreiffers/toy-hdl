#ifndef JANK_LEXER_H__
#define JANK_LEXER_H__

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "jank/context.h"

namespace jank {

// clang-format off
#define JANK_VISIT_TOKENS(visit)                                               \
  visit(kUnknownToken, true, "<<unknown>>")                                    \
  visit(kIdentifier, true, "<<identifier>>")                                   \
  visit(kIntLiteral, true, "<<literal>>")                                      \
  visit(kVoid, false, "void")                                                  \
  visit(kInt, false, "int")                                                    \
  visit(kGpi, false, "gpi")                                                    \
  visit(kFor, false, "for")                                                    \
  visit(kWhile, false, "while")                                                \
  visit(kIf, false, "if")                                                      \
  visit(kElse, false, "else")                                                  \
  visit(kGoto, false, "goto")                                                  \
  visit(kColon, false, ":")                                                    \
  visit(kReturn, false, "return")                                              \
  visit(kParenLeft, false, "(")                                                \
  visit(kParenRight, false, ")")                                               \
  visit(kBraceLeft, false, "{")                                                \
  visit(kBraceRight, false, "}")                                               \
  visit(kBracketLeft, false, "[")                                              \
  visit(kBracketRight, false, "]")                                             \
  visit(kAssignPlus, false, "+=")                                              \
  visit(kAssignMinus, false, "-=")                                             \
  visit(kAssignMul, false, "*=")                                               \
  visit(kAssignDiv, false, "/=")                                               \
  visit(kAssignShr, false, ">>=")                                              \
  visit(kAssignShl, false, "<<=")                                              \
  visit(kLe, false, "<=")                                                      \
  visit(kGe, false, ">=")                                                      \
  visit(kLt, false, "<")                                                       \
  visit(kGt, false, ">")                                                       \
  visit(kEq, false, "==")                                                      \
  visit(kNe, false, "!=")                                                      \
  visit(kPlus, false, "+")                                                     \
  visit(kMinus, false, "-")                                                    \
  visit(kAsterisk, false, "*")                                                 \
  visit(kDiv, false, "/")                                                      \
  visit(kMod, false, "%")                                                      \
  visit(kShr, false, ">>")                                                     \
  visit(kShl, false, "<<")                                                     \
  visit(kAssign, false, "=")                                                   \
  visit(kSemicolon, false, ";")                                                \
  visit(kCond, false, "?")                                                     \
  visit(kComma, false, ",")

// clang-format on

enum class TokenType {
#define JANK_VISITOR(id, ...) id,
  JANK_VISIT_TOKENS(JANK_VISITOR)
#undef JANK_VISITOR
};

std::string_view to_string(TokenType id);

struct Token {
  TokenType type;
  std::string_view text;

  SourceLocation loc;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Token& t) {
    absl::Format(&sink, "'%s' (%s), line %d column %d", t.text, t.type,
                 t.loc.line_number, t.loc.column_number);
  }
};

class Tokenizer {
 public:
  Tokenizer(Context& context) : ctx_(context), s_(context.source_code()) {
    Tokenize();
  }

  operator bool() const { return next_token_.has_value(); }

  const Token& peek() const {
    if (!next_token_) {
      return invalid_ = {TokenType::kUnknownToken, "<<unknown>>", loc_};
    }
    return *next_token_;
  }

  Token get() {
    Token result = *next_token_;
    last_token_ = result;
    Tokenize();
    return result;
  }

  Token last_token() const { return last_token_; }

  Context& context() { return ctx_; }

 private:
  void Tokenize();

  Context& ctx_;
  std::string_view s_;
  std::optional<Token> next_token_;
  SourceLocation loc_{0, 0};
  Token last_token_;
  mutable Token invalid_;
};

}  // namespace jank

#endif
