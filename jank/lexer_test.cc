#include "lexer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

namespace jank {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

MATCHER_P(TokenId, v, to_string(v)) { return arg.type == v; }

MATCHER_P(Identifier, v, v) {
  return arg.type == TokenType::kIdentifier && arg.text == v;
}

MATCHER_P(Int, v, v) {
  return arg.type == TokenType::kIntLiteral && arg.text == v;
}

std::vector<Token> Tokenize(Context& ctx) {
  Tokenizer t(ctx);
  std::vector<Token> tokens;
  while (t) {
    tokens.push_back(t.get());
  }
  EXPECT_THAT(ctx.errors(), IsEmpty());
  return tokens;
}

TEST(TokenizeTest, TokenizeTest) {
  Context ctx(R"(int test(int a, int b) {
    int cafe;
    cafe = a + b;
  foo:
    cafe += a > b ? b >>= 1 : 5;
    return 42 + cafe;
  })");
  std::vector<Token> tokens = Tokenize(ctx);

  using T = TokenType;
  ASSERT_THAT(
      tokens,
      ElementsAre(TokenId(T::kInt), Identifier("test"), TokenId(T::kParenLeft),
                  TokenId(T::kInt), Identifier("a"), TokenId(T::kComma),
                  TokenId(T::kInt), Identifier("b"), TokenId(T::kParenRight),
                  TokenId(T::kBraceLeft), TokenId(T::kInt), Identifier("cafe"),
                  TokenId(T::kSemicolon), Identifier("cafe"),
                  TokenId(T::kAssign), Identifier("a"), TokenId(T::kPlus),
                  Identifier("b"), TokenId(T::kSemicolon), Identifier("foo"),
                  TokenId(T::kColon), Identifier("cafe"),
                  TokenId(T::kAssignPlus), Identifier("a"), TokenId(T::kGt),
                  Identifier("b"), TokenId(T::kCond), Identifier("b"),
                  TokenId(T::kAssignShr), Int("1"), TokenId(T::kColon),
                  Int("5"), TokenId(T::kSemicolon), TokenId(T::kReturn),
                  Int("42"), TokenId(T::kPlus), Identifier("cafe"),
                  TokenId(T::kSemicolon), TokenId(T::kBraceRight)));
}

std::string GetParseError(std::string code) {
  Context ctx(code);
  Tokenizer t(ctx);
  while (t) {
    t.get();
  }
  return ctx.error_string();
}

TEST(TokenizeTest, InvalidDigit) {
  EXPECT_EQ(GetParseError("int a = 42ab;"), R"(Invalid digit 'a'.
   1 | int a = 42ab;
     |           ^)");
}

TEST(TokenizeTest, LineComment) {
  Context ctx(R"(te//st
     a / 3 // 5)");
  std::vector<Token> tokens = Tokenize(ctx);

  using T = TokenType;
  ASSERT_THAT(tokens, ElementsAre(Identifier("te"), Identifier("a"),
                                  TokenId(T::kDiv), Int("3")));
}

TEST(TokenizeTest, MultiLineComment) {
  Context ctx(
      R"(te/*st
     */st)");
  auto tokens = Tokenize(ctx);

  using T = TokenType;
  ASSERT_THAT(tokens, ElementsAre(Identifier("te"), Identifier("st")));
}

TEST(TokenizeTest, MultiLineCommentDegenerate) {
  Context ctx(R"(te/*/st
     */st)");
  std::vector<Token> tokens = Tokenize(ctx);

  using T = TokenType;
  ASSERT_THAT(tokens, ElementsAre(Identifier("te"), Identifier("st")));
}

TEST(TokenizeTest, MultiLineCommentNoEnd) {
  EXPECT_EQ(GetParseError("int a = /*42;"),
            R"(Expected '*/', but found end of file.
   1 | int a = /*42;
     |             ^)");
}

TEST(TokenizeTest, ErrorNotOnFirstLine) {
  EXPECT_EQ(GetParseError(R"(
    int a = 42;
    a | a;
  )"),
            R"(Unexpected '|'.
   3 |     a | a;
     |       ^)");
}

}  // namespace
}  // namespace jank
