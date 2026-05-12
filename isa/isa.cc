#include <immintrin.h>

#include <bitset>
#include <map>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "isa_types.h"

ABSL_FLAG(std::string, mode, "", "Mode");

namespace isa {

constexpr InstructionMask operator""_enc(unsigned long long v) {
  // static_assert(v < 00'77777'77777);
  int operand_bits = __builtin_ctz(v + 1) / 3;

  uint32_t opcode_mask = 0b0'11111'11111 & ~((1 << operand_bits) - 1);
  uint32_t opcode = _pext_u32(v, 01'11111'11111) & opcode_mask;
  return InstructionMask{opcode_mask, opcode, operand_bits};
};

class IsaBuilder {
 public:
  IsaBuilder() : used_(1 << 11, false) {}

  void DefineField(std::string field, uint32_t mask, std::string type,
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
    std::vector<Field> fields;
    uint32_t parammask = (1 << 10) | ((1ul << mask.operand_bits) - 1);
    for (const auto& param : params) {
      const auto& field = fields_.at(param);
      fields.push_back(field);
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

    if (absl::GetFlag(FLAGS_mode) == "instructions") {
      std::cout << mnemonic << ": " << std::bitset<11>(mask.opcode_mask) << ", "
                << std::bitset<11>(mask.opcode) << ", " << mask.operand_bits
                << "\n";
      std::cout << "    ";

      if (used_semantics.count(FieldSemantics::kRegReadAddr0))
        std::cout << "r1 ";
      if (used_semantics.count(FieldSemantics::kRegReadAddr1))
        std::cout << "r2 ";
      if (used_semantics.count(FieldSemantics::kRegWriteAddr))
        std::cout << "w ";
      if (used_semantics.count(FieldSemantics::kAluCmp)) std::cout << "cmp ";
      if (used_semantics.count(FieldSemantics::kAluLhs)) std::cout << "lhs ";
      if (used_semantics.count(FieldSemantics::kAluRhs)) std::cout << "rhs ";

      std::cout << "\n";
    }

    instructions_.push_back(Instruction{
        mnemonic, mask, std::move(fields),
        std::vector<InstrSemantics>(semantics.begin(), semantics.end())});

    return absl::OkStatus();
  }

  absl::Span<const Instruction> instructions() const { return instructions_; }

  int total_num_instructions() const {
    return std::count(used_.begin(), used_.end(), true);
  }

 private:
  absl::flat_hash_map<std::string, Field> fields_;
  std::vector<Instruction> instructions_;
  std::vector<bool> used_;
};

void IsaBuilder::DefineField(std::string field, uint32_t mask, std::string type,
                             std::vector<FieldSemantics> semantics) {
  bool inserted = fields_
                      .emplace(field, Field{mask, field, std::move(type),
                                            std::move(semantics)})
                      .second;
  (void)inserted;
  assert(inserted);
}

void BuildIsa(IsaBuilder& builder) {
  // The architecture is somewhat defined by manufacturing constraints. Since I
  // always have to get 5 of each board, we can use the leftover boards for
  // useful features (e.g. the four spare instruction counters will make a basic
  // call stack).
  using F = FieldSemantics;
  // clang-format off
  builder.DefineField("pred",      0b10000000000, "bool",       {F::kPredication});
  builder.DefineField("imm",       0b00000001111, "uint32_t",   {F::kAluRhs, F::kImmediate});
  builder.DefineField("lhsimm",    0b00011000000, "Register",   {F::kRegReadAddr0, F::kAluLhs});  // lhs when imm is used
  builder.DefineField("dstimm",    0b00000110000, "Register",   {F::kRegReadAddr0, F::kAluLhs, F::kRegWriteAddr});  // dst when imm is used
  builder.DefineField("cmp",       0b00000110000, "Comparator", {F::kAluCmp});  // e, gt, ne, le
  builder.DefineField("lhs",       0b00000001100, "Register",   {F::kRegReadAddr0, F::kAluLhs});
  builder.DefineField("rhs",       0b00000000011, "Register",   {F::kRegReadAddr1, F::kAluRhs});

  builder.DefineField("src",       0b00000000011, "Register",   {F::kRegReadAddr1, F::kAluRhs});
  builder.DefineField("dst",       0b00000001100, "Register",   {F::kRegReadAddr0, F::kAluLhs, F::kRegWriteAddr});
  builder.DefineField("dstaddr",   0b00000001100, "Register",   {F::kRegReadAddr0, F::kAluLhs});
  builder.DefineField("dstnosrc",  0b00000000011, "Register",   {F::kRegReadAddr0, F::kRegWriteAddr, F::kAluRhs});
  builder.DefineField("bit",       0b00000001100, "uint32_t",   {F::kTestBitIdx});
  builder.DefineField("val",       0b00000010000, "uint32_t",   {F::kTestBitVal});
  builder.DefineField("deref_src", 0b00000010000, "bool",       {F::kDerefSrc});

  builder.DefineField("jumpaddr",  0b00000111111, "uint32_t",   {F::kJumpAddr});
  builder.DefineField("bank",      0b00000000001, "uint32_t",   {F::kMemBankIdx});
  // clang-format on

  // rsrc/[rsrc], rdst
  std::vector<std::string> reg_or_mem_to_reg{"pred", "deref_src", "src", "dst"};
  std::vector<std::string> mem_to_reg{"pred", "src", "dst"};
  std::vector<std::string> reg_to_mem{"pred", "src", "dstaddr"};
  std::vector<std::string> imm_to_reg{"pred", "imm", "dstimm"};
  std::vector<std::string> reg_to_reg{"pred", "lhs", "rhs"};
  std::vector<std::string> cmp_reg_to_reg{"pred", "lhs", "rhs", "cmp"};
  std::vector<std::string> cmp_reg_to_imm{"pred", "imm", "lhsimm", "cmp"};

  std::vector<std::string> jump{"pred", "jumpaddr"};
  std::vector<std::string> testbit{"pred", "bit", "val", "rhs"};
  std::vector<std::string> wait{"pred", "val", "rhs", "lhs"};

#define C ABSL_CHECK_OK
  using I = InstrSemantics;
  C(builder.DefineInstruction("addi", 0'0000'777777_enc,  // b = b + imm
                              imm_to_reg, {}));
  C(builder.DefineInstruction("movi", 0'0001'777777_enc, imm_to_reg,  // b = imm
                              {I::kAluZeroLhs}));
  C(builder.DefineInstruction("add", 0'00100'77777_enc,  // b = b + a/[a]
                              reg_or_mem_to_reg, {}));
  C(builder.DefineInstruction("tbit", 0'00101'77777_enc,  // flag = (b[n] == v)
                              testbit, {I::kAluZeroLhs}));
  C(builder.DefineInstruction("mov", 0'00110'77777_enc,  // b = a/[a]
                              reg_or_mem_to_reg, {I::kAluZeroLhs}));
  C(builder.DefineInstruction("store", 0'001110'7777_enc,  // [b] = a
                              reg_to_mem, {I::kAluZeroLhs, I::kStMem}));
  C(builder.DefineInstruction("ldgpi", 0'001111'7777_enc,  // b = gpi [a]
                              mem_to_reg, {I::kAluZeroLhs, I::kLdGpi}));
  C(builder.DefineInstruction("testi", 0'01'77777777_enc,  // a <cmp> imm
                              cmp_reg_to_imm,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("subri", 0'1000'777777_enc,  // b = imm - b
                              imm_to_reg, {I::kAluNotRhs, I::kAluNeg}));
  C(builder.DefineInstruction("subit",
                              0'1001'777777_enc,  // b = b - imm, test 0
                              imm_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("jump", 0'1010'777777_enc,  // jump
                              jump, {I::kJump}));
  C(builder.DefineInstruction("call", 0'1011'777777_enc,  // call
                              jump, {I::kJump, I::kPushPc}));
  C(builder.DefineInstruction("test", 0'1100'777777_enc,  // a <cmp> b
                              cmp_reg_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("subi", 0'1101'777777_enc,  // b = b - imm
                              imm_to_reg, {I::kAluNotRhs, I::kAluCarryIn}));
  C(builder.DefineInstruction("sub", 0'11100'77777_enc,  // b = b - a/[a]
                              reg_or_mem_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn}));
  C(builder.DefineInstruction("subr", 0'11101'77777_enc,  // b = a/[a] - b
                              reg_or_mem_to_reg, {I::kAluNotRhs, I::kAluNeg}));
  C(builder.DefineInstruction(
      "wait",
      0'11110'77777_enc,  // wait for gpi [a] =/!= b
      wait, {I::kAluNotRhs, I::kAluCarryIn, I::kWait, I::kLdGpi}));
  C(builder.DefineInstruction("retpop", 0'11111000'77_enc,  // pop r; ret
                              {"pred", "dstnosrc"},
                              {I::kAluZeroLhs, I::kPopPc, I::kPopReg}));
  C(builder.DefineInstruction("pop", 0'11111001'77_enc,  // pop
                              {"pred", "dstnosrc"}, {I::kPopReg}));
  // TODO: this is dubious, it saves just one instruction vs:
  //   mov 0 reg
  //   + inc reg
  // This pattern is probably rare anyway. Find something more useful for these
  // bits.
  C(builder.DefineInstruction("flagget", 0'11111010'77_enc,  // r = flag
                              {"pred", "dstnosrc"}, {I::kFlagGet}));
  C(builder.DefineInstruction("not_", 0'11111011'77_enc,  // r = ~r
                              {"pred", "dstnosrc"},
                              {I::kAluZeroLhs, I::kAluNeg}));
  C(builder.DefineInstruction("shr", 0'11111100'77_enc, {"pred", "dstnosrc"},
                              {I::kAluShr, I::kAluZeroLhs}));
  // TODO: flagset is useless, this is just testi reg == 1 (or tbit reg[0], 1)
  C(builder.DefineInstruction("flagset", 0'11111101'77_enc,  // flag = reg[0]
                              {"pred", "rhs"}, {I::kFlagSet}));
  C(builder.DefineInstruction("push", 0'11111110'77_enc,  // push
                              {"pred", "rhs"}, {I::kAluZeroLhs, I::kPushReg}));
  C(builder.DefineInstruction("membank",
                              0'111111110'7_enc,  // select memory bank
                              {"pred", "bank"}, {I::kMemBankSet}));
  C(builder.DefineInstruction("ret", 0'1111111110_enc,  // ret
                              {"pred"}, {I::kPopPc}));
  // TODO: this will decode to some sort of <= comparison. Figure out the
  // details.
  C(builder.DefineInstruction("invflag", 0'1111111111_enc,  // flag = !flag
                              {"pred"},
                              {I::kFlagGet, I::kFlagSet, I::kAluNeg}));  // TODO
#undef C

  // Ideas for more useful instructions:
  // - something to extend the ROM (e.g. 2 extra bits for long jumps)
  // - something to read the stack without popping (assuming the stack will be
  // larger than 1 element)
}

namespace {

// Not sure any of this codegen is actually needed. But whatever, it's already
// there.

constexpr std::string_view kVisitorHeaderTemplate = R"(#ifndef VISITOR_H__
#define VISITOR_H__

#include <cstdint>
#include <string_view>

#include "absl/types/span.h"
#include "isa/isa_types.h"

namespace isa {

template <typename T>
class InstructionVisitor {
 public:
  virtual ~InstructionVisitor() = default;
 
%s
};

template <typename T>
class InstructionVisitorWithSemantics : public InstructionVisitor<T> {
 public:
%s

 protected:
  virtual T Op(std::string_view mnemonic, absl::Span<const uint32_t> args,
             absl::Span<const InstrSemantics>, absl::Span<const absl::Span<const FieldSemantics>>) = 0;
};

}  // namespace

#endif
)";

}  // namespace

std::string GetVisitorSignature(const Instruction& instr, std::string_view T) {
  std::vector<std::string> params;
  for (const auto& arg : instr.fields) {
    absl::Format(&params.emplace_back(), "%s %s", arg.type, arg.name);
  }
  std::string ret;
  absl::Format(&ret, "%s %s(%s)", T, instr.mnemonic,
               absl::StrJoin(params, ", "));
  return ret;
}

std::string GetSemantics(absl::Span<const FieldSemantics> sem) {
  std::vector<std::string> semantics;
  for (auto s : sem) {
    absl::Format(&semantics.emplace_back(),
                 "static_cast<isa::FieldSemantics>(%d)", static_cast<int>(s));
  }
  std::string out;
  absl::Format(&out, "{%s}", absl::StrJoin(semantics, ", "));
  return out;
}

std::string GetSemantics(absl::Span<const InstrSemantics> sem) {
  std::vector<std::string> semantics;
  for (auto s : sem) {
    absl::Format(&semantics.emplace_back(),
                 "static_cast<isa::InstrSemantics>(%d)", static_cast<int>(s));
  }
  return absl::StrJoin(semantics, ", ");
}

void PrintVisitorHeader(IsaBuilder& builder) {
  std::vector<std::string> functions;
  std::vector<std::string> semantics_call_functions;

  for (auto& instr : builder.instructions()) {
    absl::Format(&functions.emplace_back(), "  virtual %s = 0;",
                 GetVisitorSignature(instr, "T"));

    std::vector<std::string> field_semantics;
    for (auto f : instr.fields) {
      field_semantics.push_back(GetSemantics(f.semantics));
    }

    absl::Format(&semantics_call_functions.emplace_back(), "  %s final {",
                 GetVisitorSignature(instr, "T"));

    std::vector<std::string> args;
    for (const auto& arg : instr.fields) {
      absl::Format(&args.emplace_back(), "static_cast<uint32_t>(%s)", arg.name);
    }
    absl::Format(&semantics_call_functions.emplace_back(),
                 "    return Op(\"%s\", {%s}, {%s}, {%s});", instr.mnemonic,
                 absl::StrJoin(args, ", "), GetSemantics(instr.semantics),
                 absl::StrJoin(field_semantics, ", "));

    semantics_call_functions.emplace_back("  }");
  }

  std::string out;
  absl::Format(&out, kVisitorHeaderTemplate, absl::StrJoin(functions, "\n"),
               absl::StrJoin(semantics_call_functions, "\n"));
  std::cout << out;
}

namespace {
constexpr std::string_view kDispatchTemplate = R"(
#include "isa/asm_dispatch.h"

#include <optional>
#include <string_view>

#include "absl/types/span.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "isa/isa_types.h"
#include "visitor.h"

namespace isa {

bool DispatchInstruction(InstructionVisitor<void>& visitor,
                         std::string_view mnemonic, bool pred,
                         std::optional<bool> deref_src,
                         absl::Span<const Register> registers,
                         absl::Span<const uint32_t> imms,
                         std::optional<Comparator> cmp,
                         std::optional<uint32_t> jumpaddr) {
  bool err = false;
  auto use_span = [&](auto& span) -> std::remove_cvref_t<decltype(span[0])> {
    if (!span.empty()) {
      auto ret = span[0];
      span = span.subspan(1);
      return ret;
    }
    err = true;
    return {};
  };

  auto use = [&](auto& opt) -> std::remove_cvref_t<decltype(*opt)> {
    if (opt) {
      auto ret = *opt;
      opt = std::nullopt;
      return ret;
    }
    err = true;
    return {};
  };

  if (false) {}
%s
  else { return false; }

  if (err || deref_src || cmp || jumpaddr || !registers.empty() || 
      !imms.empty()) {
    std::cerr << "err: " << (err ? "1" : "0") << ", " << (deref_src ? "1" : "0") << (cmp ? "1" : "0") << (jumpaddr ? "1" : "0") << registers.size() << ", " << imms.size() << "\n";
    return false;
  }
  return true;
}

}  // namespace isa
)";
}  // namespace

void PrintAsmDispatch(IsaBuilder& builder) {
  std::vector<std::string> lines;

  for (auto& instr : builder.instructions()) {
    absl::Format(&lines.emplace_back(), R"(  else if (mnemonic == "%s") {)",
                 instr.mnemonic);
    static const std::array<std::string_view, 3> kOptionalFields{
        "cmp", "deref_src", "jumpaddr"};
    std::vector<std::string> args;
    for (auto& field : instr.fields) {
      if (field.name == "pred") {
        args.push_back("pred");
      } else if (absl::c_contains(kOptionalFields, field.name)) {
        args.push_back(absl::StrCat("use(", field.name, ")"));
      } else if (field.type == "Register") {
        lines.push_back(
            absl::StrCat("    auto r_", field.name, " = use_span(registers);"));
        args.push_back(absl::StrCat("r_", field.name));
      } else if (field.type == "uint32_t") {
        lines.push_back(
            absl::StrCat("    auto i_", field.name, " = use_span(imms);"));
        args.push_back(absl::StrCat("i_", field.name));
      } else {
        std::cerr << "Unexpected field type: " << field.type << "\n";
        return;
      }
    }
    absl::Format(&lines.emplace_back(), "    visitor.%s(%s);", instr.mnemonic,
                 absl::StrJoin(args, ", "));
    lines.push_back("  }");
  }

  std::string out;
  absl::Format(&out, kDispatchTemplate, absl::StrJoin(lines, "\n"));
  std::cout << out;
}

namespace {

constexpr std::string_view kEncodeDecodeTemplate = R"(
#include "isa/encdec.h"

#include "absl/status/status.h"
#include "isa/visitor.h"

namespace isa {
namespace {

template<typename T>
T DecodeInstructionImpl(uint16_t instr, InstructionVisitor<T>& visitor) {
  if (instr >= 2048) {
    std::cerr << "Invalid instruction.";
    std::abort();
  }
%s
  else {
    std::cerr << "Invalid instruction.";
    std::abort();
  }
}

}  // namespace

void DecodeInstruction(uint16_t instr, InstructionVisitor<void>& visitor) {
  DecodeInstructionImpl(instr, visitor);
}

absl::Status DecodeInstruction(uint16_t instr,
                               InstructionVisitor<absl::Status>& visitor) {
  return DecodeInstructionImpl(instr, visitor);
}


namespace detail {

class EncodeVisitor : public InstructionVisitor<void> {
 public:
  EncodeVisitor(Encoder& enc) : enc_(enc) {}
%s

 private:
  Encoder& enc_;
};

}  // namespace detail

Encoder::Encoder() : visitor_(std::make_unique<detail::EncodeVisitor>(*this)) {}

}  // namespace isa
)";

}  // namespace

void PrintEncoderDecoder(IsaBuilder& builder) {
  std::vector<std::string> dec_lines;
  for (auto& instr : builder.instructions()) {
    absl::Format(&dec_lines.emplace_back(), R"(  else if ((instr & %d) == %d))",
                 instr.mask.opcode_mask, instr.mask.opcode);
    std::vector<std::string> args;
    for (const auto& field : instr.fields) {
      absl::Format(&args.emplace_back(), "static_cast<%s>((instr & %d) >> %d)",
                   field.type, field.mask, __builtin_ctz(field.mask));
    }
    absl::Format(&dec_lines.emplace_back(), R"(    return visitor.%s(%s);)",
                 instr.mnemonic, absl::StrJoin(args, ", "));
  }

  std::vector<std::string> enc_lines;
  for (auto& instr : builder.instructions()) {
    std::vector<std::string> fields;
    absl::Format(&enc_lines.emplace_back(), "  %s override {",
                 GetVisitorSignature(instr, "void"));
    for (auto& field : instr.fields) {
      absl::Format(&enc_lines.emplace_back(),
                   "    auto i_%s = static_cast<uint32_t>(%s);", field.name,
                   field.name);
      absl::Format(&enc_lines.emplace_back(), "    assert(i_%s <= %d);",
                   field.name, field.mask >> __builtin_ctz(field.mask));
      absl::Format(&fields.emplace_back(), "(i_%s << %d)", field.name,
                   __builtin_ctz(field.mask));
    }

    absl::Format(&enc_lines.emplace_back(),
                 "    enc_.instrs_.push_back(%d | %s);\n  }", instr.mask.opcode,
                 absl::StrJoin(fields, " | "));
  }

  std::string out;
  absl::Format(&out, kEncodeDecodeTemplate, absl::StrJoin(dec_lines, "\n"),
               absl::StrJoin(enc_lines, "\n"));
  std::cout << out;
}

namespace {

static constexpr std::string_view kInstructionsTemplate = R"(
#include "isa/instructions.h"

#include "absl/types/span.h"

namespace isa {

absl::Span<const Instruction> GetInstructions() {
  static auto& instructions = *new std::vector<Instruction>{
%s
  };
  return instructions;
}

}  // namespace isa
)";

}  // namespace

void PrintInstructions(IsaBuilder& builder) {
  std::vector<std::string> instrs;
  // Why not just call the builder? Yes.
  for (auto& instr : builder.instructions()) {
    std::vector<std::string> fields;
    for (auto& field : instr.fields) {
      absl::Format(&fields.emplace_back(), R"({%d, "%s", "%s", %s})",
                   field.mask, field.name, field.type,
                   GetSemantics(field.semantics));
    }

    absl::Format(&instrs.emplace_back(),
                 R"(    {"%s", {%d, %d, %d}, {%s}, {%s}},)", instr.mnemonic,
                 instr.mask.opcode_mask, instr.mask.opcode,
                 instr.mask.operand_bits, absl::StrJoin(fields, ", "),
                 GetSemantics(instr.semantics));
  }

  std::string out;
  absl::Format(&out, kInstructionsTemplate, absl::StrJoin(instrs, "\n"));
  std::cout << out;
};

}  // namespace isa

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  isa::IsaBuilder builder;
  isa::BuildIsa(builder);

  if (absl::GetFlag(FLAGS_mode) == "visitor_hdr") {
    isa::PrintVisitorHeader(builder);
  } else if (absl::GetFlag(FLAGS_mode) == "asm_dispatch") {
    isa::PrintAsmDispatch(builder);
  } else if (absl::GetFlag(FLAGS_mode) == "encdec") {
    isa::PrintEncoderDecoder(builder);
  } else if (absl::GetFlag(FLAGS_mode) == "instrs") {
    isa::PrintInstructions(builder);
  } else {
    std::cout << "Total number of instructions: "
              << builder.total_num_instructions() << "\n";
  }
}
