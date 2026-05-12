#include "jank/print.h"

#include "absl/strings/str_cat.h"

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

  Printer& PrintBlock(const Block& b);

  Printer& ty(Type type) { return ty(type.type); }
};

struct Indent {
  Indent(Printer* printer) : pr(printer) { ++pr->indent; }
  ~Indent() { --pr->indent; }
  Printer* pr;
};

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
