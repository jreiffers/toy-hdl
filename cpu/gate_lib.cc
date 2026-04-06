#include "gate_lib.h"

#include <iostream>

std::string to_string(const Gate& gate) {
  switch (gate.kind()) {
    case GateKind::kDead:
      return "dead";
    case GateKind::kNot:
      return "not";
    case GateKind::kMux:
      return "mux";
    case GateKind::kNand:
      return "nand";
    case GateKind::kNor:
      return "nor";
    case GateKind::kLookup: {
      if (gate.lookup_data() == 0b0110 && gate.num_inputs() == 4) {
        return "xor";
      }
      std::stringstream os;
      os << "lookup(" << gate.lookup_data() << ")";
      return os.str();
    }
  }
}

GateReg<2> MakeHalfAdder(GateNetwork& net, GateTerminal a, GateTerminal b) {
  return {net.Xor(a, b), net.And(a, b)};
}

GateReg<2> MakeFullAdder(GateNetwork& net, GateTerminal a, GateTerminal b,
                         GateTerminal c) {
  auto [sum_ab, carry_1] = MakeHalfAdder(net, a, b).vals;
  auto [sum_abc, carry_2] = MakeHalfAdder(net, sum_ab, c).vals;
  return {sum_abc, net.Or(carry_1, carry_2)};
}

GateReg<2> MakeSrLatch(GateNetwork& net, GateTerminal s, GateTerminal r) {
  auto a = net.Nor({r, kHighGate});
  auto b = net.Nor({s, kHighGate});

  a.first->SetInput(1, b);
  b.first->SetInput(1, a);

  return {a, b};
}

GateReg<2> /*q, ~q*/ MakeGatedSrLatch(GateNetwork& net, GateTerminal s,
                                      GateTerminal r, GateTerminal e) {
  return MakeSrLatch(net, net.And(s, e), net.And(r, e));
}

GateReg<2> /*q, ~q*/ MakeGatedDLatch(GateNetwork& net, GateTerminal d,
                                     GateTerminal e) {
  return MakeGatedSrLatch(net, d, net.Not(d), e);
}

GateReg<2> /*q, ~q*/ MakeDFlipFlop(GateNetwork& net, GateTerminal d,
                                   GateTerminal clk) {
  // TODO: The classical D flip flop should be more efficient in terms of
  // transistor use than the master-slave one.
  auto q = MakeGatedDLatch(net, d, clk)[0];
  auto not_clk = net.Not(clk);
  return MakeGatedDLatch(net, q, not_clk);
}

constexpr int kMaxOutputs = 128;
constexpr GateTerminal kNullTerminal{nullptr, std::numeric_limits<int>::min()};

GateNetwork::GateNetwork()
    : output_gate_(gates_.emplace_back(
          this, GateKind::kDead,
          absl::InlinedVector<GateTerminal, 2>(kMaxOutputs, kNullTerminal),
          0)) {}

Gate& GateNetwork::AddGate(GateKind kind,
                           absl::InlinedVector<GateTerminal, 2> inputs) {
  return gates_.emplace_back(this, kind, std::move(inputs));
}

void GateNetwork::DeclareOutput(const DynGateReg& reg) {
  assert(output_offsets_.back() + reg.bitwidth() <= kMaxOutputs);
  output_bitwidths_.push_back(reg.bitwidth());

  for (int i = 0; i < reg.bitwidth(); ++i) {
    int index = output_offsets_.back() + i;
    uses_[reg[i]].insert({&output_gate_, index});
    output_gate_.inputs_[index] = reg[i];
  }

  output_offsets_.push_back(output_offsets_.back() + reg.bitwidth());
}

DynGateReg GateNetwork::GetOutput(int index) {
  std::vector<GateTerminal> res;
  int bw = output_bitwidths_[index];
  res.reserve(bw);
  for (int i = 0; i < bw; ++i) {
    res.push_back(output_gate_.input(output_offsets_[index] + i));
  }
  return DynGateReg(std::move(res));
}

Gate::Gate(GateNetwork* owner, GateKind kind,
           absl::InlinedVector<GateTerminal, 2> inputs, int num_outputs)
    : owner_(owner),
      kind_(kind),
      inputs_(std::move(inputs)),
      lookup_data_(0),
      num_outputs_(num_outputs) {
  for (int i = 0; i < inputs_.size(); ++i) {
    owner_->uses_[inputs_[i]].insert({this, i});
  }
}

void GateNetwork::WalkUnordered(
    const std::function<void(int id, const Gate& gate)>& fn) const {
  for (int i = 0; i < gates_.size(); ++i) {
    if (gates_[i].kind_ == GateKind::kDead) continue;
    fn(i, gates_[i]);
  }
}

void GateNetwork::WalkUnordered(
    const std::function<void(int id, Gate& gate)>& fn) {
  for (int i = 0; i < gates_.size(); ++i) {
    if (gates_[i].kind_ == GateKind::kDead) continue;
    fn(i, gates_[i]);
  }
}

const std::unordered_set<GateUse>& GateNetwork::GetUsers(
    GateTerminal terminal) const {
  static auto& kNone = *new std::unordered_set<GateUse>();
  auto it = uses_.find(terminal);
  if (it == uses_.end()) return kNone;
  return it->second;
}

bool Gate::Canonicalize() {
  bool changed = false;

  if (kind_ == GateKind::kNand) {
    changed = EraseInputs(kHighGate) != 0;
  } else if (kind_ == GateKind::kNor) {
    changed = EraseInputs(kLowGate) != 0;
  }

  if (kind_ == GateKind::kNand || kind_ == GateKind::kNor) {
    if (inputs_.size() == 1) {
      kind_ = GateKind::kNot;
      changed = true;
    }
  }

  if (kind_ == GateKind::kLookup) {
    int lut_size = 1 << (inputs_.size() / 2);

    uint64_t erased, val;
    erased = val = EraseInputs(kHighGate);
    if (!erased) {
      erased = EraseInputs(kLowGate);
    }

    if (erased) {
      uint64_t new_lut = 0;
      int out_index = 0;
      for (int i = 0; i < lut_size; ++i) {
        if ((i & erased) != val) {
          new_lut |= ((lookup_data_ >> i) & 1) << out_index;
          ++out_index;
        }
      }

      lookup_data_ = new_lut;
      changed = true;
    }
  }

  if (HasNoUse() && kind_ != GateKind::kDead) {
    Erase();
    changed = true;
  }

  return changed;
}

void Gate::Erase() {
  kind_ = GateKind::kDead;
  for (int i = 0; i < inputs_.size(); ++i) {
    bool erased = owner_->uses_[inputs_[i]].erase({this, i}) == 1;
    (void)erased;
    assert(erased);
  }
  inputs_.clear();
}

void Gate::ReplaceAllUsesWith(GateTerminal replacement) {
  if (replacement == output(0)) return;

  assert(num_outputs_ == 1);

  auto use_it = owner_->uses_.find(output(0));
  if (use_it == owner_->uses_.end()) return;

  // TODO: copy should be avoidable.
  auto uses = use_it->second;

  for (auto [gate, input] : uses) {
    gate->SetInput(input, replacement);
  }

  assert(owner_->uses_[output(0)].empty());
  owner_->uses_.erase(output(0));
}

void Gate::SetInput(int index, GateTerminal new_input) {
  if (new_input == inputs_[index]) return;

  bool erased = owner_->uses_[input(index)].erase({this, index}) == 1;
  (void)erased;
  assert(erased);

  bool inserted = owner_->uses_[new_input].insert({this, index}).second;
  (void)inserted;
  assert(inserted);

  inputs_[index] = new_input;
}

uint64_t Gate::EraseInputs(GateTerminal input) {
  assert(inputs_.size() <= 64);
  uint64_t erased = 0;

  int max_index = inputs_.size();
  if (kind_ == GateKind::kLookup) {
    max_index = max_index / 2;
  }

  // Clear all uses.
  for (int i = 0; i < num_inputs(); ++i) {
    auto erased = owner_->uses_[this->input(i)].erase({this, i}) == 1;
    assert(erased);
  }

  for (int i = 0; i < max_index; ++i) {
    if (inputs_[i] == input) {
      inputs_[i] = kNullTerminal;
      if (kind_ == GateKind::kLookup) {
        inputs_[i + max_index] = kNullTerminal;
      }
      erased |= 1ull << i;
    }
  }

  inputs_.erase(std::remove(inputs_.begin(), inputs_.end(), kNullTerminal),
                inputs_.end());

  // Rebuild uses.
  for (int i = 0; i < num_inputs(); ++i) {
    bool inserted = owner_->uses_[this->input(i)].insert({this, i}).second;
    assert(inserted);
  }

  return erased;
}

bool Gate::HasNoUse() {
  for (int i = 0; i < num_outputs_; ++i) {
    auto use_it = owner_->uses_.find(output(i));
    if (use_it != owner_->uses_.end() && !use_it->second.empty()) {
      return false;
    }
  }
  return true;
}

bool Gate::operator<(const Gate& rhs) const {
  if (kind() != rhs.kind()) return kind() < rhs.kind();
  if (num_outputs_ != rhs.num_outputs_) return num_outputs_ < rhs.num_outputs_;
  if (lookup_data_ != rhs.lookup_data_) return lookup_data_ < rhs.lookup_data_;
  return inputs_ < rhs.inputs_;
}
