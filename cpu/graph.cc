#include "cpu/graph.h"

#include <deque>
#include <ranges>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace graph {

TopoSort::TopoSort(GateNetwork& net, absl::Span<const GateTerminal> sources,
                   absl::Span<const GateTerminal> sinks)
    : sources_(sources.begin(), sources.end()),
      sinks_(sinks.begin(), sinks.end()) {
  std::deque<GateTerminal> ready;
  absl::flat_hash_map<Gate*, int> processed_inputs;

  auto visit = [&](GateTerminal t) {
    order_.push_back(t);
    for (auto [user, _] : net.GetUsers(t)) {
      if (!sinks_.count(user->output()) &&
          ++processed_inputs[user] == user->num_inputs()) {
        ready.push_back(user->output());
      }
    }
  };

  for (auto source : sources) {
    visit(source);
  }

  while (!ready.empty()) {
    visit(ready.front());
    ready.pop_front();
  }

  for (auto sink : sinks_) {
    order_.push_back(sink);
  }
}

PostDominatorTree::PostDominatorTree(GateNetwork& net, const TopoSort& order) {
  auto lowest_common_ancestor = [&](GateTerminal a, GateTerminal b) {
    if (pdt_[a].depth > pdt_[b].depth) std::swap(a, b);
    while (pdt_[b].depth > pdt_[a].depth) b = pdt_[b].postdominator;

    while (a != b) {
      a = pdt_[a].postdominator;
      b = pdt_[b].postdominator;
    }
    return a;
  };

  for (auto t : order(/*include_sources=*/true, false) | std::views::reverse) {
    std::optional<GateTerminal> pdom = std::nullopt;
    for (auto [user, _] : net.GetUsers(t)) {
      pdom =
          pdom ? lowest_common_ancestor(user->output(), *pdom) : user->output();
    }
    assert(pdom);

    auto& pdom_v = pdt_[*pdom];
    pdt_[t] = {{}, *pdom, pdom_v.depth + 1};
    pdom_v.postdominates.push_back(t);
  }

  std::function<void(GateTerminal t, int& idx)> compute_idx;
  compute_idx = [&](GateTerminal t, int& idx) {
    auto& v = pdt_[t];
    v.idx = idx++;
    for (auto it : v.postdominates) {
      compute_idx(it, idx);
    }
    v.post_idx = idx;
  };

  int idx = 0;
  for (GateTerminal sink : order.sinks()) {
    compute_idx(sink, idx);
  }
}

void PostDominatorTree::dump() const {
  std::cout << "digraph d {\n";

  absl::flat_hash_map<GateTerminal, int> ids;

  for (auto& [t, dom] : pdt_) {
    std::string scope = "";
    if (t.first && !t.first->scope().empty()) {
      scope = absl::StrCat(" ", absl::StrJoin(t.first->scope(), "/"));
    }
    std::cout << "  n" << ids.size() << " [label=\"" << to_string(t) << scope
              << "\"];\n";
    ids[t] = ids.size();
  }

  for (auto& [t, dom] : pdt_) {
    for (auto p : dom.postdominates) {
      std::cout << "  n" << ids.at(t) << " -> n" << ids.at(p) << ";\n";
    }
  }

  std::cout << "}\n";
}

}  // namespace graph
