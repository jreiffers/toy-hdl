#include "jank/parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

#include "jank/context.h"
#include "jank/print.h"

namespace jank {
namespace {

using ::testing::HasSubstr;

std::string Parse(std::string in) {
  Context ctx(in);
  Module mod = Parse(ctx);
  if (!ctx.errors().empty()) {
    return ctx.error_string();
  }
  return Print(mod);
}

TEST(ParseTest, Basic) {
  EXPECT_EQ(Parse(R"(
    int foo(int a, int b);
    int bar();  
    int foo(int c, int d);
    int baz(int a) {}
    int qux; 
    int bar() {}
  )"),
            R"(int qux;

int foo(int, int);
int bar();
int baz(int a);

int bar() {}
int baz(int a) {}
)");
}

TEST(ParseTest, WrongSig) {
  EXPECT_EQ(Parse(R"(
    int foo(int a, int b);
    int foo(int a);
  )"),
            R"(Attempted to redeclare function 'foo' with a different signature.
   3 |     int foo(int a);
     |         ^
Note: previously declared here:
   2 |     int foo(int a, int b);
     |         ^)");
}

TEST(ParseTest, WrongRet) {
  EXPECT_THAT(Parse("int foo(int a, int b); void foo(int a, int b);"),
              HasSubstr("previously declared"));
}

TEST(ParseTest, FunctionThenGlobal) {
  EXPECT_THAT(Parse("int foo(int a, int b); int foo;"),
              HasSubstr("variable which was previously"));
}

TEST(ParseTest, GlobalThenFunction) {
  EXPECT_THAT(Parse("int foo; int foo(int a, int b);"),
              HasSubstr("function which was previously"));
}

TEST(ParseTest, RedeclareGlobal) {
  EXPECT_THAT(Parse("int foo; int foo;"), HasSubstr("redeclare variable"));
}

TEST(ParseTest, DuplicateDef) {
  EXPECT_EQ(Parse("int foo() {}\nint foo() {}"),
            R"(Duplicate function definition.
   2 | int foo() {}
     |           ^
Note: first defined here:
   1 | int foo() {}
     |           ^)");
}

TEST(ParseTest, ParamListNotClosed) {
  EXPECT_THAT(Parse("int foo(int a"), HasSubstr("Expected ')'"));
}

TEST(ParseTest, BlockNotClosed) {
  EXPECT_THAT(Parse("int foo() {"), HasSubstr("Expected '}'"));
}

TEST(ParseTest, LocalVar) {
  EXPECT_THAT(Parse("int foo() { int a; }"), HasSubstr(R"(
int foo() {
  int a;
})"));
}

TEST(ParseTest, CallArg) {
  EXPECT_THAT(Parse("int foo(int a) { a(); }"), HasSubstr("Only functions"));
}

TEST(ParseTest, ExpressionPrecedence) {
  EXPECT_THAT(Parse("int f() { 1 + 2 * 3 - 4 / 2 % 3; }"),
              HasSubstr("((1 + (2 * 3)) - ((4 / 2) % 3));"));
  EXPECT_THAT(Parse("int f() { 1 + 2 << 3 < 4 == 5; }"),
              HasSubstr("((((1 + 2) << 3) < 4) == 5);"));
  EXPECT_THAT(Parse("int f() { 8 - 4 - 2; 8 >> 2 >> 1; }"),
              HasSubstr("((8 - 4) - 2);"));
  EXPECT_THAT(Parse("int f() { 8 >> 2 >> 1; }"), HasSubstr("((8 >> 2) >> 1);"));
}

TEST(ParseTest, Unary) {
  EXPECT_THAT(Parse("int f(int a) { -(a + 2) * ~3 + !a; }"),
              HasSubstr("(((-(a + 2)) * (~3)) + (!a));"));
}

TEST(ParseTest, ConditionalAssociativity) {
  EXPECT_THAT(Parse("int f() { 1 ? 2 : 3 ? 4 : 5; }"),
              HasSubstr("(1 ? 2 : (3 ? 4 : 5));"));
  EXPECT_THAT(Parse("int f() { 1 ? 2 ? 3 : 4 : 5 + 6; }"),
              HasSubstr("(1 ? (2 ? 3 : 4) : (5 + 6));"));
}

TEST(ParseTest, CallsAndScope) {
  EXPECT_THAT(Parse("int g; int h(int a, int b); int f(int x) { "
                    "int y; y = h(x + g, h(1, 2)); h(3, 4); }"),
              HasSubstr("y = h((x + g), h(1, 2));"));
  EXPECT_THAT(Parse("int h(); int f() { -h(); }"), HasSubstr("(-h());"));
  EXPECT_THAT(Parse("int f() { missing + 1; }"),
              HasSubstr("Undeclared identifier 'missing'"));
}

TEST(ParseTest, CallArguments) {
  Context ctx("int h(int a, int b); int f() { h(1, 2); }");
  Module mod = Parse(ctx);
  ASSERT_TRUE(ctx.errors().empty()) << ctx.error_string();
  auto* call = mod.functions()[1].def()->body.instructions[0].expression;
  EXPECT_EQ(call->type, ExpressionType::kCall);
  EXPECT_EQ(call->symbol, &mod.functions()[0]);
  ASSERT_EQ(call->operands.size(), 2);
  EXPECT_EQ(call->operands[0]->const_val, 1);
  EXPECT_EQ(call->operands[1]->const_val, 2);
}

TEST(ParseTest, FunctionNameRequiresCall) {
  Context ctx("int f(); int g() { f + 10; }");
  Module mod = Parse(ctx);
  ASSERT_EQ(ctx.errors().size(), 1) << ctx.error_string();
  EXPECT_EQ(ctx.errors()[0].message, "Expected '('.");
}

TEST(ParseTest, FunctionsRequireDirectCalls) {
  for (const char* expression :
       {"h", "h + 1", "(h)()", "h()()", "a()", "a[0]()", "1()", "(1 + 2)()"}) {
    SCOPED_TRACE(expression);
    Context ctx(absl::StrCat("int h(); int f(int a) { ", expression, "; }"));
    Module mod = Parse(ctx);
    EXPECT_FALSE(ctx.errors().empty());
  }
}

TEST(ParseTest, AssignmentStatements) {
  Context ctx(
      "int f(int a) { a += 1 + 2; a -= 3; a *= 4; a /= 5; "
      "a <<= 6; a >>= 7; a = 8; }");
  Module mod = Parse(ctx);
  ASSERT_TRUE(ctx.errors().empty()) << ctx.error_string();
  const auto& instructions = mod.functions().front().def()->body.instructions;
  ASSERT_EQ(instructions.size(), 7);
  const std::vector<ExpressionType> types = {
      ExpressionType::kAdd, ExpressionType::kSub, ExpressionType::kMul,
      ExpressionType::kDiv, ExpressionType::kShl, ExpressionType::kShr};
  for (size_t i = 0; i < instructions.size(); ++i) {
    EXPECT_EQ(instructions[i].type, InstructionType::kAssign);
    EXPECT_EQ(instructions[i].target->type, ExpressionType::kVariable);
    if (i < types.size()) {
      EXPECT_EQ(instructions[i].expression->type, types[i]);
      EXPECT_EQ(instructions[i].expression->operands[0],
                instructions[i].target);
    } else {
      EXPECT_EQ(instructions[i].expression->type, ExpressionType::kConstant);
    }
  }
  EXPECT_EQ(instructions[0].expression->operands[1]->type,
            ExpressionType::kAdd);
  EXPECT_THAT(Print(mod), HasSubstr("a = (a + (1 + 2));"));
}

TEST(ParseTest, AllBinaryOperators) {
  const std::vector<std::pair<std::string, ExpressionType>> operators = {
      {"+", ExpressionType::kAdd},  {"-", ExpressionType::kSub},
      {"*", ExpressionType::kMul},  {"/", ExpressionType::kDiv},
      {"%", ExpressionType::kMod},  {"<<", ExpressionType::kShl},
      {">>", ExpressionType::kShr}, {"<", ExpressionType::kLt},
      {"<=", ExpressionType::kLe},  {">", ExpressionType::kGt},
      {">=", ExpressionType::kGe},  {"==", ExpressionType::kEq},
      {"!=", ExpressionType::kNe}};
  for (const auto& [op, type] : operators) {
    SCOPED_TRACE(op);
    Context ctx("int f() { 1 " + op + " 2; }");
    Module mod = Parse(ctx);
    ASSERT_TRUE(ctx.errors().empty()) << ctx.error_string();
    const auto* expr =
        mod.functions().front().def()->body.instructions[0].expression;
    EXPECT_EQ(expr->type, type);
    EXPECT_EQ(expr->operands.size(), 2);
    EXPECT_EQ(expr->loc().column_number, 12);
  }
}

TEST(ParseTest, MalformedExpressions) {
  struct TestCase {
    std::string source;
    std::string message;
  };
  const std::vector<TestCase> cases = {
      {"int f() { 1 + ; }", "Expected expression."},
      {"int f() { (1 + 2; }", "Expected ')'."},
      {"int f() { 1 ? 2; }", "Expected ':'."},
      {"int f() { 1 ? 2 : ; }", "Expected expression."},
      {"int f() { 1 +", "Expected expression."},
      {"int f() { int ; }", "Expected 'identifier'."},
      {"int f() { 1 = 2; }", "Expected assignable expression."},
      {"int f(int a) { a = a = 2; }", "Expected ';'."},
      {"int f() { 1, 2; }", "Expected ';'."},
      {"int f(int a) { (a += 1); }", "Expected ')'."},
      {"int h(int a); int f(int a) { h(a = 1); }", "Expected ')'."},
      {"int h(int a); int f(int a) { h(a += 1); }", "Expected ')'."},
      {"int f(int a) { a[a = 1]; }", "Expected ']'."},
      {"int h(int a); int f() { h(1,); }", "Expected expression."},
      {"int f(int a) { *a; }", "Expected expression."},
      {"int f(int a) { *a = 1; }", "Expected expression."},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.source);
    Context ctx(test.source);
    Module mod = Parse(ctx);
    ASSERT_EQ(ctx.errors().size(), 1) << ctx.error_string();
    EXPECT_EQ(ctx.errors()[0].message, test.message) << ctx.error_string();
  }
}

TEST(ParseTest, Indexing) {
  EXPECT_THAT(Parse("int f(int a) { a[1 + 2] = a[3] * 4; }"),
              HasSubstr("a[(1 + 2)] = (a[3] * 4);"));
  EXPECT_THAT(Parse("int f(int a) { a[1; }"), HasSubstr("Expected ']'"));
  EXPECT_THAT(Parse("int f(int a) { 1[a] = 2; }"), HasSubstr("1[a] = 2"));
}

TEST(ParseTest, DeclAndInit) {
  EXPECT_THAT(Parse("int f(int a) { int b = a + 3; }"),
              HasSubstr("int b;\n  b = (a + 3);"));
}

TEST(ParseTest, If) {
  EXPECT_THAT(Parse(R"(
    int f(int a) {
      if (a > a - 1)
        a = 0;
      else { a = a + 3; a = a * 2; }
    })"),
              HasSubstr(R"(
  if ((a > (a - 1))) a = 0;
  else {
    a = (a + 3);
    a = (a * 2);
  })"));
}

TEST(ParseTest, IfWithoutElse) {
  EXPECT_THAT(Parse(R"(
    int f(int a) {
      if (a > 0)
        a = 0;
      a += 1;
    })"),
              HasSubstr(R"(
  if ((a > 0)) a = 0;
  a = (a + 1);
)"));
}

TEST(ParseTest, While) {
  EXPECT_THAT(Parse(R"(
    int f(int a) {
      while (a < 5) {
        a += 1;
      }
      a = 0;
    })"),
              HasSubstr(R"(
  while ((a < 5)) {
    a = (a + 1);
  }
  a = 0;
)"));
}

TEST(ParseTest, For) {
  EXPECT_THAT(Parse(R"(
    int g(int a);
    int f() {
      for (int a = 0; a < 5; a += 1) {
        g(a);
      }
    })"),
              HasSubstr(R"(
  {
    int a;
    a = 0;
    for (; (a < 5); a = (a + 1)) {
      g(a);
    }
  })"));
}

TEST(ParseTest, ForWithoutIncrement) {
  EXPECT_THAT(Parse("int f() { for (int a = 0; a < 5;) { a += 1; } }"),
              HasSubstr("for (; (a < 5); ) {\n      a = (a + 1);\n    }"));
}

}  // namespace
}  // namespace jank
