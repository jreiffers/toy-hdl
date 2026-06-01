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
  uint32_t opcode_mask = _pext_u32(~v, 02'22222'22222);
  uint32_t opcode = _pext_u32(v, 01'11111'11111) & opcode_mask;
  uint32_t operand_mask = _pext_u32(v, 02'22222'22222);
  return InstructionMask{opcode_mask, opcode, operand_mask};
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
    int num = 1 << __builtin_popcount(mask.operand_mask);
    for (uint32_t i = 0; i < num; ++i) {
      uint32_t opcode = mask.opcode + _pdep_u32(i, mask.operand_mask);
      if (used_[opcode]) {
        return absl::InvalidArgumentError("Duplicate bitcode.");
      }

      used_[opcode] = true;
    }

    absl::flat_hash_set<FieldSemantics> used_semantics;
    std::vector<Field> fields;
    uint32_t parammask = mask.operand_mask;
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
                << std::bitset<11>(mask.opcode) << ", "
                << std::bitset<11>(mask.operand_mask) << "\n";
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
  builder.DefineField("val",       0b00000010000, "uint32_t",   {F::kTestBitVal});
  builder.DefineField("deref_src", 0b00000010000, "bool",       {F::kDerefSrc});

  builder.DefineField("jumpaddr",  0b00000111111, "uint32_t",   {F::kJumpAddr});
  // clang-format on

  // rsrc/[rsrc], rdst
  std::vector<std::string> reg_or_mem_to_reg{"pred", "deref_src", "src", "dst"};
  std::vector<std::string> mem_to_reg{"pred", "src", "dst"};
  std::vector<std::string> reg_to_mem{"pred", "src", "dstaddr"};
  std::vector<std::string> imm_to_reg{"pred", "imm", "dstimm"};
  std::vector<std::string> reg_to_reg{"pred", "src", "dst"};
  std::vector<std::string> reg_reg{"pred", "lhs", "rhs"};
  std::vector<std::string> cmp_reg_to_reg{"pred", "lhs", "rhs", "cmp"};
  std::vector<std::string> cmp_reg_to_imm{"pred", "imm", "lhsimm", "cmp"};

  std::vector<std::string> jump{"pred", "jumpaddr"};
  std::vector<std::string> testbit{"pred", "bit", "val", "rhs"};
  std::vector<std::string> wait{"pred", "val", "rhs", "lhs"};

#define C ABSL_CHECK_OK
  using I = InstrSemantics;
  C(builder.DefineInstruction("addi", 0'70000'777777_enc,  // b = b + imm
                              imm_to_reg, {}));
  C(builder.DefineInstruction("movi", 0'70001'777777_enc,
                              imm_to_reg,  // b = imm
                              {I::kAluZeroLhs}));
  C(builder.DefineInstruction("add", 0'700100'77777_enc,  // b = b + a/[a]
                              reg_or_mem_to_reg, {}));
  C(builder.DefineInstruction("and", 0'700101'07777_enc, reg_to_reg,
                              {I::kAluAnd}));
  C(builder.DefineInstruction("andtnz", 0'700101'17777_enc, reg_reg,
                              {I::kAluAnd, I::kFlagSet}));
  C(builder.DefineInstruction("mov", 0'700110'77777_enc,  // b = a/[a]
                              reg_or_mem_to_reg, {I::kAluZeroLhs}));
  C(builder.DefineInstruction("store", 0'7001110'7777_enc,  // [b] = a
                              reg_to_mem, {I::kAluZeroLhs, I::kStMem}));
  C(builder.DefineInstruction("ldgpi", 0'7001111'7777_enc,  // b = gpi [a]
                              mem_to_reg, {I::kAluZeroLhs, I::kLdGpi}));
  C(builder.DefineInstruction("testi", 0'701'77777777_enc,  // a <cmp> imm
                              cmp_reg_to_imm,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("addtnz", 0'71000'077777_enc,  // b = a + b, test != 0
                              reg_or_mem_to_reg, {I::kFlagSet}));
  C(builder.DefineInstruction("subtnz", 0'71000'177777_enc,  // b = b - a, test != 0
                              reg_or_mem_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("subitnz",
                              0'71001'777777_enc,  // b = b - imm, test != 0
                              imm_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("jump", 0'71010'777777_enc,  // jump rombank:imm6
                              jump, {I::kJump}));
  C(builder.DefineInstruction("call", 0'71011'777777_enc,  // call rombank:imm6
                              jump, {I::kJump, I::kPushPc}));
  C(builder.DefineInstruction("test", 0'71100'777777_enc,  // a <cmp> b
                              cmp_reg_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn, I::kFlagSet}));
  C(builder.DefineInstruction("subi", 0'71101'777777_enc,  // b = b - imm
                              imm_to_reg, {I::kAluNotRhs, I::kAluCarryIn}));
  C(builder.DefineInstruction("sub", 0'711100'77777_enc,  // b = b - a/[a]
                              reg_or_mem_to_reg,
                              {I::kAluNotRhs, I::kAluCarryIn}));
  C(builder.DefineInstruction("subr", 0'711101'77777_enc,  // b = a/[a] - b
                              reg_or_mem_to_reg, {I::kAluNotRhs, I::kAluNot}));
  C(builder.DefineInstruction(
      "wait",
      0'711110'77777_enc,  // wait for gpi [a] =/!= b
      wait, {I::kAluNotRhs, I::kAluCarryIn, I::kWait, I::kLdGpi}));
  C(builder.DefineInstruction("retpop", 0'7111110'7700_enc,  // pop r; ret
                              {"pred", "dst"},
                              {I::kAluZeroLhs, I::kPopPc, I::kPopReg}));
  C(builder.DefineInstruction("pop", 0'7111110'7701_enc,  // pop
                              {"pred", "dst"}, {I::kPopReg}));
  C(builder.DefineInstruction("membank",
                              0'7111110'7710_enc,  // select memory bank r
                              {"pred", "lhs"}, {I::kMemBankSet}));
  C(builder.DefineInstruction("not", 0'7111110'7711_enc,  // r = ~r
                              {"pred", "dst"}, {I::kAluZeroRhs, I::kAluNot}));
  C(builder.DefineInstruction("shr", 0'7111111'7700_enc, {"pred", "dst"},
                              {I::kAluShr, I::kAluZeroRhs}));
  C(builder.DefineInstruction("rombank",
                              0'7111111'7701_enc,  // select rom bank r
                                                   // only affects jumps
                              {"pred", "lhs"}, {I::kRomBankSet}));
  C(builder.DefineInstruction("push", 0'7111111'7710_enc,  // push
                              {"pred", "lhs"}, {I::kAluZeroRhs, I::kPushReg}));
  C(builder.DefineInstruction("jump3", 0'71111110011_enc,  // jump rombank:r3:00
                              {"pred"}, {I::kJump, I::kIndirect}));
  C(builder.DefineInstruction("call3", 0'71111110111_enc,  // call rombank:r3:00
                              {"pred"}, {I::kJump, I::kPushPc, I::kIndirect}));
  C(builder.DefineInstruction("ret", 0'71111111011_enc,  // ret
                              {"pred"}, {I::kPopPc}));
  C(builder.DefineInstruction(
      "invflag", 0'71111111111_enc,  // flag = !flag
      {"pred"},
      {I::kAluZeroLhs, I::kFlagGet, I::kAluCarryIn, I::kAluNot, I::kFlagSet}));
#undef C
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

std::string_view FunctionName(std::string_view mnemonic) {
  if (mnemonic == "not") return "not_";
  if (mnemonic == "and") return "and_";
  return mnemonic;
}

}  // namespace

std::string GetVisitorSignature(const Instruction& instr, std::string_view T) {
  std::vector<std::string> params;
  for (const auto& arg : instr.fields) {
    absl::Format(&params.emplace_back(), "%s %s", arg.type, arg.name);
  }
  std::string ret;
  absl::Format(&ret, "%s %s(%s)", T, FunctionName(instr.mnemonic),
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
    absl::Format(&lines.emplace_back(), "    visitor.%s(%s);",
                 FunctionName(instr.mnemonic), absl::StrJoin(args, ", "));
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
                 FunctionName(instr.mnemonic), absl::StrJoin(args, ", "));
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
                 instr.mask.operand_mask, absl::StrJoin(fields, ", "),
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
