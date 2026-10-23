#include "jank/print.h"

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "jank/lexer.h"

namespace jank {
namespace {

struct Printer {
  std::string buf;
  int indent = 0;
  bool start = true;

  Printer& endl() {
    absl::StrAppend(&buf, "\n");
    start = true;
    return *this;
  }

  Printer& s(absl::string_view str) {
    if (start) {
      absl::StrAppend(&buf, std::string(2 * indent, ' '));
      start = false;
    }
    absl::StrAppend(&buf, str);
    return *this;
  }

  Printer& ty(PrimitiveType type) {
    switch (type) {
      case PrimitiveType::kVoid:
        return s("void");
      case PrimitiveType::kInt:
        return s("int");
      case PrimitiveType::kBool:
        return s("bool");
    }
  }

  Printer& instr(const InstructionNode& inst, bool semi = true);

  Printer& PrintBlock(const Block& b);
  Printer& expr(const ExpressionNode& e);

  Printer& ty(Type type) { return ty(type.type); }
};

struct Indent {
  Indent(Printer* printer) : pr(printer) { ++pr->indent; }
  ~Indent() { --pr->indent; }
  Printer* pr;
};

std::string_view Operator(ExpressionType type) {
  switch (type) {
#define JANK_VISITOR(type, token, ...) \
  case ExpressionType::type:           \
    return to_string(TokenType::token);
    JANK_VISIT_BINARY_OPERATORS(JANK_VISITOR)
    JANK_VISIT_UNARY_OPERATORS(JANK_VISITOR)
#undef JANK_VISITOR
    default:
      return "";
  }
}

Printer& Printer::expr(const ExpressionNode& e) {
  switch (e.type) {
    case ExpressionType::kConstant:
      return s(absl::StrCat(e.const_val));
    case ExpressionType::kVariable:
      return s(dynamic_cast<const VariableNode*>(e.symbol)->name);
    case ExpressionType::kCall:
      s(dynamic_cast<const FunctionNode*>(e.symbol)->name()).s("(");
      for (size_t i = 0; i < e.operands.size(); ++i) {
        if (i > 0) s(", ");
        expr(*e.operands[i]);
      }
      return s(")");
    case ExpressionType::kArrayIndex:
      return expr(*e.operands[0]).s("[").expr(*e.operands[1]).s("]");
    case ExpressionType::kConditional:
      return s("(")
          .expr(*e.operands[0])
          .s(" ? ")
          .expr(*e.operands[1])
          .s(" : ")
          .expr(*e.operands[2])
          .s(")");
#define JANK_VISITOR(exp, ...) \
  case ExpressionType::exp:    \
    return s("(").s(Operator(e.type)).expr(*e.operands[0]).s(")");
      JANK_VISIT_UNARY_OPERATORS(JANK_VISITOR)
#undef JANK_VISITOR
    case ExpressionType::kUnknown:
      return s("<unknown>");
    default:
      return s("(")
          .expr(*e.operands[0])
          .s(" ")
          .s(Operator(e.type))
          .s(" ")
          .expr(*e.operands[1])
          .s(")");
  }
}

Printer& Printer::instr(const InstructionNode& inst, bool semi) {
  switch (inst.type) {
    case InstructionType::kAssign:
      CHECK(inst.target);
      expr(*inst.target).s(" = ");
    case InstructionType::kExpression:
      expr(*inst.expression);
      if (semi) s(";");
      break;
    case InstructionType::kBlock:
      PrintBlock(*inst.block);
      break;
    case InstructionType::kIf:
      s("if (").expr(*inst.expression).s(") ").instr(*inst.children[0]);

      if (inst.children.size() > 1) {
        CHECK_EQ(inst.children.size(), 2);
        endl().s("else ").instr(*inst.children[1]);
      }
      break;
    case InstructionType::kFor:
      CHECK_EQ(inst.children.size(), 2);
      s("for (; ").expr(*inst.expression).s("; ");
      if (inst.children[0]) instr(*inst.children[0], /*semi=*/false);
      s(") ").instr(*inst.children[1]);
      break;
    case InstructionType::kWhile:
      CHECK_EQ(inst.children.size(), 1);
      s("while (").expr(*inst.expression).s(") ").instr(*inst.children[0]);
      break;
    default:
      CHECK(false);
  }

  return *this;
}

Printer& Printer::PrintBlock(const Block& b) {
  s("{");
  if (b.scope.vars().empty() && b.instructions.empty()) {
    return s("}");
  }

  endl();
  {
    Indent block_indent(this);
    for (auto* var : b.scope.vars()) {
      ty(var->type).s(" ").s(var->name).s(";").endl();
    }
    for (const auto& instruction : b.instructions) {
      instr(instruction).endl();
    }
  }
  s("}");
  return *this;
}

}  // namespace

std::string Print(const Module& mod) {
  std::string out;

  Printer p;

  bool any = false;
  for (auto* var : mod.global_scope().vars()) {
    p.ty(var->type).s(" ").s(var->name).s(";").endl();
    any = true;
  }
  if (any) p.endl();

  auto print_fun_decl = [&](const FunctionNode& fun) -> Printer& {
    p.ty(fun.type().return_type).s(" ").s(fun.name()).s("(");
    bool first = true;
    int i = 0;
    for (auto& param : fun.type().param_types) {
      if (!first) p.s(", ");
      first = false;
      p.ty(param.type);
      if (fun.def()) {
        p.s(" ").s(fun.def()->params[i].name);
      }
      ++i;
    }
    return p.s(")");
  };

  for (auto& fun : mod.functions()) {
    print_fun_decl(fun).s(";").endl();
  }

  p.endl();
  for (auto& fun : mod.functions()) {
    auto& maybe_def = fun.def();
    if (!maybe_def) continue;

    auto& def = *maybe_def;
    print_fun_decl(fun).s(" ");
    p.PrintBlock(def.body).endl();
  }

  return std::move(p.buf);
}

}  // namespace jank
