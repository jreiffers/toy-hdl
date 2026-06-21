#ifndef DECODER_H__
#define DECODER_H__

#include "absl/container/linked_hash_map.h"
#include "cpu/alu.h"
#include "cpu/gate_lib.h"
#include "cpu/types.h"
#include "isa/instructions.h"

namespace detail {

template <int bwout, int bwin>
void ExtractBits(uint32_t mask, GateReg<bwin> in, GateReg<bwout>& out) {
  assert(__builtin_popcount(mask) == bwout);
  int j = 0;
  for (int i = 0; i < bwin; ++i) {
    if ((mask >> i) & 1) {
      out[j++] = in[i];
    }
  }
}

}  // namespace detail

struct Decoder {
  template <template <int> class Ty>
  struct Outs {
    Ty<1> write_enable;
    Ty<2> cmp;
    Ty<1> no_jump;
    Ty<1> indirect;
    AluFlags<Ty> alu_flags;
  };

  template <template <int> class Ty>
  struct Args {
    Ty<11> instruction;
    Ty<1> pred_mismatch;
  };

  static Outs<Integer> spec(Args<Integer> args) {
    using namespace isa;

    uint32_t bits = args.instruction.value();
    bool pred_mismatch = args.pred_mismatch.value();

    const Instruction* it = nullptr;
    for (const auto& instr : isa::GetInstructions()) {
      if ((bits & instr.mask.opcode_mask) == instr.mask.opcode) {
        it = &instr;
      }
    }

    assert(it);

    uint32_t cmp = _pext_u32(bits, isa::GetFieldBits(FieldSemantics::kAluCmp));
    if (it->Has(InstrSemantics::kCmpNz)) {
      cmp = static_cast<uint32_t>(Comparator::kNe);
    }

    uint32_t b_lut = it->Has(InstrSemantics::kAluZeroRhs)
                         ? 0
                         : (it->Has(InstrSemantics::kAluNotRhs) ? 1 : 2);

    AluFlags<Integer> flags{!it->Has(InstrSemantics::kAluZeroLhs),
                            b_lut,
                            it->Has(FieldSemantics::kImmediate),
                            it->Has(InstrSemantics::kAluCarryIn),
                            it->Has(InstrSemantics::kAluAnd),
                            it->Has(InstrSemantics::kAluNot),
                            it->Has(InstrSemantics::kAluShr)};

    return {!pred_mismatch &&
                it->Has(FieldSemantics::kRegWriteAddr) /*write_enable*/,
            cmp, pred_mismatch || !it->Has(InstrSemantics::kJump) /*no_jump*/,
            it->Has(InstrSemantics::kIndirect), flags};
  }

  static Outs<GateReg> Build(GateNetwork& net, const Args<GateReg>& a) {
    using namespace detail;
    using namespace isa;

    ScopeGuard scope(net, "decoder");
    Outs<GateReg> decoder;

    auto pred_match = net.Not(a.pred_mismatch);

    const auto instruction = a.instruction;
    absl::linked_hash_map<const Instruction*, GateTerminal> instruction_sel;

    for (const auto& instr : isa::GetInstructions()) {
      ScopeGuard g(net, instr.mnemonic);

      std::vector<GateTerminal> mismatches;
      // TODO Generating a tree from the prefixes directly is probably better,
      // but the optimization passes can handle it maybe?
      for (int i = 0; i < 11; ++i) {
        int bit = 1 << i;
        if (instr.mask.opcode_mask & bit) {
          if (instr.mask.opcode & bit) {
            mismatches.push_back(net.Not(instruction[i]));
          } else {
            mismatches.push_back(instruction[i]);
          }
        }
      }

      instruction_sel[&instr] = net.Nor(mismatches);
    }

    auto has = [&](auto it) {
      std::vector<GateTerminal> sels;
      for (auto [instr, sel] : instruction_sel) {
        if (instr->Has(it)) {
          sels.push_back(sel);
        }
      }
      return net.Not(net.Nor(sels));
    };

    {
      ScopeGuard g(net, "alu_bits");
      auto& f = decoder.alu_flags;
      {
        ScopeGuard g2(net, "a_enable");
        f.a_enable = net.Not(has(InstrSemantics::kAluZeroLhs));
      }
      {
        ScopeGuard g2(net, "b_lut");
        auto nzrhs = net.Not(has(InstrSemantics::kAluZeroRhs));
        auto nrhs = has(InstrSemantics::kAluNotRhs);

        f.b_lut[0] = net.And(nzrhs, nrhs);
        f.b_lut[1] = net.And(nzrhs, net.Not(nrhs));
      }
      {
        ScopeGuard g2(net, "c_enable");
        f.c_enable = has(FieldSemantics::kImmediate);
      }
      {
        ScopeGuard g2(net, "cin");
        f.carry_in = has(InstrSemantics::kAluCarryIn);
      }
      {
        ScopeGuard g2(net, "compute_and");
        f.compute_and = has(InstrSemantics::kAluAnd);
      }
      {
        ScopeGuard g2(net, "compute_not");
        f.not_out = has(InstrSemantics::kAluNot);
      }
      {
        ScopeGuard g2(net, "shr");
        f.shr = has(InstrSemantics::kAluShr);
      }
    }

    {
      ScopeGuard g(net, "write_enable");
      decoder.write_enable =
          net.And(pred_match, has(FieldSemantics::kRegWriteAddr));
    }

    {
      ScopeGuard g(net, "comparator");
      ExtractBits<2>(GetFieldBits(FieldSemantics::kAluCmp), instruction,
                     decoder.cmp);
      auto cmp2 = has(InstrSemantics::kCmpNz);
      decoder.cmp[0] = net.And(decoder.cmp[0], net.Not(cmp2));
      decoder.cmp[1] = net.Or(decoder.cmp[1], cmp2);
    }

    {
      ScopeGuard g(net, "jump_bits");
      decoder.no_jump =
          net.Or(a.pred_mismatch, net.Not(has(InstrSemantics::kJump)));
      decoder.indirect = has(InstrSemantics::kIndirect);
    }

    return decoder;
  }
};

#endif
