#ifndef GATE_LIB_H__
#define GATE_LIB_H__

#include <array>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

enum class GateKind {
  kDead,

  kNot,
  kTriStateBuffer,  // enable, !enable, data
  kMux,             // condition, !condition, true, false
  kNand,
  kNor,
  kLookup,
};

struct Gate;

using GateTerminal = std::pair<Gate* /* null if input */, int /* output */>;
using GateUse = GateTerminal /* user, input id */;

template <>
struct std::hash<GateTerminal> {
  std::size_t operator()(const GateTerminal& g) const {
    return hash<Gate*>()(g.first) ^ hash<int>()(g.second);
  }
};

constexpr GateTerminal kLowGate{nullptr, -1};
constexpr GateTerminal kHighGate{nullptr, -2};

struct GateNetwork;

struct Gate {
  Gate(GateNetwork* owner, GateKind kind,
       absl::InlinedVector<GateTerminal, 2> inputs, int num_outputs = 1);

  GateTerminal output(int index = 0) { return {this, index}; }

  int num_inputs() const { return inputs_.size(); }
  int num_outputs() const { return num_outputs_; }
  GateKind kind() const { return kind_; }

  absl::Span<const GateTerminal> inputs() { return inputs_; }
  GateTerminal input(int index) { return inputs_[index]; }
  void SetInput(int index, GateTerminal new_input);

  void ReplaceAllUsesWith(GateTerminal replacement);

  void set_lookup_data(uint64_t data) { lookup_data_ = data; }
  uint64_t lookup_data() const { return lookup_data_; }

  // Erases all inputs that reference the given terminal. If kind == kLookup,
  // only considers the positive inputs for matching and also erased the
  // corresponding negated ones.
  uint64_t EraseInputs(GateTerminal input);

  bool HasNoUse();
  bool Canonicalize();
  void Erase();

  bool operator<(const Gate& rhs) const;

  const std::vector<std::string>& scope() const { return scope_; }
  std::vector<std::string>& scope() { return scope_; }
  void set_scope(std::vector<std::string> scope) { scope_ = std::move(scope); }

 private:
  friend struct GateNetwork;

  GateNetwork* owner_;
  GateKind kind_;
  absl::InlinedVector<GateTerminal, 2> inputs_;
  uint64_t lookup_data_;
  int num_outputs_;
  std::vector<std::string> scope_;
};

std::string to_string(const Gate& gate);

template <int bw>
struct GateReg {
  std::array<GateTerminal, bw> vals;

  GateTerminal operator[](size_t i) const { return vals[i]; }
  GateTerminal& operator[](size_t i) { return vals[i]; }
};

struct DynGateReg {
 public:
  explicit DynGateReg(std::vector<GateTerminal> vals)
      : vals_(std::move(vals)) {}
  template <int bw>
  DynGateReg(const GateReg<bw>& reg)
      : vals_(reg.vals.begin(), reg.vals.end()) {}

  int bitwidth() const { return vals_.size(); }
  GateTerminal operator[](size_t i) const { return vals_[i]; }
  GateTerminal& operator[](size_t i) { return vals_[i]; }

 private:
  std::vector<GateTerminal> vals_;
};

struct GateNetwork {
 public:
  GateNetwork();
  GateNetwork(const GateNetwork&) = delete;
  GateNetwork(GateNetwork&&) = delete;

  GateNetwork& operator=(const GateNetwork&) = delete;
  GateNetwork& operator=(GateNetwork&&) = delete;

  absl::Span<const int> input_bitwidths() const { return input_bitwidths_; }

  int num_inputs() const { return input_bitwidths_.size(); }
  int num_outputs() const { return output_bitwidths_.size(); }
  int total_input_bits() const { return input_offsets_.back(); }

  void DeclareOutput(const DynGateReg& reg, std::string label = "");
  DynGateReg GetOutput(int index);

  DynGateReg GetInput(int index) const {
    std::vector<GateTerminal> res;
    int bw = input_bitwidths_[index];
    res.reserve(bw);
    for (int i = 0; i < bw; ++i) {
      res.push_back({nullptr, input_offsets_[index] + i});
    }
    return DynGateReg(std::move(res));
  }

  const std::string& input_label(int index) const {
    return input_labels_[index];
  }
  const std::string& output_label(int index) const {
    return output_labels_[index];
  }

  template <int bw>
  GateReg<bw> AddInput(std::string label = "") {
    if (label.empty()) {
      label = absl::StrCat("i", input_labels_.size());
    }
    input_labels_.push_back(std::move(label));
    GateReg<bw> result;
    for (int i = 0; i < bw; ++i) {
      result[i] = {nullptr, input_offsets_.back() + i};
    }
    input_offsets_.push_back(input_offsets_.back() + bw);
    input_bitwidths_.push_back(bw);
    return result;
  }

  Gate& AddGate(GateKind kind,
                absl::InlinedVector<GateTerminal, 2> inputs = {});

  GateTerminal Xor(GateTerminal a, GateTerminal b) {
    GateTerminal not_a = Not(a);
    GateTerminal not_b = Not(b);
    auto& gate = AddGate(GateKind::kLookup, {a, b, not_a, not_b});
    gate.set_lookup_data(0b0110);
    return gate.output();
  }

  GateTerminal And(GateTerminal a, GateTerminal b) { return Not(Nand({a, b})); }

  GateTerminal Or(GateTerminal a, GateTerminal b) { return Not(Nor({a, b})); }

  GateTerminal Not(GateTerminal a) {
    if (a == kHighGate) return kLowGate;
    if (a == kLowGate) return kHighGate;
    return AddGate(GateKind::kNot, {a}).output();
  }

  GateTerminal Nor(absl::Span<const GateTerminal> in) {
    return AddGate(GateKind::kNor,
                   absl::InlinedVector<GateTerminal, 2>(in.begin(), in.end()))
        .output();
  }

  GateTerminal Nand(absl::Span<const GateTerminal> in) {
    return AddGate(GateKind::kNand,
                   absl::InlinedVector<GateTerminal, 2>(in.begin(), in.end()))
        .output();
  }

  GateTerminal Mux(GateTerminal sel, GateTerminal high, GateTerminal low) {
    return AddGate(GateKind::kMux, {sel, Not(sel), high, low}).output();
  }

  template <int bw>
  GateReg<bw> Not(GateReg<bw> in) {
    GateReg<bw> out;
    for (int i = 0; i < bw; ++i) {
      out[i] = Not(in[i]);
    }
    return out;
  }

  template <int bw>
  GateReg<bw> Mux(GateTerminal sel, GateReg<bw> low, GateReg<bw> high);

  template <int bw>
  GateReg<bw> Xor(GateReg<bw> lhs, GateReg<bw> rhs) {
    GateReg<bw> out;
    for (int i = 0; i < bw; ++i) {
      out[i] = Xor(lhs[i], rhs[i]);
    }
    return out;
  }

  template <int bw>
  GateTerminal Eq(GateReg<bw> lhs, GateReg<bw> rhs) {
    auto x = Xor(lhs, rhs);
    return Nor(x.vals);
  }

  template <int bw>
  GateReg<bw> TriStateBuffer(GateTerminal enable, GateReg<bw> value) {
    auto not_enable = Not(enable);
    GateReg<bw> out;
    for (int i = 0; i < bw; ++i) {
      out[i] =
          AddGate(GateKind::kTriStateBuffer, {enable, not_enable, value[i]})
              .output();
    }
    return out;
  }

  void WalkUnordered(
      const std::function<void(int id, const Gate& gate)>& fn) const;
  void WalkUnordered(const std::function<void(int id, Gate& gate)>& fn);

  const std::unordered_set<GateUse>& GetUsers(GateTerminal terminal) const;

  void PushScope(std::string scope) { scope_.push_back(std::move(scope)); }
  void PopScope() { scope_.pop_back(); }
  const std::vector<std::string>& current_scope() const { return scope_; }

 private:
  friend struct Gate;

  std::vector<int> input_bitwidths_;
  std::vector<int> input_offsets_ = {0};
  std::vector<std::string> input_labels_;
  std::vector<std::string> output_labels_;
  std::vector<int> output_bitwidths_;
  std::vector<int> output_offsets_ = {0};

  std::vector<std::string> scope_;

  std::deque<Gate> gates_;
  std::vector<Gate*> free_gates_;
  std::unordered_map<GateTerminal, std::unordered_set<GateUse>> uses_;
  Gate& output_gate_;  // Fake gate to keep output references.
};

struct ScopeGuard {
  ScopeGuard(GateNetwork& net, std::string scope) : net_(net), depth_(1) {
    net_.PushScope(std::move(scope));
  }
  ScopeGuard(GateNetwork& net, std::vector<std::string> scope)
      : net_(net), depth_(scope.size()) {
    for (auto& s : scope) net_.PushScope(std::move(s));
  }

  ~ScopeGuard() {
    for (int i = 0; i < depth_; ++i) net_.PopScope();
  }

 private:
  GateNetwork& net_;
  int depth_;
};

GateReg<2> MakeHalfAdder(GateNetwork& net, GateTerminal a, GateTerminal b);
GateReg<2> MakeFullAdder(GateNetwork& net, GateTerminal a, GateTerminal b,
                         GateTerminal c);

template <int bw>
std::pair<GateReg<bw>, GateTerminal /* carry */> MakeAdder(
    GateNetwork& net, const GateReg<bw>& lhs, const GateReg<bw>& rhs,
    GateTerminal carry = kLowGate) {
  ScopeGuard guard(net, "adder");
  GateReg<bw> result;
  for (int i = 0; i < bw; ++i) {
    ScopeGuard guard(net, absl::StrCat("bit", i));
    auto partial = MakeFullAdder(net, lhs[i], rhs[i], carry).vals;
    result[i] = partial[0];
    carry = partial[1];
  }
  return {result, carry};
}

template <int bw>
GateReg<bw> GateNetwork::Mux(GateTerminal sel, GateReg<bw> low,
                             GateReg<bw> high) {
  ScopeGuard guard(*this, "mux");
  GateReg<bw> out;
  for (int i = 0; i < bw; ++i) {
    ScopeGuard guard(*this, absl::StrCat("bit", i));
    out[i] = Mux(sel, low[i], high[i]);
  }
  return out;
}

GateReg<2> /*q, ~q*/ MakeSrLatch(GateNetwork& net, GateTerminal s,
                                 GateTerminal r, GateTerminal reset);
GateReg<2> /*q, ~q*/ MakeGatedSrLatch(GateNetwork& net, GateTerminal s,
                                      GateTerminal r, GateTerminal e,
                                      GateTerminal reset);
GateReg<2> /*q, ~q*/ MakeGatedDLatch(GateNetwork& net, GateTerminal d,
                                     GateTerminal e, GateTerminal reset);
// Stores on falling clk edge.
GateReg<2> /*q, ~q*/ MakeDFlipFlop(GateNetwork& net, GateTerminal d,
                                   GateTerminal clk,
                                   GateTerminal reset = kLowGate);

#endif
