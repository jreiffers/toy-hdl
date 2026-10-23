#ifndef JANK_AST_H__
#define JANK_AST_H__

#include <deque>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "jank/context.h"

namespace jank {

enum class PrimitiveType {
  kVoid,
  kBool,
  kInt,
};

class Node {
 public:
  explicit Node(SourceLocation loc) : loc_(loc) {}
  virtual ~Node() {}

  SourceLocation loc() const { return loc_; }

 private:
  SourceLocation loc_;
};

class Type {
 public:
  PrimitiveType type;

  bool operator==(const Type& o) const { return o.type == type; }
  bool operator!=(const Type& o) const { return !(*this == o); }
};

class VariableNode : public Node {
 public:
  explicit VariableNode(SourceLocation loc, std::string_view name, Type type)
      : Node(loc), name(name), type(type) {}

  std::string_view name;
  Type type;
};

// (expression type, token type, precedence,
// syntactic sugar assignment token type or kUnknownToken for none)
// clang-format off
#define JANK_VISIT_BINARY_OPERATORS(visit)                  \
  visit(kAdd, kPlus,     50, kAssignPlus)                   \
  visit(kSub, kMinus,    50, kAssignMinus)                  \
  visit(kMul, kAsterisk, 60, kAssignMul)                    \
  visit(kDiv, kDiv,      60, kAssignDiv)                    \
  visit(kMod, kMod,      60, kUnknownToken)                 \
  visit(kShl, kShl,      40, kAssignShl)                    \
  visit(kShr, kShr,      40, kAssignShr)                    \
  visit(kLt,  kLt,       30, kUnknownToken)                 \
  visit(kLe,  kLe,       30, kUnknownToken)                 \
  visit(kGt,  kGt,       30, kUnknownToken)                 \
  visit(kGe,  kGe,       30, kUnknownToken)                 \
  visit(kEq,  kEq,       20, kUnknownToken)                 \
  visit(kNe,  kNe,       20, kUnknownToken)

#define JANK_VISIT_UNARY_OPERATORS(visit)                   \
  visit(kNeg,        kNeg,      70)                         \
  visit(kNot,        kNot,      70)                         \
  visit(kUnaryMinus, kMinus,    70)


enum class ExpressionType {
  kUnknown,
#define JANK_VISITOR(type, ...) type,
  JANK_VISIT_BINARY_OPERATORS(JANK_VISITOR)
  JANK_VISIT_UNARY_OPERATORS(JANK_VISITOR)
#undef JANK_VISITOR
  kConditional,
  kArrayIndex,

  kConstant,
  kVariable,
  kCall,
};
// clang-format on

class Scope {
 public:
  Scope(Context& ctx) : ctx_(ctx), parent_(nullptr) {}
  explicit Scope(Scope* parent) : ctx_(parent->context()), parent_(parent) {}

  Node* Lookup(std::string_view name) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
      if (parent_) {
        return parent_->Lookup(name);
      }
      return nullptr;
    }
    return it->second;
  }

  bool Insert(std::string_view name, Node* entry) {
    return entries_.try_emplace(name, entry).second;
  }

  bool AddVar(VariableNode& var) {
    auto* existing_sym = Lookup(var.name);
    if (existing_sym) {
      auto* existing_var = dynamic_cast<VariableNode*>(existing_sym);
      if (existing_var) {
        ctx_.AddError(absl::StrCat("Attempted to redeclare variable."),
                      var.loc());
      } else {
        ctx_.AddError(
            absl::StrCat(
                "Attempted to declare variable which was previously declared "
                "as a different kind of symbol."),
            var.loc());
      }
      ctx_.AddError("Note: previously defined here:", existing_sym->loc());

      return false;
    }

    bool inserted = Insert(var.name, &var);
    (void)inserted;
    vars_.push_back(&var);
    assert(inserted);
    return true;
  }

  absl::Span<const VariableNode* const> vars() const { return vars_; }

  Context& context() { return ctx_; }

 private:
  Context& ctx_;
  Scope* parent_;
  std::vector<VariableNode*> vars_;
  absl::flat_hash_map<std::string_view, Node*> entries_;
};

struct ExpressionNode : public Node {
  using Node::Node;

  ExpressionType type = ExpressionType::kUnknown;
  int const_val;
  Node* symbol = nullptr;
  std::vector<ExpressionNode*> operands;
};

enum class InstructionType { kExpression, kAssign, kBlock, kIf, kWhile, kFor };

class Block;

// TODO should probably be a Node
struct InstructionNode {
  InstructionType type = InstructionType::kExpression;
  ExpressionNode* expression = nullptr;
  ExpressionNode* target = nullptr;
  Block* block = nullptr;  // Valid only for kind == kBlock

  // if (expression) [0] else [1]
  // while (expression) [0]
  // for (; expression; [0]) [1]
  //    [0] is nullptr if empty.

  std::vector<InstructionNode*> children;
};

class Block : public Node {
 public:
  Block(SourceLocation loc, Scope* parent) : Node(loc), scope(parent) {}

  Scope scope;
  absl::InlinedVector<InstructionNode, 5> instructions;
};

class FunctionDefinitionNode : public Node {
 public:
  explicit FunctionDefinitionNode(SourceLocation loc, Scope* parent)
      : Node(loc), scope(parent), body(loc, &scope) {}

  std::deque<VariableNode> params;
  Scope scope;
  Block body;
};

class FunctionType {
 public:
  Type return_type;
  std::vector<Type> param_types;

  bool operator==(const FunctionType& o) const {
    return return_type == o.return_type && param_types == o.param_types;
  }
  bool operator!=(const FunctionType& o) const { return !(*this == o); }
};

class FunctionNode : public Node {
 public:
  explicit FunctionNode(SourceLocation loc, std::string_view name,
                        FunctionType type)
      : Node(loc), name_(name), type_(type) {}

  std::string_view name() const { return name_; }
  const FunctionType& type() const { return type_; }

  std::optional<FunctionDefinitionNode>& def() { return def_; }
  const std::optional<FunctionDefinitionNode>& def() const { return def_; }

 private:
  std::string_view name_;
  FunctionType type_;
  std::optional<FunctionDefinitionNode> def_;
};

class Module {
 public:
  explicit Module(Context& ctx) : ctx_(&ctx), global_scope_(ctx) {}

  Scope& global_scope() { return global_scope_; }
  const Scope& global_scope() const { return global_scope_; }

  FunctionNode* declare_function(std::string_view name, FunctionType type,
                                 SourceLocation loc) {
    Node* res = global_scope().Lookup(name);
    if (res) {
      auto* fun = dynamic_cast<FunctionNode*>(res);
      if (fun) {
        if (fun->type() != type) {
          ctx_->AddError(absl::StrCat("Attempted to redeclare function '", name,
                                      "' with a different signature."),
                         loc);
          ctx_->AddError("Note: previously declared here:", res->loc());
          fun = nullptr;
        }
      } else {
        ctx_->AddError(absl::StrCat("Attempted to declare function which was "
                                    "previously declared as a "
                                    "different kind of symbol."),
                       loc);
        ctx_->AddError("Note: previously declared here:", res->loc());
      }
      return fun;
    }

    auto* f = &functions_.emplace_back(loc, name, type);
    bool inserted = global_scope().Insert(name, f);
    (void)inserted;
    assert(inserted);
    return f;
  }

  VariableNode* alloc_var(SourceLocation loc, std::string_view name,
                          Type type) {
    if (type.type == PrimitiveType::kVoid) {
      ctx_->AddError("Cannot declare a void global variable", loc);
      return nullptr;
    }

    return &var_nodes_.emplace_back(loc, name, type);
  }

  const std::deque<FunctionNode>& functions() const { return functions_; }

  ExpressionNode* alloc_expr(SourceLocation loc, ExpressionType type) {
    auto* expr = &expression_nodes_.emplace_back(loc);
    expr->type = type;
    return expr;
  }

  Block* alloc_block(SourceLocation loc, Scope* parent) {
    return &blocks_.emplace_back(loc, parent);
  }

  InstructionNode* alloc_instr() { return &instrs_.emplace_back(); }

 private:
  Context* ctx_;
  Scope global_scope_;
  std::deque<FunctionNode> functions_;
  std::deque<VariableNode> var_nodes_;
  std::deque<ExpressionNode> expression_nodes_;
  std::deque<FunctionDefinitionNode> fun_defs_;
  std::deque<Block> blocks_;
  std::deque<InstructionNode> instrs_;
};

}  // namespace jank

#endif
