#ifndef GRAPH_H__
#define GRAPH_H__

#include <vector>

#include "absl/container/linked_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/types/span.h"
#include "cpu/gate_lib.h"

namespace graph {

// TODO tarjan for SCCs

class TopoSort {
 public:
  TopoSort(GateNetwork& net, absl::Span<const GateTerminal> sources,
           absl::Span<const GateTerminal> sinks);

  absl::linked_hash_set<GateTerminal> sources() const { return sources_; }
  absl::linked_hash_set<GateTerminal> sinks() const { return sinks_; }

  absl::Span<const GateTerminal> order(bool include_sources,
                                       bool include_sinks) const {
    auto r = absl::MakeSpan(order_);
    if (!include_sources) {
      r.remove_prefix(sources_.size());
    }
    if (!include_sinks) {
      r.remove_suffix(sinks_.size());
    }
    return r;
  }

  absl::Span<const GateTerminal> operator()(bool include_sources,
                                            bool include_sinks) const {
    return order(include_sources, include_sinks);
  }

  bool is_sink(GateTerminal t) { return sinks_.count(t); }
  bool is_source(GateTerminal t) { return sources_.count(t); }

 private:
  absl::linked_hash_set<GateTerminal> sources_;
  absl::linked_hash_set<GateTerminal> sinks_;
  std::vector<GateTerminal> order_;
};

class PostDominatorTree {
 public:
  PostDominatorTree(GateNetwork& net, const TopoSort& order);

  bool Check(GateTerminal dominator, GateTerminal subordinate) const {
    auto& d = pdt_.at(dominator);
    int s = pdt_.at(subordinate).idx;
    return d.idx <= s && s < d.post_idx;
  }

  bool CheckAll(GateTerminal dominator,
                absl::Span<const GateTerminal> sub) const {
    auto& d = pdt_.at(dominator);
    for (auto st : sub) {
      auto s = pdt_.at(st).idx;
      if (!(d.idx <= s && s < d.post_idx)) return false;
    }
    return true;
  }

  void dump() const;

 private:
  struct Dominance {
    absl::InlinedVector<GateTerminal, 4> postdominates;
    GateTerminal postdominator;
    int depth = 0;  // distance to sink

    int idx;
    int post_idx;
  };

  absl::node_hash_map<GateTerminal, Dominance> pdt_;
};

}  // namespace graph

#endif
