#include "isa/emulator.h"

#include <deque>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpu/alu.h"
#include "isa/encdec.h"
#include "isa/instructions.h"
#include "isa/visitor.h"

namespace isa {

struct ParsedFields {
  static absl::StatusOr<ParsedFields> Get(
      uint32_t instr, absl::Span<const uint32_t> args,
      absl::Span<const absl::Span<const FieldSemantics>> field_semantics) {
#define SET(c, target)                                                       \
  case FieldSemantics::c:                                                    \
    if (target) return absl::InvalidArgumentError("Duplicate " #target "."); \
    target = static_cast<std::remove_cvref_t<decltype(*target)>>(args[i]);   \
    break

    ParsedFields ret;
    ret.ra0 =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegReadAddr0));
    ret.ra1 =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegReadAddr1));
    ret.wa = _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegWriteAddr));
    ret.imm = _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kImmediate));
    ret.comparator = static_cast<Comparator>(
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kAluCmp)));
    ret.jmp_addr =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kJumpAddr));
    ret.test_bit_val =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kTestBitVal));
    ret.pred =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kPredication));

    ret.imm_valid = false;
    ret.write_enable = false;

    for (int i = 0; i < args.size(); ++i) {
      for (auto f : field_semantics[i]) {
        switch (f) {
          SET(kDerefSrc, ret.deref_src);

          case FieldSemantics::kRegWriteAddr:
            ret.write_enable = true;
            break;
          case FieldSemantics::kImmediate:
            ret.imm_valid = true;
            break;
        }
      }
    }
    return ret;
  }

  bool pred;
  int ra0;
  int ra1;
  int wa;
  bool write_enable;
  uint4_t imm;
  bool imm_valid;
  Comparator comparator;
  bool test_bit_val;
  std::optional<bool> deref_src = std::nullopt;
  uint16_t jmp_addr;
};

absl::Status Emulator::Op(
    std::string_view mnemonic, absl::Span<const uint32_t> args,
    absl::Span<const InstrSemantics> instr_semantics,
    absl::Span<const absl::Span<const FieldSemantics>> field_semantics) {
  auto maybe_f = ParsedFields::Get(rom_[state().pc()], args, field_semantics);
  if (!maybe_f.ok()) return maybe_f.status();
  auto f = std::move(*maybe_f);

  bool alu_shr = false;
  bool alu_not = false;
  bool alu_and = false;
  bool alu_carry_in = false;
  bool alu_zero_lhs = false;
  bool alu_zero_rhs = false;
  bool alu_not_rhs = false;

  bool jump = false;
  bool indirect = false;
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
  bool rom_bank_set = false;

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
      case InstrSemantics::kCmpNz:
        f.comparator = Comparator::kNe;
        break;
        SET(kAluZeroLhs, alu_zero_lhs);
        SET(kAluZeroRhs, alu_zero_rhs);
        SET(kAluNotRhs, alu_not_rhs);
        SET(kAluShr, alu_shr);
        SET(kAluNot, alu_not);
        SET(kAluAnd, alu_and);
        SET(kAluCarryIn, alu_carry_in);
        SET(kJump, jump);
        SET(kIndirect, indirect);
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
        SET(kRomBankSet, rom_bank_set);
    }
  }

  bool pred_mismatch = f.pred && !state().flag();

  uint4_t rd_port_0 = state().read(f.ra0);
  uint4_t rd_port_1 = state().read(f.ra1);

  if (f.deref_src && *f.deref_src) {
    rd_port_1 = state().load(rd_port_1);
  }

  if (alu_not && alu_shr) {
    return absl::InvalidArgumentError("neg & shr unsupported");
  }

  if (ld_gpi) {
    rd_port_1 = state().load_gpi(rd_port_1);
  }

  if (flag_get) {
    rd_port_1 = state().flag() * 15;
  }

  auto ret =
      Alu<4>::spec({rd_port_0, rd_port_1, f.imm, !alu_zero_lhs,
                    alu_zero_rhs ? 0u : (alu_not_rhs ? 1u : 2u), f.imm_valid,
                    alu_carry_in, alu_and, alu_not, alu_shr});

  uint4_t alu_res = ret[0];
  bool alu_ge = ret[1];
  bool alu_eq = ret[2];

  if (!pred_mismatch) {
    if (mem_bank_set) state().set_membank(rd_port_0);
    if (rom_bank_set) state().set_rombank(rd_port_0);
    if (pop_reg) alu_res = state().pop();
    if (push_reg) state().push(alu_res);
    if (st_mem) state().store(rd_port_0, alu_res);
    if (push_pc) state().push_pc();
    if (pop_pc) state().pop_pc();
  }

  {
    bool next_instruction = !wait || (alu_eq == f.test_bit_val);
    // a = current PC
    // b = direct jump addr
    // c = indirect jump addr
    uint32_t pc = state().pc();
    uint32_t b_lut = jump && !pred_mismatch ? 2 : 0;
    uint32_t ja = f.jmp_addr;
    bool carry_in = pred_mismatch || (next_instruction && !jump);
    bool a_enable = pred_mismatch || !jump;
    bool c_enable = indirect;
    uint4_t rb = state().rombank();

    std::array<uint32_t, 10> alu_vals_0{
        pc & 0b11, ja & 0b11, 0,     a_enable, b_lut,
        c_enable,  carry_in,  false, false,    false};
    uint32_t next_pc_0 = Alu<4>::spec(alu_vals_0)[0];
    std::array<uint32_t, 10> alu_vals_1{
        (pc >> 2) & 0b1111,   ja >> 2, rd_port_1, a_enable, b_lut, c_enable,
        (next_pc_0 >> 2) & 1, false,   false,     false};
    uint32_t next_pc_1 = Alu<4>::spec(alu_vals_1)[0];
    bool next_pc_carry = Alu<4>::spec(alu_vals_1)[1];
    std::array<uint32_t, 10> alu_vals_2{
        (pc >> 6) & 0b1111, 0,     rb,    a_enable, b_lut, true,
        next_pc_carry,      false, false, false};
    uint32_t next_pc_2 = Alu<4>::spec(alu_vals_2)[0];

    state().set_pc((next_pc_2 << 6) | (next_pc_1 << 2) | (next_pc_0 & 0b11));
  }

  state().write(f.wa, (!pred_mismatch && f.write_enable) ? alu_res : rd_port_0);

  if (flag_set && !pred_mismatch) {
    switch (f.comparator) {
      case Comparator::kEq:
      case Comparator::kNe:
        state().set_flag(alu_eq == (f.comparator == Comparator::kEq));
        break;
      case Comparator::kGe:
      case Comparator::kLt:
        state().set_flag(alu_ge == (f.comparator == Comparator::kGe));
        break;
    }
  }

  std::cerr << absl::StrFormat("executed %6s: %v\n", mnemonic, state());

  return absl::OkStatus();
}

absl::Status Emulator::step() {
  if (state().pc() >= rom_.size()) {
    return absl::InvalidArgumentError("PC out of bounds.");
  }
  return DecodeInstruction(rom_[state().pc()], *this);
}

}  // namespace isa
