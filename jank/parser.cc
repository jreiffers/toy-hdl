#include "jank/parser.h"

#include <initializer_list>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "jank/ast.h"
#include "jank/context.h"
#include "jank/lexer.h"

namespace jank {
namespace {

#define EXPECT(condition, ...)           \
  do {                                   \
    if (!(condition)) {                  \
      __VA_OPT__(Errors({__VA_ARGS__});) \
      return false;                      \
    }                                    \
  } while (false)

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
    EXPECT(maybe_ty, {"Expected type.", tokenizer_.last_token().loc});
    ty = *maybe_ty;
    return true;
  }

  std::optional<NameAndType> TryParseNameAndType();

  void Error(std::string msg) {
    ctx_.AddError(std::move(msg), tokenizer_.last_token().loc);
  }

  void Errors(std::initializer_list<jank::Error> errors) {
    for (const auto& error : errors) {
      ctx_.AddError(error.message, error.loc);
    }
  }

  std::optional<bool> TryParseVarDecl(Block& b);

  bool ParseBlock(Block& b);
  bool ParseInstruction(Block& parent, InstructionNode& out, bool semi = true);
  bool ParseIf(Block& parent, InstructionNode& i);
  bool ParseWhile(Block& parent, InstructionNode& i);
  bool ParseFor(Block& parent, InstructionNode& i);

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
    EXPECT(Consume(ty), {absl::StrCat("Expected '", to_string(ty), "'."),
                         tokenizer_.last_token().loc});
    return true;
  }

  template <typename C, typename F>
  std::optional<bool> TryParseList(C& c, F&& f) {
    if (!Consume(TokenType::kParenLeft)) return std::nullopt;
    if (Consume(TokenType::kParenRight)) return true;
    do {
      EXPECT(f(c.emplace_back()));
    } while (Consume(TokenType::kComma));
    return Expect(TokenType::kParenRight);
  }

  template <typename C, typename F>
  bool ParseList(C& c, F&& f) {
    auto success = TryParseList(c, std::forward<F>(f));
    EXPECT(success, {"Expected '('.", tokenizer_.last_token().loc});
    return *success;
  }

  std::optional<Token> Get(TokenType ty) {
    if (!Expect(ty)) return std::nullopt;
    return tokenizer_.last_token();
  }

  bool Get(TokenType ty, Token& out) {
    EXPECT(Consume(ty));
    out = tokenizer_.last_token();
    return true;
  }

  bool ParseExpression(Scope& scope, ExpressionNode*& out, int min_prec = 0);

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

#define JANK_VISITOR(type, token, prec, ...) \
  {TokenType::token, {ExpressionType::type, prec}},
const auto& kOperators =
    *new absl::flat_hash_map<TokenType, std::pair<ExpressionType, int>>{
        JANK_VISIT_BINARY_OPERATORS(JANK_VISITOR){
            TokenType::kCond, {ExpressionType::kConditional, 10}},
        {TokenType::kBracketLeft, {ExpressionType::kArrayIndex, 0}},
    };

const auto& kUnaryOperators =
    *new absl::flat_hash_map<TokenType, std::pair<ExpressionType, int>>{
        JANK_VISIT_UNARY_OPERATORS(JANK_VISITOR)};
#undef JANK_VISITOR

const auto& kAssignmentOperators = *[] {
  auto* result = new absl::flat_hash_map<TokenType, ExpressionType>;
#define JANK_VISITOR(type, _, __, assignment)                        \
  if constexpr (TokenType::assignment != TokenType::kUnknownToken) { \
    result->emplace(TokenType::assignment, ExpressionType::type);    \
  }
  JANK_VISIT_BINARY_OPERATORS(JANK_VISITOR)
#undef JANK_VISITOR
  return result;
}();

bool Parser::ParseExpression(Scope& scope, ExpressionNode*& out, int min_prec) {
  Token token = tokenizer_.get();
  ExpressionNode* lhs = nullptr;

  switch (token.type) {
    case TokenType::kIntLiteral:
      lhs = module_.alloc_expr(token.loc, ExpressionType::kConstant);
      CHECK(absl::SimpleAtoi(token.text, &lhs->const_val));
      break;
    case TokenType::kIdentifier: {
      auto* symbol = scope.Lookup(token.text);
      EXPECT(symbol, {absl::StrCat("Undeclared identifier '", token.text, "'."),
                      token.loc});
      if (dynamic_cast<FunctionNode*>(symbol)) {
        lhs = module_.alloc_expr(token.loc, ExpressionType::kCall);
        EXPECT(ParseList(lhs->operands, [&](ExpressionNode*& arg) {
          return ParseExpression(scope, arg);
        }));
      } else {
        EXPECT(tokenizer_.peek().type != TokenType::kParenLeft,
               {"Only functions can be called.", token.loc},
               {"Note: declared here:", symbol->loc()});
        lhs = module_.alloc_expr(token.loc, ExpressionType::kVariable);
      }
      lhs->symbol = symbol;
      break;
    }
    case TokenType::kParenLeft:
      EXPECT(ParseExpression(scope, lhs) && Expect(TokenType::kParenRight));
      break;
    default:
      auto unary = kUnaryOperators.find(token.type);
      if (unary != kUnaryOperators.end()) {
        EXPECT(ParseExpression(scope, lhs, unary->second.second));
        auto* expr = module_.alloc_expr(token.loc, unary->second.first);
        expr->operands = {lhs};
        lhs = expr;
      } else {
        ctx_.AddError("Expected expression.", token.loc);
        return false;
      }
  }

  while (true) {
    auto op_it = kOperators.find(tokenizer_.peek().type);
    if (op_it == kOperators.end()) break;
    auto [type, prec] = op_it->second;
    if (prec < min_prec) break;
    Token op = tokenizer_.get();

    bool cond = op.type == TokenType::kCond;
    prec = cond ? prec : prec + 1;

    auto* expr = module_.alloc_expr(op.loc, type);
    expr->operands = {lhs, nullptr};
    EXPECT(ParseExpression(scope, expr->operands[1], prec));
    if (op.type == TokenType::kBracketLeft) {
      EXPECT(Expect(TokenType::kBracketRight));
    } else if (op.type == TokenType::kCond) {
      expr->operands.push_back(nullptr);
      EXPECT(Expect(TokenType::kColon) &&
             ParseExpression(scope, expr->operands[2], prec));
    }
    lhs = expr;
  }
  out = lhs;
  return true;
}

bool Parser::ParseInstruction(Block& parent, InstructionNode& out, bool semi) {
  switch (tokenizer_.peek().type) {
    case TokenType::kIf:
      return semi && ParseIf(parent, out);
    case TokenType::kFor:
      return semi && ParseFor(parent, out);
    case TokenType::kWhile:
      return semi && ParseWhile(parent, out);
    case TokenType::kBraceLeft: {
      EXPECT(semi);
      out.type = InstructionType::kBlock;
      out.block = module_.alloc_block(tokenizer_.peek().loc, &parent.scope);
      return ParseBlock(*out.block);
    }
    default:
      EXPECT(ParseExpression(parent.scope, out.expression));
      auto operation = kAssignmentOperators.find(tokenizer_.peek().type);
      if (tokenizer_.peek().type == TokenType::kAssign ||
          operation != kAssignmentOperators.end()) {
        auto op = tokenizer_.get();
        auto* target = out.expression;
        EXPECT(target->type == ExpressionType::kVariable ||
                   target->type == ExpressionType::kArrayIndex,
               {"Expected assignable expression.", op.loc});
        out.type = InstructionType::kAssign;
        out.target = target;
        EXPECT(ParseExpression(parent.scope, out.expression));
        if (operation != kAssignmentOperators.end()) {
          auto* value = module_.alloc_expr(op.loc, operation->second);
          value->operands = {target, out.expression};
          out.expression = value;
        }
      }
      return !semi || Expect(TokenType::kSemicolon);
  }
}

std::optional<bool> Parser::TryParseVarDecl(Block& b) {
  auto type = TryParseType();
  if (!type) {
    return std::nullopt;
  }

  auto ident = Get(TokenType::kIdentifier);
  EXPECT(ident);
  InstructionNode* assign = nullptr;
  if (tokenizer_.peek().type == TokenType::kAssign) {
    tokenizer_.get();
    assign = &b.instructions.emplace_back();
    EXPECT(ParseExpression(b.scope, assign->expression));
  }

  auto* var = module_.alloc_var(ident->loc, ident->text, *type);
  EXPECT(var && b.scope.AddVar(*var));
  if (assign) {
    assign->type = InstructionType::kAssign;
    assign->target = module_.alloc_expr(var->loc(), ExpressionType::kVariable);
    assign->target->symbol = var;
  }

  return Expect(TokenType::kSemicolon);
}

bool Parser::ParseBlock(Block& b) {
  EXPECT(Expect(TokenType::kBraceLeft));
  while (tokenizer_ && tokenizer_.peek().type != TokenType::kBraceRight) {
    if (auto decl = TryParseVarDecl(b)) {
      EXPECT(*decl);
      continue;
    }
    EXPECT(ParseInstruction(b, b.instructions.emplace_back()));
  }
  return Expect(TokenType::kBraceRight);
}

bool Parser::ParseIf(Block& parent, InstructionNode& i) {
  i.type = InstructionType::kIf;
  EXPECT(Expect(TokenType::kIf) && Expect(TokenType::kParenLeft) &&
         ParseExpression(parent.scope, i.expression) &&
         Expect(TokenType::kParenRight));
  EXPECT(ParseInstruction(parent,
                          *i.children.emplace_back(module_.alloc_instr())));
  if (tokenizer_.peek().type == TokenType::kElse) {
    tokenizer_.get();
    EXPECT(ParseInstruction(parent,
                            *i.children.emplace_back(module_.alloc_instr())));
  }
  return true;
}

bool Parser::ParseFor(Block& parent, InstructionNode& i) {
  auto t = tokenizer_.get();
  EXPECT(Expect(TokenType::kParenLeft));
  i.type = InstructionType::kBlock;
  i.block = module_.alloc_block(t.loc, &parent.scope);

  if (auto decl = TryParseVarDecl(*i.block)) {
    EXPECT(*decl);
  } else {
    EXPECT(Expect(TokenType::kSemicolon));
  }

  auto& for_inst = i.block->instructions.emplace_back();
  for_inst.type = InstructionType::kFor;
  for_inst.children = {nullptr, module_.alloc_instr()};

  EXPECT(ParseExpression(i.block->scope, for_inst.expression) &&
         Expect(TokenType::kSemicolon));

  if (tokenizer_.peek().type != TokenType::kParenRight) {
    for_inst.children[0] = module_.alloc_instr();
    EXPECT(ParseInstruction(*i.block, *for_inst.children[0], /*semi=*/false));
  }

  EXPECT(Expect(TokenType::kParenRight));
  return ParseInstruction(*i.block, *for_inst.children[1]);
}

bool Parser::ParseWhile(Block& parent, InstructionNode& i) {
  i.type = InstructionType::kWhile;
  EXPECT(Expect(TokenType::kWhile) && Expect(TokenType::kParenLeft) &&
         ParseExpression(parent.scope, i.expression) &&
         Expect(TokenType::kParenRight));
  return ParseInstruction(parent,
                          *i.children.emplace_back(module_.alloc_instr()));
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

  if (auto match_and_success = TryParseList(
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
