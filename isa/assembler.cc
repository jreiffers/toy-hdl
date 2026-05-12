#include "absl/strings/numbers.h"
#include "isa/asm_dispatch.h"
#include "isa/instructions.h"
#include "isa/isa_types.h"
#include "jank/lexer.h"

namespace isa {

using ::jank::Token;
using ::jank::Tokenizer;
using ::jank::TokenType;

static auto& kComparators =
    *new absl::flat_hash_map<TokenType, std::pair<Comparator, bool>>{
        {TokenType::kEq, {Comparator::kEq, false}},
        {TokenType::kGe, {Comparator::kGe, false}},
        {TokenType::kNe, {Comparator::kNe, false}},
        {TokenType::kLt, {Comparator::kLt, false}},

        {TokenType::kLe, {Comparator::kGe, true}},
        {TokenType::kGt, {Comparator::kLt, true}},
    };

static auto& kInstrsByMnemonic = *[]() {
  auto* instrs =
      new absl::flat_hash_map<std::string_view, const Instruction*>();
  for (auto& instr : GetInstructions()) {
    (*instrs)[instr.mnemonic] = &instr;
  }
  return instrs;
}();

template <typename F, typename G, typename H>
bool Parse(Tokenizer& tokenizer, F&& label_def_callback, G&& label_lookup,
           H&& instr_callback) {
  // This is a very silly way to parse this. Initially it didn't really know
  // what instructions exist. Now it does and I didn't feel like rewriting it to
  // be saner.
  while (tokenizer) {
    auto& context = tokenizer.context();

    bool pred = false;
    if (tokenizer.peek().type == TokenType::kPlus) {
      pred = true;
      tokenizer.get();
    }

    auto unexpected = [&](Token t) {
      context.AddError(absl::StrCat("Unexpected ", to_string(t.type), "."),
                       t.loc);
      return false;
    };

    Token mnemonic = tokenizer.get();
    if (mnemonic.type != TokenType::kIdentifier) {
      return unexpected(mnemonic);
    }

    if (tokenizer.peek().type == TokenType::kColon) {
      if (!label_def_callback(mnemonic.text)) return false;
      tokenizer.get();
      continue;
    }

    // if (ParsePseudoInstruction(tokenizer, mnemonic.text, listener)) {
    //   return true;
    // }

    if (!kInstrsByMnemonic.count(mnemonic.text)) {
      return unexpected(mnemonic);
    }
    const auto& instr = *kInstrsByMnemonic.at(mnemonic.text);

    std::optional<bool> deref_src = std::nullopt;
    std::vector<Register> registers;
    std::vector<uint32_t> imms;
    std::optional<Comparator> cmp;
    std::optional<uint32_t> jump_addr = std::nullopt;

    auto to_register = [&](Token t) -> std::optional<Register> {
      if (t.text == "r0") {
        return Register::kR0;
      }
      if (t.text == "r1") {
        return Register::kR1;
      }
      if (t.text == "r2") {
        return Register::kR2;
      }
      if (t.text == "r3") {
        return Register::kR3;
      }
      return std::nullopt;
    };

    auto parse_register = [&]() {
      if (!tokenizer) {
        context.AddError("Unexpected end of file.", tokenizer.last_token().loc);
        return false;
      }
      auto t = tokenizer.get();
      auto reg = to_register(t);
      if (!reg) return unexpected(t);
      registers.push_back(*reg);
      return true;
    };

    if (instr.HasField("deref_src")) {
      if (tokenizer.peek().type == TokenType::kBracketLeft) {
        tokenizer.get();
        deref_src = true;
        if (!parse_register()) return false;
        if (tokenizer.get().type != TokenType::kBracketRight)
          return unexpected(tokenizer.last_token());
      } else {
        deref_src = false;
      }
    }

    do {
      auto t = tokenizer.peek();
      auto reg = to_register(t);
      if (reg) {
        registers.push_back(*reg);
        tokenizer.get();
      } else if (auto cmp_it = kComparators.find(t.type);
                 cmp_it != kComparators.end()) {
        if (cmp || (registers.size() + imms.size() != 1)) return unexpected(t);
        if (cmp_it->second.second) {
          context.AddError("Unsupported comparator. Swap operands if possible.",
                           t.loc);
          return false;
        }
        cmp = cmp_it->second.first;
        tokenizer.get();
      } else if (t.type == TokenType::kIntLiteral) {
        uint8_t val;
        if (!absl::SimpleAtoi(t.text, &val)) {
          return unexpected(t);
        }
        tokenizer.get();
        imms.push_back(val);
      } else if (t.type == TokenType::kIdentifier && !jump_addr &&
                 instr.HasField("jumpaddr")) {
        //       if (kInstrsByMnemonic.count(t.text)) break;
        //       Tokenizer copy = tokenizer;
        //       copy.get();
        //      if (copy.peek().type == TokenType::kColon) break;
        uint32_t target;
        if (!label_lookup(t.text, &target)) {
          context.AddError("Unknown label.", t.loc);
          return false;
        }
        tokenizer.get();
        jump_addr = target;
      } else {
        break;
      }
    } while (true);

    if (!instr_callback(mnemonic.text, pred, deref_src, registers, imms, cmp,
                        jump_addr)) {
      context.AddError("Failed to parse instruction", mnemonic.loc);
      return false;
    }
  }

  return true;
}

bool ParseAssembly(jank::Context& context, InstructionVisitor<void>& visitor) {
  Tokenizer t1(context);

  uint32_t pc = 0;
  absl::flat_hash_map<std::string_view, uint32_t> labels;

  if (!Parse(
          t1,
          [&](std::string_view label) {
            if (!labels.try_emplace(label, pc).second) {
              t1.context().AddError("Label redefined.", t1.last_token().loc);
              return false;
            }
            return true;
          },
          [](std::string_view label, uint32_t* out) {
            *out = 0;
            return true;
          },
          [&](std::string_view mnemonic, bool pred,
              std::optional<bool> deref_src,
              absl::Span<const Register> registers,
              absl::Span<const uint32_t> imms, std::optional<Comparator> cmp,
              std::optional<uint32_t> jumpaddr) {
            if (++pc == 64) {
              t1.context().AddError("Too many instructions.",
                                    t1.last_token().loc);
              return false;
            }
            return true;
          })) {
    return false;
  }

  Tokenizer t2(context);
  return Parse(
      t2, [&](std::string_view label) { return true; },
      [&](std::string_view label, uint32_t* out) {
        if (!labels.count(label)) return false;
        *out = labels[label];
        return true;
      },
      [&](std::string_view mnemonic, bool pred, std::optional<bool> deref_src,
          absl::Span<const Register> registers, absl::Span<const uint32_t> imms,
          std::optional<Comparator> cmp, std::optional<uint32_t> jumpaddr) {
        return DispatchInstruction(visitor, mnemonic, pred, deref_src,
                                   registers, imms, cmp, jumpaddr);
      });
}

}  // namespace isa
