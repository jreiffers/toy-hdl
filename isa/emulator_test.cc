#include "isa/emulator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "isa/assembler.h"
#include "isa/encdec.h"
#include "jank/context.h"

namespace isa {
namespace {

using ::absl::StrFormat;
using ::testing::ElementsAre;

std::vector<uint16_t> Parse(const std::string& source_code) {
  jank::Context ctx(source_code);
  Encoder enc;
  if (!ParseAssembly(ctx, enc.visitor())) {
    ADD_FAILURE() << ctx.error_string();
    return {};
  }
  return std::vector<uint16_t>(enc.instructions().begin(),
                               enc.instructions().end());
}

MachineState RunProgram(
    const std::string& source_code,
    absl::flat_hash_map<int /*time*/,
                        std::pair<uint4_t /*input*/, uint4_t /*val*/>>
        gpi_changes = {},
    int* cycles = 0) {
  constexpr int kMaxSteps = 1000;

  auto instrs = Parse(source_code);
  Emulator emulator(instrs);
  int time = 0;
  while (time < kMaxSteps && emulator.state().pc() < instrs.size()) {
    auto change = gpi_changes.find(time);
    if (change != gpi_changes.end()) {
      auto [addr, val] = change->second;
      emulator.state().set_gpi(addr, val);
    }
    ABSL_EXPECT_OK(emulator.step());
    ++time;
  }
  if (time == kMaxSteps) {
    ADD_FAILURE() << "Ran out of steps.";
  }
  if (cycles) {
    *cycles = time;
  }
  return emulator.state();
}

template <typename Fn>
void TestAll2(const absl::FormatSpec<int, int>& pattern, Fn&& verifier) {
  for (int i = 0; i < 16; ++i) {
    for (int j = 0; j < 16; ++j) {
      auto state = RunProgram(StrFormat(pattern, i, j));
      EXPECT_TRUE(verifier(state, i, j)) << StrFormat(pattern, i, j);
    }
  }
}

bool Eq(MachineState& state, int i, int j) { return state.flag() == (i == j); }

bool Lt(MachineState& state, int i, int j) { return state.flag() == (i < j); }

bool Ne(MachineState& state, int i, int j) { return state.flag() == (i != j); }

bool Ge(MachineState& state, int i, int j) { return state.flag() == (i >= j); }

template <int r>
bool Sub(MachineState& state, int i, int j) {
  return state.read(r) == ((i - j) & 15);
}

template <int r>
bool Subr(MachineState& state, int i, int j) {
  return state.read(r) == ((j - i) & 15);
}

template <int r>
bool And(MachineState& state, int i, int j) {
  return state.read(r) == (i & j);
}

template <int r>
bool Subtnz(MachineState& state, int i, int j) {
  return state.read(r) == ((i - j) & 15) && state.flag() == ((i - j) != 0);
}

bool Andtnz(MachineState& state, int i, int j) {
  return state.flag() == ((i & j) != 0);
}

TEST(EmulatorTest, Add) {
  auto final_state = RunProgram(R"(
    addi 4 r0
    addi 2 r0
    add r0 r1
    add r1 r1
  )");

  EXPECT_EQ(final_state.read(0), 6);
  EXPECT_EQ(final_state.read(1), 12);
}

TEST(EmulatorTest, And) {
  TestAll2("movi %d r0 movi %d r1 and r0 r1", And<1>);
  TestAll2("movi %d r0 movi %d r1 andtnz r0 r1", Andtnz);

  auto final_state = RunProgram(R"(
    movi 5 r0
    movi 3 r1
    andtnz r0 r1
  )");

  EXPECT_EQ(final_state.read(0), 5);
  EXPECT_EQ(final_state.read(1), 3);
  EXPECT_EQ(final_state.flag(), true);
}

TEST(EmulatorTest, Mov) {
  auto final_state = RunProgram(R"(
    movi 4 r0
    mov r0 r3
  )");

  EXPECT_EQ(final_state.read(0), 4);
  EXPECT_EQ(final_state.read(3), 4);
}

TEST(EmulatorTest, LoadStore) {
  auto final_state = RunProgram(R"(
    movi 7 r0
    movi 3 r1
    store r0 r1
    mov [r0] r2
  )");

  EXPECT_EQ(final_state.read(2), 7) << absl::StrCat(final_state);
}

TEST(EmulatorTest, Ldgpi) {
  auto final_state = RunProgram(R"(
    movi 7 r0    // t = 0
    ldgpi r0 r1  // t = 1
    ldgpi r0 r2  // t = 2
    add r1 r2
  )",
                                {{0, {7, 1}}, {1, {7, 2}}, {2, {7, 3}}});

  EXPECT_EQ(final_state.read(2), 5) << absl::StrCat(final_state);
}

TEST(EmulatorTest, WaitTrue) {
  int cycles;
  auto final_state = RunProgram(R"(
    movi 3 r0
    movi 5 r1
    wait r0 r1 1   // wait for [3] == 5
  )",
                                {{99, {2, 5}}, {110, {3, 5}}}, &cycles);

  EXPECT_EQ(cycles, 111);
}

TEST(EmulatorTest, WaitFalse) {
  int cycles;
  auto final_state = RunProgram(R"(
    movi 3 r0
    movi 5 r1
    wait r0 r1 0   // wait for [3] != 5
  )",
                                {{0, {3, 5}}, {99, {3, 4}}}, &cycles);

  EXPECT_EQ(cycles, 100);
}

// TODO finalize the comparison operators to support
TEST(EmulatorTest, Testi) {
  TestAll2("movi %d r0 testi r0 == %d", Eq);
  TestAll2("movi %d r0 testi r0 >= %d", Ge);
  TestAll2("movi %d r0 testi r0 != %d", Ne);
  TestAll2("movi %d r0 testi r0 < %d", Lt);
}

TEST(EmulatorTest, Test) {
  TestAll2("movi %d r0 movi %d r1 test r0 == r1", Eq);
  TestAll2("movi %d r0 movi %d r1 test r0 >= r1", Ge);
  TestAll2("movi %d r0 movi %d r1 test r0 != r1", Ne);
  TestAll2("movi %d r0 movi %d r1 test r0 < r1", Lt);
}

TEST(EmulatorTest, Subi) {
  // Imm is always the ALU LHS, but the destination reg should be last. Hmm.
  // TODO double check the sub/subr and operand order mess here.
  TestAll2("movi %d /*i*/ r0 subi %d r0 /*j*/", Sub<0>);
  TestAll2("movi %d /*i*/ r1 subri %d r1 /*j*/", Subr<1>);
}

TEST(EmulatorTest, Sub) {
  TestAll2("movi %d r0 movi %d r2 sub r0 r2", Subr<2>);
  TestAll2("movi %d r1 movi %d r3 subr r1 r3", Sub<3>);
}

TEST(EmulatorTest, Subitnz) { TestAll2("movi %d r2 subitnz %d r2", Subtnz<2>); }

TEST(EmulatorTest, PushPop) {
  EXPECT_EQ(RunProgram("movi 5 r3   push r3   pop r2").read(2), 5);
}

TEST(EmulatorTest, RetPop) {
  EXPECT_EQ(RunProgram(R"(
         movi 3 r3
         push r3
         call do_it
         jump end
  do_it: retpop r2
         movi 0 r2
  end:
  )")
                .read(2),
            3);
}

TEST(EmulatorTest, Not) {
  EXPECT_EQ(RunProgram("movi 1 r3  not r3").read(3), 14);
}

TEST(EmulatorTest, Shr) {
  EXPECT_EQ(RunProgram("movi 15 r3  shr r3").read(3), 7);
}

TEST(EmulatorTest, Membank) {
  auto final_state = RunProgram(R"(
        movi 4 r3
        store r3 r3
        movi 5 r3
        movi 1 r2
        membank r2
        store r3 r3
  )");

  EXPECT_EQ(final_state.load(0, 4), 4);
  EXPECT_EQ(final_state.load(1, 5), 5);
}

TEST(EmulatorTest, Rombank) {
  int cycles;
  auto final_state = RunProgram(R"(
        movi 1 r3
        rombank r3
        addi 1 r3
        jump next

        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 8
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 16
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 24
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 32
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 40
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 48
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3  // 56
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2
 halt:  jump halt

        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3
        movi 1 r0 movi 2 r1 movi 3 r2 movi 0 r3

 next:  addi 1 r3
  )",
                                {}, &cycles);

  EXPECT_EQ(final_state.read(3), 3);
  EXPECT_EQ(cycles, 5);
}

TEST(EmulatorTest, Invflag) {
  EXPECT_EQ(RunProgram("test r0 == r1 invflag").flag(), false);
  EXPECT_EQ(RunProgram("test r0 != r1 invflag").flag(), true);
}

TEST(ControlFlowTest, Jump3) {
  auto final_state = RunProgram(R"(
               movi 7 r3
               jump3
    (27) halt: jump halt
               movi 8 r3
    (56) e:
  )");
  EXPECT_EQ(final_state.read(3), 8);
}

TEST(ControlFlowTest, Call3) {
  int cycles;
  auto final_state = RunProgram(R"(
            movi 1 r3
            call3
            call3
            call3
    (4)     movi 10 r3
            ret
    (20)    jump e
    (40)    movi 5 r3
            ret
    (56) e:
  )",
                                {}, &cycles);
  EXPECT_EQ(final_state.read(3), 5);
  EXPECT_EQ(cycles, 9);
}

TEST(ControlFlowTest, JumpRet) {
  auto final_state = RunProgram(R"(
        movi 0 r3
  loop: mov r3 r0
        call sqrt
        store r1 r3
        addi r3 1
        testi r3 != 0
      + jump loop
        jump end

  sqrt: movi 0 r1
        testi r0 >= 1
        +addi 1 r1
        testi r0 >= 4
        +addi 1 r1
        testi r0 >= 9
        +addi 1 r1
        ret
  end:

  )");

  for (int i = 0; i <= 15; ++i) {
    EXPECT_EQ(final_state.load(0, i), int(sqrt(i))) << i;
  }
}

TEST(ControlFlowTest, JumpRet2) {
  auto final_state = RunProgram(R"(
        movi 3 r0
        movi 4 r1
        call mul
        jump end
  mul:  movi 0 r2
  mull: testi r0 == 0
      + ret
        call add
        subi 1 r0
        jump mull
  add:  mov r1 r3
  addl: testi r3 == 0
      + ret
        call inc
        subi 1 r3
        jump addl
  inc:  addi 1 r2
        ret
  end:
  )");
  EXPECT_EQ(final_state.read(2), 12);
}

TEST(ControlFlowTest, BasicLoop) {
  auto final_state = RunProgram(R"(
    movi 4 r0
  loop:
    add r0 r1
    subi 1 r0
    testi 0 != r0
  + jump loop
  )");

  EXPECT_EQ(final_state.read(0), 0);
  EXPECT_EQ(final_state.read(1), 10);
}

}  // namespace
}  // namespace isa
