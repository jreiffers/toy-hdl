#ifndef LIB_H__
#define LIB_H__

#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "absl/types/span.h"

enum class TransistorType {
  kNChannel,
  kPChannel,
};

enum TransistorTerminal {
  kDrain,
  kGate,
  kSource,
};

struct TransistorId;

struct NodeId {
  int id;

  NodeId() : id(0) {}
  explicit constexpr NodeId(int id) : id(id) {}

  bool is_input() const { return id <= -4; }

  int input_index() const {
    assert(is_input());
    return -4 - id;
  }

  bool operator<(const NodeId& rhs) const { return id < rhs.id; }
  bool operator==(const NodeId& rhs) const { return id == rhs.id; }
  bool operator!=(const NodeId& rhs) const { return !(*this == rhs); }
};

template <>
struct std::hash<NodeId> {
  std::size_t operator()(const NodeId& id) const { return hash<int>()(id.id); }
};

constexpr NodeId kVdd(-1);
constexpr NodeId kVss(-2);
constexpr NodeId kClk(-3);

template <int bw>
struct reg {
  // LSB -> MSB
  std::array<NodeId, bw> node_ids;

  NodeId operator[](size_t index) const { return node_ids[index]; }

  NodeId& operator[](size_t index) { return node_ids[index]; }
};

struct dyn_reg {
  explicit dyn_reg(std::vector<NodeId> ids) : node_ids_(std::move(ids)) {}

  int bitwidth() const { return node_ids_.size(); }
  NodeId operator[](size_t index) const { return node_ids_[index]; }
  NodeId& operator[](size_t index) { return node_ids_[index]; }

 private:
  std::vector<NodeId> node_ids_;
};

struct TransistorId {
  int id;

  explicit TransistorId(int id) : id(id) {}
  explicit TransistorId(NodeId node) : id(node.id / 3) { assert(node.id >= 0); }

  NodeId gate() const { return NodeId(id * 3 + TransistorTerminal::kGate); }
  NodeId source() const { return NodeId(id * 3 + TransistorTerminal::kSource); }
  NodeId drain() const { return NodeId(id * 3 + TransistorTerminal::kDrain); }
};

struct Network {
 public:
  template <int bw>
  reg<bw> make_input() {
    reg<bw> result;
    for (int i = 0; i < bw; ++i) {
      result[i] = get_input_bit(input_offsets_.back() + i);
    }
    input_bitwidths_.push_back(bw);
    input_offsets_.push_back(input_offsets_.back() + bw);
    return result;
  }

  dyn_reg make_input(int bw) {
    input_bitwidths_.push_back(bw);
    input_offsets_.push_back(input_offsets_.back() + bw);
    return get_input(num_inputs() - 1);
  }

  NodeId get_input_bit(int id) const { return NodeId(-4 - id); }
  dyn_reg get_input(int id) const {
    std::vector<NodeId> ids;
    int bw = input_bitwidths_[id];
    ids.reserve(bw);
    for (int i = 0; i < bw; ++i) {
      ids.push_back(get_input_bit(input_offsets_[id] + i));
    }
    return dyn_reg(std::move(ids));
  }
  int num_input_bits() const { return input_offsets_.back(); }
  int num_inputs() const { return input_bitwidths_.size(); }
  int input_bitwidth(int index) const { return input_bitwidths_[index]; }
  absl::Span<const int> input_bitwidths() const { return input_bitwidths_; }

  int num_transistors() const { return transistors_.size(); }
  TransistorId make_transistor(TransistorType type);
  TransistorType transistor_type(TransistorId id) const {
    return transistors_[id.id];
  }

  void connect(NodeId a, NodeId b);
  void disconnect(NodeId a, NodeId b);

  // Returns the set of nodes connected to `node`. Returns an empty
  // set if the node is disconnected.
  const std::set<NodeId>& connected_nodes(NodeId node) const;

  // first < second.
  const std::set<std::pair<NodeId, NodeId>>& ordered_connections() const {
    return connections_;
  }

  void DeclareOutput(dyn_reg reg) { outputs_.push_back(std::move(reg)); }
  const std::vector<dyn_reg>& outputs() const { return outputs_; }
  std::vector<dyn_reg>& outputs() { return outputs_; }

 private:
  std::vector<TransistorType> transistors_;
  std::set<std::pair<NodeId, NodeId>> connections_;
  std::map<NodeId, std::set<NodeId>> bidi_connections;
  std::vector<int> input_bitwidths_;
  std::vector<int> input_offsets_ = {0};
  std::vector<dyn_reg> outputs_;
};

NodeId make_nand(Network& net, absl::Span<const NodeId> inputs);
NodeId make_nor(Network& net, absl::Span<const NodeId> inputs);
NodeId make_not(Network& net, NodeId input);
template <int bw>
reg<bw> make_not(Network& net, const reg<bw>& input) {
  reg<bw> output;
  for (int i = 0; i < bw; ++i) {
    output[i] = make_not(net, input[i]);
  }
  return output;
}

NodeId make_and(Network& net, absl::Span<const NodeId> inputs);
NodeId make_or(Network& net, absl::Span<const NodeId> inputs);
NodeId make_xor(Network& net, NodeId a, NodeId b);
NodeId make_lookup(Network& net, NodeId a, NodeId b, NodeId not_a, NodeId not_b,
                   uint32_t mask);

reg<2> make_half_adder(Network& net, NodeId a, NodeId b);
reg<2> make_full_adder(Network& net, NodeId a, NodeId b, NodeId c);

// Returns the transmission gate's drain.
NodeId make_tg(Network& net, NodeId sel, NodeId not_sel, NodeId a);
// Returns h if sel is high, l if sel is low.
NodeId make_mux(Network& net, NodeId sel, NodeId not_sel, NodeId l, NodeId h);
template <int bw>
reg<bw> make_mux(Network& net, NodeId sel, NodeId not_sel, const reg<bw>& l,
                 const reg<bw>& h) {
  reg<bw> out;
  for (int i = 0; i < bw; ++i) {
    out[i] = make_mux(net, sel, not_sel, l[i], h[i]);
  }
  return out;
}

template <int bw>
std::pair<reg<bw>, NodeId /* carry */> make_adder(Network& net,
                                                  const reg<bw>& lhs,
                                                  const reg<bw>& rhs,
                                                  NodeId carry = {kVss}) {
  reg<bw> result;
  for (int i = 0; i < bw; ++i) {
    auto partial = make_full_adder(net, lhs[i], rhs[i], carry).node_ids;
    result[i] = partial[0];
    carry = partial[1];
  }
  return {result, carry};
}

std::vector<NodeId> Flatten(absl::Span<const dyn_reg> regs);

#endif
