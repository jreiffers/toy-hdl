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
  while (time < kMaxSteps && emulator.state().pc < instrs.size()) {
    auto change = gpi_changes.find(time);
    if (change != gpi_changes.end()) {
      auto [addr, val] = change->second;
      emulator.state().gpi[addr] = val;
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
      EXPECT_TRUE(verifier(RunProgram(StrFormat(pattern, i, j)), i, j))
          << StrFormat(pattern, i, j);
    }
  }
}

bool Eq(const MachineState& state, int i, int j) {
  return state.flag == (i == j);
}

bool Lt(const MachineState& state, int i, int j) {
  return state.flag == (i < j);
}

bool Ne(const MachineState& state, int i, int j) {
  return state.flag == (i != j);
}

bool Ge(const MachineState& state, int i, int j) {
  return state.flag == (i >= j);
}

template <int r>
bool Sub(const MachineState& state, int i, int j) {
  return state.registers[r] == ((i - j) & 15);
}

template <int r>
bool Subr(const MachineState& state, int i, int j) {
  return state.registers[r] == ((j - i) & 15);
}

template <int r>
bool And(const MachineState& state, int i, int j) {
  return state.registers[r] == (i & j);
}

template <int r>
bool Subtnz(const MachineState& state, int i, int j) {
  return state.registers[r] == ((i - j) & 15) && state.flag == ((i - j) != 0);
}

bool Andtnz(const MachineState& state, int i, int j) {
  return state.flag == ((i & j) != 0);
}

TEST(EmulatorTest, Add) {
  auto final_state = RunProgram(R"(
    addi 4 r0
    addi 2 r0
    add r0 r1
    add r1 r1
  )");

  EXPECT_EQ(final_state.registers[0], 6);
  EXPECT_EQ(final_state.registers[1], 12);
}

TEST(EmulatorTest, And) {
  TestAll2("movi %d r0 movi %d r1 and r0 r1", And<1>);
  TestAll2("movi %d r0 movi %d r1 andtnz r0 r1", Andtnz);

  auto final_state = RunProgram(R"(
    movi 5 r0
    movi 3 r1
    andtnz r0 r1
  )");

  EXPECT_EQ(final_state.registers[0], 5);
  EXPECT_EQ(final_state.registers[1], 3);
  EXPECT_EQ(final_state.flag, true);
}

TEST(EmulatorTest, Mov) {
  auto final_state = RunProgram(R"(
    movi 4 r0
    mov r0 r3
  )");

  EXPECT_EQ(final_state.registers[0], 4);
  EXPECT_EQ(final_state.registers[3], 4);
}

TEST(EmulatorTest, LoadStore) {
  auto final_state = RunProgram(R"(
    movi 7 r0
    movi 3 r1
    store r0 r1
    mov [r0] r2
  )");

  EXPECT_EQ(final_state.registers[2], 7) << absl::StrCat(final_state);
}

TEST(EmulatorTest, Ldgpi) {
  auto final_state = RunProgram(R"(
    movi 7 r0    // t = 0
    ldgpi r0 r1  // t = 1
    ldgpi r0 r2  // t = 2
    add r1 r2
  )",
                                {{0, {7, 1}}, {1, {7, 2}}, {2, {7, 3}}});

  EXPECT_EQ(final_state.registers[2], 5) << absl::StrCat(final_state);
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
  EXPECT_EQ(RunProgram("movi 5 r3   push r3   pop r2").registers[2], 5);
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
                .registers[2],
            3);
}

TEST(EmulatorTest, Not) {
  EXPECT_EQ(RunProgram("movi 1 r3  not r3").registers[3], 14);
}

TEST(EmulatorTest, Shr) {
  EXPECT_EQ(RunProgram("movi 15 r3  shr r3").registers[3], 7);
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

  EXPECT_EQ(final_state.ram[0][4], 4);
  EXPECT_EQ(final_state.ram[1][5], 5);
}

TEST(EmulatorTest, DISABLED_Rombank) {
  // NYI
  GTEST_FAIL();
}

TEST(EmulatorTest, Invflag) {
  EXPECT_EQ(RunProgram("test r0 == r1 invflag").flag, false);
  EXPECT_EQ(RunProgram("test r0 != r1 invflag").flag, true);
}

TEST(ControlFlowTest, DISABLED_Jump3) {
  // NYI
  GTEST_FAIL();
}

TEST(ControlFlowTest, DISABLED_Call3) {
  // NYI
  GTEST_FAIL();
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
  EXPECT_THAT(final_state.ram[0],
              ElementsAre(0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3));
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
  EXPECT_EQ(final_state.registers[2], 12);
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

  EXPECT_EQ(final_state.registers[0], 0);
  EXPECT_EQ(final_state.registers[1], 10);
}

}  // namespace
}  // namespace isa
