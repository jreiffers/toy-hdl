#include "isa/emulator.h"

#include <deque>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "isa/encdec.h"
#include "isa/visitor.h"

namespace isa {

std::optional<int> GetReadPort(absl::Span<const FieldSemantics> field) {
  for (auto f : field) {
    if (f == FieldSemantics::kRegReadAddr0) return 0;
    if (f == FieldSemantics::kRegReadAddr1) return 1;
  }
  return std::nullopt;
}

bool IsImmediate(absl::Span<const FieldSemantics> field) {
  return absl::c_contains(field, FieldSemantics::kImmediate);
}

enum class AluInput {
  kReadPort0,
  kReadPort1,
  kImm,
  kZero,
  kFlag,
};

std::optional<AluInput> InferAluInput(absl::Span<const FieldSemantics> field) {
  if (IsImmediate(field)) return AluInput::kImm;
  auto rd_port = GetReadPort(field);
  if (!rd_port) return std::nullopt;
  return *rd_port == 0 ? AluInput::kReadPort0 : AluInput::kReadPort1;
}

struct ParsedFields {
  static absl::StatusOr<ParsedFields> Get(
      absl::Span<const uint32_t> args,
      absl::Span<const absl::Span<const FieldSemantics>> field_semantics) {
#define SET(c, target)                                                       \
  case FieldSemantics::c:                                                    \
    if (target) return absl::InvalidArgumentError("Duplicate " #target "."); \
    target = static_cast<std::remove_cvref_t<decltype(*target)>>(args[i]);   \
    break

#define SET_ALU(c, target)                                                   \
  case FieldSemantics::c:                                                    \
    if (target) return absl::InvalidArgumentError("Duplicate " #target "."); \
    target = InferAluInput(field_semantics[i]);                              \
    if (!target) return absl::InvalidArgumentError("Invalid " #target ".");  \
    break

    ParsedFields ret;
    for (int i = 0; i < args.size(); ++i) {
      for (auto f : field_semantics[i]) {
        switch (f) {
          SET(kPredication, ret.pred);
          SET(kRegReadAddr0, ret.ra0);
          SET(kRegReadAddr1, ret.ra1);
          SET(kRegWriteAddr, ret.wa);
          SET(kImmediate, ret.imm);
          SET(kAluCmp, ret.comparator);
          SET(kTestBitIdx, ret.test_bit_idx);
          SET(kTestBitVal, ret.test_bit_val);
          SET(kDerefSrc, ret.deref_src);
          SET(kJumpAddr, ret.jmp_addr);
          SET(kMemBankIdx, ret.mem_bank_idx);

          SET_ALU(kAluLhs, ret.alu_lhs);
          SET_ALU(kAluRhs, ret.alu_rhs);
        }
      }
    }
    return ret;
  }

  std::optional<uint4_t> ReadPort(int i, const MachineState& state) {
    auto port = i == 0 ? ra0 : ra1;
    if (!port) return std::nullopt;
    return state.registers[*port];
  }

  std::optional<uint4_t> GetAluInput(std::optional<AluInput> input,
                                     const MachineState& state) {
    if (!input) return std::nullopt;
    switch (*input) {
      case AluInput::kReadPort0:
        return ReadPort(0, state);
      case AluInput::kReadPort1:
        return ReadPort(1, state);
      case AluInput::kImm:
        return imm;
      case AluInput::kZero:
        return 0;
      case AluInput::kFlag:
        return state.flag;
    }
  }

  std::optional<bool> pred = std::nullopt;
  std::optional<int> ra0 = std::nullopt;
  std::optional<int> ra1 = std::nullopt;
  std::optional<int> wa = std::nullopt;
  std::optional<uint4_t> imm = std::nullopt;
  std::optional<Comparator> comparator = std::nullopt;
  std::optional<AluInput> alu_lhs = std::nullopt;
  std::optional<AluInput> alu_rhs = std::nullopt;
  std::optional<int> test_bit_idx = std::nullopt;
  std::optional<bool> test_bit_val = std::nullopt;
  std::optional<bool> deref_src = std::nullopt;
  std::optional<uint6_t> jmp_addr = std::nullopt;
  std::optional<int> mem_bank_idx = std::nullopt;
};

absl::Status Emulator::Op(
    std::string_view mnemonic, absl::Span<const uint32_t> args,
    absl::Span<const InstrSemantics> instr_semantics,
    absl::Span<const absl::Span<const FieldSemantics>> field_semantics) {
  auto maybe_f = ParsedFields::Get(args, field_semantics);
  if (!maybe_f.ok()) return maybe_f.status();
  auto f = std::move(*maybe_f);

  bool alu_shr = false;
  bool alu_not = false;
  bool alu_carry_in = false;
  bool alu_not_rhs = false;

  bool jump = false;
  bool push_pc = false;
  bool pop_pc = false;

  bool push_reg = false;
  bool pop_reg = false;  // TODO go through ALU?

  bool st_mem = false;
  bool ld_gpi = false;

  bool wait = false;
  bool flag_get = false;
  bool flag_set = false;

  bool mem_bank_set = false;

  // Yikes, but doing this properly would be too annoying right now.
  if (mnemonic == "subit") {
    // Needs circuitry right now. Moving cmp and/or changing the subit opcode
    // could save a few transistors.
    f.comparator = static_cast<Comparator>(0);
  }
  if (mnemonic == "invflag") {
    // Decodes instruction as comparator, doesn't need circuitry.
    f.comparator = static_cast<Comparator>(3);
  }

#undef SET
#define SET(c, target)    \
  case InstrSemantics::c: \
    target = true;        \
    break;

  for (auto i : instr_semantics) {
    switch (i) {
      case InstrSemantics::kAluZeroLhs:
        f.alu_lhs = AluInput::kZero;
        break;
        SET(kAluNotRhs, alu_not_rhs);
        SET(kAluShr, alu_shr);
        SET(kAluNot, alu_not);
        SET(kAluCarryIn, alu_carry_in);
        SET(kJump, jump);
        SET(kPushPc, push_pc);
        SET(kPopPc, pop_pc);
        SET(kPushReg, push_reg);
        SET(kPopReg, pop_reg);
        SET(kStMem, st_mem);
        SET(kLdGpi, ld_gpi);
        SET(kWait, wait);
        SET(kFlagGet, flag_get);
        SET(kFlagSet, flag_set);
        SET(kMemBankSet, mem_bank_set);
    }
  }

  if (!f.pred) {
    return absl::InvalidArgumentError("Did not get a pred flag.");
  }

  if (*f.pred && !state_.flag) {
    state_.pc = state_.pc + 1;
    return absl::OkStatus();
  }

  std::optional<uint4_t> alu_lhs = f.GetAluInput(f.alu_lhs, state_);
  if (f.deref_src && *f.deref_src) {
    if (alu_lhs) {
      alu_lhs = state_.Load(*alu_lhs);
    } else {
      return absl::InvalidArgumentError("Attempted deref of undef.");
    }
  }

  if (alu_not && alu_shr) {
    return absl::InvalidArgumentError("neg & shr unsupported");
  }

  std::optional<uint4_t> alu_rhs = f.GetAluInput(f.alu_rhs, state_);
  std::optional<uint4_t> alu_res = std::nullopt;

  std::optional<bool> alu_eq = std::nullopt;
  std::optional<bool> alu_ge = std::nullopt;

  if (ld_gpi) {
    if (!alu_rhs) {
      return absl::InvalidArgumentError("attempted to load undef gpi.");
    }
    alu_rhs = state_.gpi[*alu_rhs];
  }

  if (flag_get) {
    alu_rhs = state_.flag * 15;
  }

  if (mem_bank_set) {
    if (!f.mem_bank_idx) {
      return absl::InvalidArgumentError(
          "attempted to set RAM bank without an index");
    }
    state_.bank = *f.mem_bank_idx;
  }

  if (alu_lhs && alu_rhs) {
    uint32_t wide_out;

    if (alu_not_rhs) {
      alu_rhs = ~*alu_rhs;
    }

    wide_out = *alu_lhs + *alu_rhs;
    if (alu_carry_in) {
      ++wide_out;
    }
    if (alu_not) {
      wide_out = ~wide_out;
    }
    alu_res = wide_out;

    if (alu_shr) {
      alu_res = *alu_res >> 1;
    }

    alu_eq = *alu_res == 0;
    alu_ge = (wide_out & 16) == 16;
  }

  if (pop_reg) {
    alu_res = state_.bak;
  }

  if (push_reg) {
    if (!alu_res) {
      return absl::InvalidArgumentError("attempted to push undef.");
    }
    state_.bak = *alu_res;
  }

  if (st_mem) {
    if (!alu_res) {
      return absl::InvalidArgumentError("attempted to store undef.");
    }
    auto addr = f.ReadPort(0, state_);
    if (!addr) {
      return absl::InvalidArgumentError("attempted to store to undef.");
    }
    state_.Store(*addr, *alu_res);
  }

  if (wait) {
    if (!alu_eq) {
      return absl::InvalidArgumentError("attempted to wait with invalid flag.");
    }
    if (!f.test_bit_val) {
      return absl::InvalidArgumentError("testbit is undef.");
    }
    if (*alu_eq == *f.test_bit_val) {
      state_.pc = state_.pc + 1;
    }
  } else {
    state_.pc = state_.pc + 1;
  }

  if (push_pc) {
    if (state_.call_stack.size() >= 4) {
      return absl::InvalidArgumentError("call stack overflow");
    }
    state_.call_stack.push_back(state_.pc);
  }

  if (jump) {
    if (!f.jmp_addr) {
      return absl::InvalidArgumentError(
          "attempted jump without defined target.");
    }
    state_.pc = *f.jmp_addr;
  }

  if (pop_pc) {
    if (state_.call_stack.empty()) {
      return absl::InvalidArgumentError("call stack underflow");
    }
    state_.pc = state_.call_stack.back();
    state_.call_stack.pop_back();
  }

  if (f.wa) {
    if (!alu_res) {
      return absl::InvalidArgumentError(
          "attempted to write undef to register.");
    }
    state_.registers[*f.wa] = *alu_res;
  }

  if (flag_set) {
    if (!f.comparator) {
      return absl::InvalidArgumentError(
          "Attempted to set flags, but no comparator was set.");
    }

    switch (*f.comparator) {
      case Comparator::kEq:
      case Comparator::kNe:
        if (!alu_eq) {
          return absl::InvalidArgumentError("ALU did not produce a flag.");
        }
        state_.flag = *alu_eq == (*f.comparator == Comparator::kEq);
        break;
      case Comparator::kGe:
      case Comparator::kLt:
        if (!alu_ge) {
          return absl::InvalidArgumentError("ALU did not produce a flag.");
        }
        state_.flag = *alu_ge == (*f.comparator == Comparator::kGe);
        break;
    }
  }

  std::cerr << absl::StrFormat("executed %6s: %v\n", mnemonic, state_);

  return absl::OkStatus();
}

absl::Status Emulator::step() {
  if (state_.pc > rom_.size()) {
    return absl::InvalidArgumentError("PC out of bounds.");
  }
  return DecodeInstruction(rom_[state_.pc], *this);
}

}  // namespace isa
