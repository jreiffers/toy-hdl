#include "isa/emulator.h"

#include <deque>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "cpu/alu.h"
#include "cpu/decoder.h"
#include "isa/encdec.h"
#include "isa/instructions.h"
#include "isa/visitor.h"

namespace isa {

struct ParsedFields {
  static ParsedFields Get(uint32_t instr) {
    ParsedFields ret;
    ret.ra0 =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegReadAddr0));
    ret.ra1 =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegReadAddr1));
    ret.wa = _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kRegWriteAddr));
    ret.imm = _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kImmediate));
    ret.jmp_addr =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kJumpAddr));
    ret.test_bit_val =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kTestBitVal));
    ret.pred =
        _pext_u32(instr, isa::GetFieldBits(FieldSemantics::kPredication));
    return ret;
  }

  bool pred;
  int ra0;
  int ra1;
  int wa;
  uint4_t imm;
  bool test_bit_val;
  uint16_t jmp_addr;
};

absl::Status Emulator::Op(std::string_view mnemonic, absl::Span<const uint32_t>,
                          absl::Span<const InstrSemantics>,
                          absl::Span<const absl::Span<const FieldSemantics>>) {
  auto f = ParsedFields::Get(rom_[state().pc()]);
  bool pred_mismatch = f.pred && !state().flag();

  uint4_t rd_port_0 = state().read(f.ra0);
  uint4_t rd_port_1 = state().read(f.ra1);

  auto dec = Decoder::spec({rom_[state().pc()], pred_mismatch});

  if (dec.deref_src.value()) {
    rd_port_1 = state().load(rd_port_1);
  }

  if (dec.load_gpi.value()) {
    rd_port_1 = state().load_gpi(rd_port_1);
  }

  if (dec.flag_get.value()) {
    rd_port_1 = state().flag() * 15;
  }

  auto ret = Alu<4>::spec({rd_port_0, rd_port_1, f.imm, dec.alu_flags});
  uint4_t alu_res = ret.res.value();

  if (dec.membank_set.value()) state().set_membank(rd_port_0);
  if (dec.rombank_set.value()) state().set_rombank(rd_port_0);
  if (dec.push.value()) state().push(alu_res);
  if (dec.pop.value()) alu_res = state().pop();
  if (dec.store.value()) state().store(rd_port_0, alu_res);
  if (dec.push_pc.value()) state().push_pc();
  if (dec.pop_pc.value()) state().pop_pc();

  {
    bool next_instruction =
        !dec.wait.value() || (ret.zero.value() == f.test_bit_val);
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

  if (dec.flag_set.value()) {
    auto cmp = static_cast<Comparator>(dec.cmp.value());
    switch (cmp) {
      case Comparator::kEq:
      case Comparator::kNe:
        state().set_flag(ret.zero.value() == (cmp == Comparator::kEq));
        break;
      case Comparator::kGe:
      case Comparator::kLt:
        state().set_flag(ret.carry_out.value() == (cmp == Comparator::kGe));
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
