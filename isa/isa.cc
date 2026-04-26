#include <immintrin.h>

#include <bitset>
#include <map>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

struct InstructionMask {
  uint32_t opcode_mask;
  uint32_t opcode;
  int operand_bits;  // Not counting the predication bit.
};

constexpr InstructionMask operator""_enc(unsigned long long v) {
  // static_assert(v < 00'77777'77777);
  int operand_bits = __builtin_ctz(v + 1) / 3;

  uint32_t opcode_mask = 0b0'11111'11111 & ~((1 << operand_bits) - 1);
  uint32_t opcode = _pext_u32(v, 01'11111'11111) & opcode_mask;
  return InstructionMask{opcode_mask, opcode, operand_bits};
};

enum FieldSemantics {
  kPredication,

  kRegReadAddr1,
  kRegReadAddr2,
  kRegWriteAddr,

  kAluCmp,
  kAluLhs,
  kAluRhs,
};

enum class InstrSemantics {
  kAluZeroLhs,
  kAluSub,  // If not set, alu ads
  kAluShr,
  kAluNeg,  // (~N + 1)
  kJump,
  kPushPc,
  kPopPc,
  kPushReg,
  kPopReg,
};

struct Field {
  uint32_t mask;
  std::vector<FieldSemantics> semantics;
};

class IsaBuilder {
 public:
  IsaBuilder() : used_(1 << 11, false) {}

  void DefineField(std::string field, uint32_t mask,
                   std::vector<FieldSemantics> semantics);

  absl::Status DefineInstruction(std::string_view mnemonic,
                                 InstructionMask mask,
                                 absl::Span<const std::string> params,
                                 absl::Span<const InstrSemantics> semantics) {
    for (uint32_t p = 0; p <= 1 << 10; p += 1 << 10) {  // predicate bit
      for (uint32_t i = 0; i < (1ul << mask.operand_bits); ++i) {
        if (used_[mask.opcode + i + p]) {
          return absl::InvalidArgumentError("Duplicate bitcode.");
        }

        used_[mask.opcode + i + p] = true;
      }
    }

    absl::flat_hash_set<FieldSemantics> used_semantics;
    uint32_t parammask = (1 << 10) | ((1ul << mask.operand_bits) - 1);
    for (const auto& param : params) {
      const auto& field = fields_.at(param);
      if ((parammask & field.mask) != field.mask) {
        return absl::InvalidArgumentError(
            absl::StrCat("Field ", param, " not in parameter mask."));
      }
      for (auto sem : field.semantics) {
        if (!used_semantics.insert(sem).second) {
          return absl::InvalidArgumentError(absl::StrCat("Reused ", sem, "."));
        }
      }

      parammask &= ~field.mask;
    }

    if (parammask != 0) {
      return absl::InvalidArgumentError("Unused parameter bits.");
    }

    std::cout << mnemonic << ": " << std::bitset<11>(mask.opcode_mask) << ", "
              << std::bitset<11>(mask.opcode) << ", " << mask.operand_bits
              << "\n";
    std::cout << "    ";

    if (used_semantics.count(FieldSemantics::kRegReadAddr1)) std::cout << "r1 ";
    if (used_semantics.count(FieldSemantics::kRegReadAddr2)) std::cout << "r2 ";
    if (used_semantics.count(FieldSemantics::kRegWriteAddr)) std::cout << "w ";
    if (used_semantics.count(FieldSemantics::kAluCmp)) std::cout << "cmp ";
    if (used_semantics.count(FieldSemantics::kAluLhs)) std::cout << "lhs ";
    if (used_semantics.count(FieldSemantics::kAluRhs)) std::cout << "rhs ";

    std::cout << "\n";

    return absl::OkStatus();
  }

  int total_num_instructions() const {
    return std::count(used_.begin(), used_.end(), true);
  }

 private:
  absl::flat_hash_map<std::string, Field> fields_;
  std::vector<bool> used_;
};

void IsaBuilder::DefineField(std::string field, uint32_t mask,
                             std::vector<FieldSemantics> semantics) {
  bool inserted =
      fields_.emplace(std::move(field), Field{mask, std::move(semantics)})
          .second;
  (void)inserted;
  assert(inserted);
}

int main(int argc, const char* argv[]) {
  // The architecture is somewhat defined by manufacturing constraints. Since I
  // always have to get 5 of each board, we can use the leftover boards for
  // useful features (e.g. the four spare instruction counters will make a basic
  // call stack).

  IsaBuilder builder;

  // clang-format off
  builder.DefineField("pred",      0b10000000000, {kPredication});
  builder.DefineField("imm",       0b00000001111, {kAluRhs});
  builder.DefineField("lhsimm",    0b00011000000, {kRegReadAddr1, kAluLhs});  // lhs when imm is used
  builder.DefineField("dstimm",    0b00000110000, {kRegReadAddr1, kAluLhs, kRegWriteAddr});  // dst when imm is used
  builder.DefineField("cmp",       0b00000110000, {kAluCmp});  // e, gt, ne, le
  builder.DefineField("lhs",       0b00000001100, {kRegReadAddr1, kAluLhs});
  builder.DefineField("rhs",       0b00000000011, {kRegReadAddr2, kAluRhs});

  builder.DefineField("src",       0b00000000011, {kRegReadAddr2, kAluRhs});
  builder.DefineField("dst",       0b00000001100, {kRegReadAddr1, kAluLhs, kRegWriteAddr});
  builder.DefineField("dstnosrc",  0b00000000011, {kRegReadAddr1, kRegWriteAddr, kAluRhs});
  builder.DefineField("bit",       0b00000001100, {});
  builder.DefineField("val",       0b00000010000, {});
  builder.DefineField("deref-src", 0b00000010000, {});

  builder.DefineField("jumpaddr",  0b00000111111, {});
  builder.DefineField("bank",      0b00000000001, {});
  // clang-format on

  // rsrc/[rsrc], rdst
  std::vector<std::string> reg_or_mem_to_reg{"pred", "deref-src", "src", "dst"};
  std::vector<std::string> mem_to_reg{"pred", "src", "dst"};
  std::vector<std::string> imm_to_reg{"pred", "imm", "dstimm"};
  std::vector<std::string> reg_to_reg{"pred", "lhs", "rhs"};
  std::vector<std::string> cmp_reg_to_reg{"pred", "lhs", "rhs", "cmp"};
  std::vector<std::string> cmp_reg_to_imm{"pred", "imm", "lhsimm", "cmp"};

  std::vector<std::string> jump{"pred", "jumpaddr"};
  std::vector<std::string> testbit{"pred", "bit", "val", "rhs"};
  std::vector<std::string> wait{"pred", "val", "src", "dst"};

#define C ABSL_CHECK_OK
  using I = InstrSemantics;
  C(builder.DefineInstruction("addi", 0'0000'777777_enc,  // b = b + imm
                              imm_to_reg, {}));           // b = imm
  C(builder.DefineInstruction("movi", 0'0001'777777_enc, imm_to_reg,
                              {I::kAluZeroLhs}));
  C(builder.DefineInstruction("add", 0'00100'77777_enc,  // b = b + a
                              reg_or_mem_to_reg, {}));
  C(builder.DefineInstruction("tbit", 0'00101'77777_enc,  // flag = (b[n] == v)
                              testbit, {I::kAluZeroLhs}));
  C(builder.DefineInstruction("mov", 0'00110'77777_enc,  // b = a/[a]
                              reg_or_mem_to_reg, {I::kAluZeroLhs}));
  C(builder.DefineInstruction("movm", 0'001110'7777_enc,       // [b] = a
                              mem_to_reg, {I::kAluZeroLhs}));  // TODO deref
  C(builder.DefineInstruction("ldgpi", 0'001111'7777_enc,     // b = gpi [a]
                              mem_to_reg, {}));                // TODO
  C(builder.DefineInstruction("testi", 0'01'77777777_enc,      // a <cmp> imm
                              cmp_reg_to_imm, {I::kAluSub}));
  C(builder.DefineInstruction("subri", 0'1000'777777_enc,  // b = imm - b
                              imm_to_reg, {I::kAluSub, I::kAluNeg}));
  C(builder.DefineInstruction("subit",
                              0'1001'777777_enc,  // b = b - imm, test 0
                              imm_to_reg, {I::kAluSub}));
  C(builder.DefineInstruction("jump", 0'1010'777777_enc,  // jump
                              jump, {I::kJump}));
  C(builder.DefineInstruction("call", 0'1011'777777_enc,  // call
                              jump, {I::kJump, I::kPushPc}));
  C(builder.DefineInstruction("test", 0'1100'777777_enc,  // a <cmp> b
                              cmp_reg_to_reg, {I::kAluSub}));
  C(builder.DefineInstruction("subi", 0'1101'777777_enc,  // b = b - imm
                              imm_to_reg, {I::kAluSub}));
  C(builder.DefineInstruction("sub", 0'11100'77777_enc,  // b = b - a
                              reg_or_mem_to_reg, {I::kAluSub}));
  C(builder.DefineInstruction("subr", 0'11101'77777_enc,  // b = a - b
                              reg_or_mem_to_reg, {I::kAluSub, I::kAluNeg}));
  C(builder.DefineInstruction("wait",
                              0'11110'77777_enc,  // wait for gpi [a] =/!= b
                              wait, {}));         // TODO
  C(builder.DefineInstruction("retpop", 0'11111000'77_enc,  // pop r; ret
                              {"pred", "dstnosrc"}, {I::kPopPc, I::kPopReg}));
  C(builder.DefineInstruction("pop", 0'11111001'77_enc,  // pop
                              {"pred", "dstnosrc"}, {I::kPopReg}));
  C(builder.DefineInstruction("flagget", 0'11111010'77_enc,  // r = flag
                              {"pred", "dstnosrc"}, {}));    // TODO
  C(builder.DefineInstruction("not", 0'11111011'77_enc,      // r = ~r
                              {"pred", "dstnosrc"}, {}));    // TODO alu flags
  C(builder.DefineInstruction("shr", 0'11111100'77_enc, {"pred", "dstnosrc"},
                              {I::kAluShr, I::kAluZeroLhs}));
  C(builder.DefineInstruction("flagset", 0'11111101'77_enc,  // flag = reg[0]
                              {"pred", "rhs"}, {}));         // TODO
  C(builder.DefineInstruction("push", 0'11111110'77_enc,     // push
                              {"pred", "rhs"}, {I::kPushReg}));
  C(builder.DefineInstruction("membank",
                              0'111111110'7_enc,        // select memory bank
                              {"pred", "bank"}, {}));   // TODO
  C(builder.DefineInstruction("ret", 0'1111111110_enc,  // ret
                              {"pred"}, {I::kPopPc}));
  C(builder.DefineInstruction("invflag", 0'1111111111_enc,  // flag = !flag
                              {"pred"}, {}));               // TODO
#undef C

  std::cout << "Total number of instructions: "
            << builder.total_num_instructions() << "\n";
}
