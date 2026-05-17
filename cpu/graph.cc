#include "cpu/graph.h"

#include <deque>
#include <ranges>
#include <stack>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace graph {

Sccs::Sccs(GateNetwork& net) {
  struct NodeInfo {
    int index = -1;
    int lowlink = -1;
    bool on_stack = false;
  };

  absl::node_hash_map<GateTerminal, NodeInfo> nodes;

  int index = 0;

  std::stack<GateTerminal> stack;
  std::function<void(GateTerminal t, NodeInfo & info)> visit;
  visit = [&](GateTerminal t, NodeInfo& info) {
    info.lowlink = info.index = index++;
    stack.push(t);
    info.on_stack = true;

    for (auto [user, _] : net.GetUsers(t)) {
      auto out = user->output();
      assert(out != t);
      auto& user_info = nodes[out];
      if (user_info.index == -1) {
        visit(out, user_info);
        info.lowlink = std::min(info.lowlink, user_info.lowlink);
      } else if (user_info.on_stack) {
        info.lowlink = std::min(info.lowlink, user_info.index);
      }
    }

    if (info.lowlink == info.index) {
      GateTerminal w;
      auto& scc = sccs_.emplace_back();
      do {
        w = stack.top();
        auto& w_info = nodes[w];
        w_info.on_stack = false;
        scc.members.insert(w);
        stack.pop();
      } while (w != t);

      if (scc.members.size() == 1) {
        sccs_.pop_back();
      } else {
        for (auto node : scc.members) {
          auto& gate = *node.first;

          bool all_operands_inside = true;
          bool all_users_inside = true;

          for (int i = 0; all_operands_inside && i < gate.num_inputs(); ++i) {
            all_operands_inside &= scc.members.count(gate.input(i));
          }

          for (auto [user, _] : net.GetUsers(gate.output())) {
            if (!(all_users_inside &= scc.members.count(user->output()))) break;
          }

          if (!all_operands_inside) scc.sinks.insert(node);
          if (!all_users_inside) scc.sources.insert(node);
        }
      }
    }
  };

  for (auto input : net.all_inputs()) {
    visit(input, nodes[input]);
  }

  net.WalkUnordered([&](int, Gate& gate) {
    auto& info = nodes[gate.output()];
    if (info.index == -1) {
      visit(gate.output(), info);
    }
  });
}

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
