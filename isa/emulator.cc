#include "isa/emulator.h"

#include <deque>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpu/alu.h"
#include "cpu/decoder.h"
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
    ret.deref_src = false;
    for (auto em : field_semantics) {
      ret.deref_src |=
          absl::c_contains(em, FieldSemantics::kDerefSrc) &&
          _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kDerefSrc));
    }
    return ret;
  }

  bool pred;
  int ra0;
  int ra1;
  int wa;
  uint4_t imm;
  Comparator comparator;
  bool test_bit_val;
  bool deref_src;
  uint16_t jmp_addr;
};

absl::Status Emulator::Op(
    std::string_view mnemonic, absl::Span<const uint32_t> args,
    absl::Span<const InstrSemantics> instr_semantics,
    absl::Span<const absl::Span<const FieldSemantics>> field_semantics) {
  auto maybe_f = ParsedFields::Get(rom_[state().pc()], args, field_semantics);
  if (!maybe_f.ok()) return maybe_f.status();
  auto f = std::move(*maybe_f);

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

  if (f.deref_src) {
    rd_port_1 = state().load(rd_port_1);
  }

  if (ld_gpi) {
    rd_port_1 = state().load_gpi(rd_port_1);
  }

  if (flag_get) {
    rd_port_1 = state().flag() * 15;
  }

  auto dec = Decoder::spec({rom_[state().pc()], pred_mismatch});
  auto ret = Alu<4>::spec({rd_port_0, rd_port_1, f.imm, dec.alu_flags});
  uint4_t alu_res = ret.res.value();
  bool alu_ge = ret.carry_out.value();
  bool alu_eq = ret.zero.value();

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
    uint32_t ja = f.jmp_addr;
    bool carry_in = pred_mismatch || (next_instruction && dec.no_jump.value());
    uint4_t rb = state().rombank();

    AluFlags<Integer> jump_flags{dec.no_jump,  dec.no_jump.value() ? 0 : 2,
                                 dec.indirect, carry_in,
                                 false,        false,
                                 false};

    auto r0 = Alu<4>::spec({pc & 0b11, ja & 0b11, 0, jump_flags});
    jump_flags.carry_in = r0.res[2];
    auto r1 =
        Alu<4>::spec({(pc >> 2) & 0b1111, ja >> 2, rd_port_1, jump_flags});
    jump_flags.carry_in = r1.carry_out;
    auto r2 = Alu<4>::spec({pc >> 6, rb, rb, jump_flags});

    state().set_pc((r2.res.value() << 6) | (r1.res.value() << 2) |
                   (r0.res.value() & 0b11));
  }

  state().write(f.wa, dec.write_enable.value() ? alu_res : rd_port_0);

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
