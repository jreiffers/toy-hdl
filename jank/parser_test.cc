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

}  // namespace
}  // namespace jank
