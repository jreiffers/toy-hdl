#include "jank/parser.h"

#include "jank/ast.h"
#include "jank/context.h"
#include "jank/lexer.h"

namespace jank {
namespace {

struct NameAndType {
  Type ty;
  std::string_view name;
};

class Parser {
 public:
  explicit Parser(Context& ctx) : ctx_(ctx), tokenizer_(ctx), module_(ctx) {}

  Module Parse();
  void ParseTopLevelDecl();
  std::optional<Type> TryParseType();
  bool ParseType(Type& ty) {
    auto maybe_ty = TryParseType();
    if (!maybe_ty) {
      Error("Expected type.");
      return false;
    }
    ty = *maybe_ty;
    return true;
  }

  std::optional<NameAndType> TryParseNameAndType();

  void Error(std::string msg) {
    ctx_.AddError(std::move(msg), tokenizer_.last_token().loc);
  }

  bool ParseBlock(Block& b);

  bool Consume(TokenType ty) {
    if (tokenizer_.peek().type != ty) return false;
    tokenizer_.get();
    return true;
  }

  /*std::optional<TokenType> Consume(const absl::flat_hash_set<TokenType>& tys)
  { if (!tys.count(tokenizer_.peek().type)) return false; tokenizer_.get();
    return true;
  }*/

  bool Expect(TokenType ty) {
    if (!Consume(ty)) {
      Error(absl::StrCat("Expected '", to_string(ty), "'."));
      return false;
    }
    return true;
  }

  template <typename C, typename F>
  std::optional<bool> ParseList(C& c, F&& f) {
    if (!Consume(TokenType::kParenLeft)) return std::nullopt;
    while (tokenizer_ && tokenizer_.peek().type != TokenType::kParenRight) {
      if (!f(c.emplace_back())) {
        return false;
      }

      if (tokenizer_ && tokenizer_.peek().type != TokenType::kParenRight) {
        if (!Expect(TokenType::kComma)) return false;
      }
    }
    return Expect(TokenType::kParenRight);
  }

  std::optional<Token> Get(TokenType ty) {
    if (tokenizer_.peek().type != ty) {
      Error(absl::StrCat("Expected '", to_string(ty), "'."));
      return std::nullopt;
    }
    return tokenizer_.get();
  }

  bool Get(TokenType ty, Token& out) {
    if (tokenizer_.peek().type != ty) {
      return false;
    }
    out = tokenizer_.get();
    return true;
  }

  bool ParseFunctionCall(Block& b, Token name);
  bool ParseExpression(ExpressionNode*& out);

 private:
  Context& ctx_;
  Tokenizer tokenizer_;
  Module module_;
};

Module Parser::Parse() {
  while (ctx_.errors().empty() && tokenizer_) {
    ParseTopLevelDecl();
  }
  return std::move(module_);
}

std::optional<Type> Parser::TryParseType() {
  if (Consume(TokenType::kVoid)) {
    return {{PrimitiveType::kVoid}};
  }
  if (Consume(TokenType::kInt)) {
    return {{PrimitiveType::kInt}};
  }
  return std::nullopt;
}

bool Parser::ParseExpression(ExpressionNode*& out) {}

bool Parser::ParseFunctionCall(Block& b, Token name) {
  auto* sym = b.scope.Lookup(name.text);
  if (!sym) {
    ctx_.AddError(absl::StrCat("Undeclared identifier '", name.text, "'."),
                  name.loc);
    return false;
  }

  auto* fun = dynamic_cast<FunctionNode*>(sym);
  if (!fun) {
    Error("Only functions can be called.");
    ctx_.AddError(absl::StrCat("Note: declared here:"), sym->loc());
    return false;
  }

  // std::vector<ExpressionNode*> args;
  // auto match_and_success = ParseList(args, [this](ExpressionNode*& out) {
  //   return ParseExpression(out);
  // });

  // assert(match_and_success.has_value());
  // return *match_and_success;
  return false;
}

bool Parser::ParseBlock(Block& b) {
  if (!Expect(TokenType::kBraceLeft)) return false;

  while (tokenizer_ && tokenizer_.peek().type != TokenType::kBraceRight) {
    auto type = TryParseType();
    if (type) {
      auto ident = Get(TokenType::kIdentifier);
      auto* var = module_.alloc_var(ident->loc, ident->text, *type);
      if (!var) return false;
      if (!b.scope.AddVar(*var)) return false;
      if (!Expect(TokenType::kSemicolon)) return false;
      continue;
    }

    if (auto ident = Get(TokenType::kIdentifier)) {
      if (tokenizer_.peek().type == TokenType::kParenLeft) {
        if (!ParseFunctionCall(b, *ident)) return false;
      }
    }
  }

  return Expect(TokenType::kBraceRight);
}

void Parser::ParseTopLevelDecl() {
  auto type = TryParseType();
  // TODO GPI
  if (!type) {
    Error("Expected type.");
    return;
  }

  auto ident = Get(TokenType::kIdentifier);
  if (!ident) {
    Error("Expected identifier.");
    return;
  }

  struct Param {
    Type ty;
    Token name;
  };
  std::vector<Param> params;

  if (auto match_and_success = ParseList(
          params,
          [this](Param& a) {
            return ParseType(a.ty) && Get(TokenType::kIdentifier, a.name);
          });
      match_and_success.has_value()) {
    if (!*match_and_success) return;

    FunctionType fun_ty;
    fun_ty.return_type = *type;
    for (auto& param : params) {
      fun_ty.param_types.push_back(param.ty);
    }

    auto fun = module_.declare_function(ident->text, fun_ty, ident->loc);
    if (!fun) return;

    if (tokenizer_.peek().type == TokenType::kBraceLeft) {
      if (fun->def()) {
        ctx_.AddError("Duplicate function definition.", tokenizer_.peek().loc);
        ctx_.AddError("Note: first defined here:", fun->def()->loc());
        return;
      }

      fun->def().emplace(tokenizer_.peek().loc, &module_.global_scope());
      for (int i = 0; i < params.size(); ++i) {
        fun->def()->params.emplace_back(params[i].name.loc, params[i].name.text,
                                        fun_ty.param_types[i]);
        if (!fun->def()->scope.AddVar(fun->def()->params.back())) return;
      }

      if (!ParseBlock(fun->def()->body)) return;
    } else {
      if (!Expect(TokenType::kSemicolon)) {
        return;
      }
    }
  } else {
    auto* var = module_.alloc_var(ident->loc, ident->text, *type);
    if (!var) return;
    if (!module_.global_scope().AddVar(*var)) return;
    if (!Expect(TokenType::kSemicolon)) return;
  }
}

}  // namespace

Module Parse(Context& ctx) { return Parser(ctx).Parse(); }

}  // namespace jank
