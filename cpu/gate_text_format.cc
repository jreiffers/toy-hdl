#include "cpu/gate_text_format.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "cpu/graph.h"
#include "jank/lexer.h"

namespace {

using jank::Token;
using T = jank::TokenType;

// (type, jank type, required text or empty, gate kind or kDead).
// clang-format off
#define VISIT_TOKENS(visit)                         \
  visit(kUnknown,    kUnknownToken, "",         kDead)       \
  visit(kLo,         kIntLiteral,   "0",        kDead)       \
  visit(kHi,         kIntLiteral,   "1",        kDead)       \
  visit(kIntLiteral, kIntLiteral,   "",         kDead)       \
  visit(kNeg,        kNeg,          "",         kDead)       \
  visit(kLet,        kIdentifier,   "let",      kDead)       \
  visit(kIn,         kIdentifier,   "in",       kDead)       \
  visit(kNand,       kIdentifier,   "nand",     kNand)       \
  visit(kNor,        kIdentifier,   "nor",      kNor)        \
  visit(kNot,        kIdentifier,   "not",      kNot)        \
  visit(kMux,        kIdentifier,   "mux",      kMux)        \
  visit(kTriState,   kIdentifier,   "tristate", kTriStateBuffer) \
  visit(kLookup,     kIdentifier,   "lut",      kLookup)     \
  visit(kIdentifier, kIdentifier,   "",         kDead)
// clang-format on

enum class TokenType {
#define VISITOR(type, ...) type,
  VISIT_TOKENS(VISITOR)
#undef VISITOR
};

TokenType Convert(const Token& token) {
#define VISITOR(ty, jank_ty, txt, _)                              \
  if (token.type == T::jank_ty && (!txt[0] || token.text == txt)) \
    return TokenType::ty;
  VISIT_TOKENS(VISITOR)
#undef VISITOR
  return TokenType::kUnknown;
}

class NetworkParser {
 public:
  NetworkParser(jank::Context& ctx, GateNetwork& net)
      : ctx_(ctx), tokenizer_(ctx), net_(net) {}

  bool Parse() {
#define EXPECT(x) \
  if (!(x)) return false
#define EXPECT_E(x, e)           \
  if (!(x)) {                    \
    Error(e, tokenizer_.peek()); \
    return false;                \
  }
    if (!ctx_.errors().empty()) return false;
    do {
      Token name = tokenizer_.peek();
      EXPECT_E(Convert(name) == TokenType::kIdentifier, "Expected identifier.");
      auto in = net_.AddInput<1>(std::string(name.text))[0];
      EXPECT_E(names_.emplace(name.text, in).second, "Duplicate name");
      tokenizer_.get();
    } while (Consume(T::kComma));

    EXPECT(Expect(T::kMinus) && Expect(T::kGt));

    while (Convert(tokenizer_.peek()) == TokenType::kLet) {
      tokenizer_.get();
      Token name = tokenizer_.peek();
      EXPECT_E(Convert(name) == TokenType::kIdentifier, "Expected identifier.");
      tokenizer_.get();
      GateTerminal value;
      EXPECT(Expect(T::kAssign) && Expression(value));
      EXPECT_E(Convert(tokenizer_.peek()) == TokenType::kIn, "Expected 'in'.");
      EXPECT_E(names_.emplace(name.text, value).second, "Duplicate name");
      tokenizer_.get();
    }

    absl::InlinedVector<GateTerminal, 2> outputs;
    if (Consume(T::kParenLeft)) {
      EXPECT(List(outputs));
    } else {
      outputs.emplace_back();
      EXPECT(Expression(outputs.back()));
    }
    EXPECT(ctx_.errors().empty());
    EXPECT_E(!tokenizer_, "Expected end of input.");
    for (auto output : outputs) net_.DeclareOutput(output);
    return true;
  }

#undef EXPECT_E
#define EXPECT_E(x, e) \
  if (!(x)) return Error(e, token)

 private:
  bool Consume(T type) {
    if (!tokenizer_ || tokenizer_.peek().type != type) return false;
    tokenizer_.get();
    return true;
  }

  bool Expect(T type) {
    return Consume(type) ||
           Error(absl::StrCat("Expected '", jank::to_string(type), "'."),
                 tokenizer_.peek());
  }

  bool Error(std::string message, const Token& token) {
    ctx_.AddError(std::move(message), token.loc);
    return false;
  }

  bool List(absl::InlinedVector<GateTerminal, 2>& values) {
    if (Consume(T::kParenRight)) return true;
    do {
      EXPECT(Expression(values.emplace_back()));
    } while (Consume(T::kComma));
    return Expect(T::kParenRight);
  }

  bool Expression(GateTerminal& out) {
    Token token = tokenizer_.get();
    auto type = Convert(token);
    switch (type) {
      case TokenType::kNeg:
        if (!Expression(out)) return false;
        out = net_.Not(out);
        return true;
      case TokenType::kLo:
        out = kLowGate;
        return true;
      case TokenType::kHi:
        out = kHighGate;
        return true;
      case TokenType::kIntLiteral:
        return Error("Expected constant 0 or 1.", token);
      case TokenType::kIdentifier: {
        EXPECT_E(tokenizer_.peek().type != T::kParenLeft, "Invalid gate");
        auto it = names_.find(token.text);
        EXPECT_E(it != names_.end(), "Unknown identifier");
        out = it->second;
        return true;
      }
      case TokenType::kUnknown:
      case TokenType::kLet:
      case TokenType::kIn:
        return Error("Expected identifier.", token);
      default: {
        GateKind kind = GateKind::kDead;
        uint64_t mask = 0;
        switch (type) {
#define VISITOR(ty, _, __, gate) \
  case TokenType::ty:            \
    kind = GateKind::gate;       \
    break;
          VISIT_TOKENS(VISITOR)
#undef VISITOR
        }
        EXPECT_E(kind != GateKind::kDead, "Invalid gate");
        if (kind == GateKind::kLookup) {
          EXPECT_E(Expect(T::kBracketLeft) &&
                       absl::SimpleAtoi(tokenizer_.get().text, &mask) &&
                       Expect(T::kBracketRight),
                   "Invalid LUT");
        }

        absl::InlinedVector<GateTerminal, 2> args;
        EXPECT(Expect(T::kParenLeft) && List(args));
        auto& gate = net_.AddGate(kind, std::move(args));
        if (kind == GateKind::kLookup) gate.set_lookup_data(mask);
        out = gate.output();
        return true;
      }
    }
  }

  jank::Context& ctx_;
  jank::Tokenizer tokenizer_;
  std::map<std::string_view, GateTerminal> names_;
  GateNetwork& net_;
};

}  // namespace

void ParseNetwork(GateNetwork& net, std::string_view source) {
  jank::Context ctx{std::string(source)};
  CHECK(NetworkParser(ctx, net).Parse()) << ctx.error_string();
}

std::string PrintNetwork(GateNetwork& net) {
  int next_name = 0;
  auto name = [&]() {
    while (true) {
      int index = next_name++;
      std::string result;
      do {
        result.insert(result.begin(), 'a' + (index % 26));
        index = index / 26 - 1;
      } while (index >= 0);
      return result;
    }
  };

  auto sources = net.all_inputs();
  sources.push_back(kHighGate);
  sources.push_back(kLowGate);
  graph::TopoSort sort(net, sources, {net.sink()});
  std::vector<GateTerminal> outputs;
  for (auto output : net.GetOutputs()) {
    for (int i = 0; i < output.bitwidth(); ++i) {
      outputs.push_back(output[i]);
    }
  }

  std::map<GateTerminal, std::string> exprs{{kLowGate, "0"}, {kHighGate, "1"}};
  std::vector<std::string> inputs;
  for (int i = 0; i < net.total_input_bits(); ++i) {
    inputs.push_back(name());
    exprs[net.input_bit(i)] = inputs.back();
  }
  std::vector<std::string> lines{
      absl::StrCat(absl::StrJoin(inputs, ", "), " ->")};
  for (auto t : sort.order(false, false)) {
    auto& gate = *t.first;
    if (gate.kind() == GateKind::kNot) {
      exprs[t] = absl::StrCat("~", exprs.at(gate.input(0)));
    } else {
      std::string args = absl::StrJoin(
          gate.inputs(), ", ", [&](std::string* out, GateTerminal input) {
            absl::StrAppend(out, exprs.at(input));
          });
      std::string op = gate.kind() == GateKind::kLookup
                           ? absl::StrCat("lut[", gate.lookup_data(), "]")
                           : to_string(gate);
      exprs[t] = absl::StrCat(op, "(", args, ")");
    }
    if (net.GetUsers(t).size() != 1) {
      std::string n = name();
      lines.push_back(absl::StrCat("let ", n, " = ", exprs[t], " in"));
      exprs[t] = n;
    }
  }
  if (outputs.size() == 1) {
    lines.push_back(exprs.at(outputs[0]));
  } else {
    std::string outs = absl::StrJoin(
        outputs, ", ", [&](std::string* out, GateTerminal output) {
          absl::StrAppend(out, exprs.at(output));
        });
    lines.push_back(absl::StrCat("(", outs, ")"));
  }
  return absl::StrJoin(lines, "\n  ");
}
