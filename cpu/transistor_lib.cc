#include "transistor_lib.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "absl/types/span.h"

TransistorId Network::make_transistor(TransistorType type) {
  transistors_.push_back(type);
  return TransistorId{static_cast<int>(transistors_.size()) - 1};
}

void Network::replace(NodeId from, NodeId to) {
  auto connections = bidi_connections[from];
  for (NodeId it : connections) {
    disconnect(from, it);
    connect(to, it);
  }
}

void Network::connect(NodeId a, NodeId b) {
  bidi_connections[a].insert(b);
  bidi_connections[b].insert(a);

  if (b < a) std::swap(a, b);
  connections_.insert({a, b});
}

void Network::disconnect(NodeId a, NodeId b) {
  bidi_connections[a].erase(b);
  bidi_connections[b].erase(a);

  if (b < a) std::swap(a, b);
  connections_.erase({a, b});
}

const std::set<NodeId>& Network::connected_nodes(NodeId node) const {
  static auto& kEmpty = *new std::set<NodeId>();
  auto it = bidi_connections.find(node);
  if (it == bidi_connections.end()) {
    return kEmpty;
  }
  return it->second;
}

NodeId make_nand(Network& net, absl::Span<const NodeId> inputs) {
  NodeId ret = kVss;
  for (NodeId i : inputs) {
    TransistorId n = net.make_transistor(TransistorType::kNChannel);
    net.connect(ret, n.source());
    net.connect(i, n.gate());
    ret = n.drain();
  }

  for (NodeId i : inputs) {
    TransistorId p = net.make_transistor(TransistorType::kPChannel);
    net.connect(i, p.gate());
    net.connect(kVdd, p.source());
    net.connect(ret, p.drain());
  }

  return ret;
}

NodeId make_nor(Network& net, absl::Span<const NodeId> inputs) {
  NodeId ret = kVdd;
  for (NodeId i : inputs) {
    TransistorId n = net.make_transistor(TransistorType::kPChannel);
    net.connect(ret, n.source());
    net.connect(i, n.gate());
    ret = n.drain();
  }

  for (NodeId i : inputs) {
    TransistorId p = net.make_transistor(TransistorType::kNChannel);
    net.connect(i, p.gate());
    net.connect(kVss, p.source());
    net.connect(ret, p.drain());
  }

  return ret;
}

NodeId make_not(Network& net, NodeId input) { return make_nand(net, {input}); }

NodeId make_and(Network& net, absl::Span<const NodeId> inputs) {
  return make_not(net, make_nand(net, inputs));
}

NodeId make_or(Network& net, absl::Span<const NodeId> inputs) {
  return make_not(net, make_nor(net, inputs));
}

NodeId make_xor(Network& net, NodeId a, NodeId b) {
  NodeId not_a = make_not(net, a);
  NodeId not_b = make_not(net, b);
  NodeId a_not_b = make_and(net, {not_a, b});
  NodeId b_not_a = make_and(net, {not_b, a});

  return make_or(net, {a_not_b, b_not_a});
}

NodeId make_lookup(Network& net, NodeId a, NodeId b, NodeId not_a, NodeId not_b,
                   uint32_t mask) {
  auto make_output = [&](int index) {
    bool one = (mask >> index) & 1;
    auto type = one ? TransistorType::kPChannel : TransistorType::kNChannel;

    TransistorId t1 = net.make_transistor(type);
    TransistorId t2 = net.make_transistor(type);

    net.connect(t1.source(), one ? kVdd : kVss);
    net.connect(t1.gate(), index & 1 ? a : not_a);
    net.connect(t2.source(), t1.drain());
    net.connect(t2.gate(), index & 2 ? b : not_b);
    return t2.drain();
  };

  auto o0 = make_output(0);
  auto o1 = make_output(1);
  auto o2 = make_output(2);
  auto o3 = make_output(3);

  net.connect(o0, o1);
  net.connect(o1, o2);
  net.connect(o2, o3);
  return o0;
}

NodeId make_tri_state_buffer(Network& net, NodeId enable, NodeId not_enable,
                             NodeId data) {
  NodeId data_nand_enable = make_nand(net, {data, enable});
  NodeId not_data_and_enable = make_nor(net, {data, not_enable});

  TransistorId p = net.make_transistor(TransistorType::kPChannel);
  TransistorId n = net.make_transistor(TransistorType::kNChannel);

  net.connect(p.source(), kVdd);
  net.connect(n.source(), kVss);
  net.connect(p.drain(), n.drain());
  net.connect(data_nand_enable, p.gate());
  net.connect(not_data_and_enable, n.gate());

  return n.drain();
}

NodeId make_tg(Network& net, NodeId sel, NodeId not_sel, NodeId a) {
  TransistorId p = net.make_transistor(TransistorType::kPChannel);
  TransistorId n = net.make_transistor(TransistorType::kNChannel);

  net.connect(n.gate(), sel);
  net.connect(p.gate(), not_sel);

  net.connect(n.source(), p.source());
  net.connect(n.drain(), p.drain());

  net.connect(n.source(), a);
  return n.drain();
}

NodeId make_mux(Network& net, NodeId sel, NodeId not_sel, NodeId h, NodeId l) {
  NodeId a = make_tg(net, sel, not_sel, h);
  NodeId b = make_tg(net, not_sel, sel, l);
  net.connect(a, b);
  return a;
}

std::vector<NodeId> Flatten(absl::Span<const dyn_reg> regs) {
  std::vector<NodeId> nodes;
  for (auto& reg : regs) {
    for (int b = 0; b < reg.bitwidth(); ++b) {
      nodes.push_back(reg[b]);
    }
  }
  return nodes;
}
