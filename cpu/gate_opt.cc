#include "cpu/gate_opt.h"

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/linked_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "cpu/graph.h"

namespace {

bool InputIs(Gate& gate, GateKind kind, int input_id = 0) {
  return gate.input(input_id).first &&
         gate.input(input_id).first->kind() == kind;
}

std::optional<GateTerminal> SkipNot(Gate& gate, int input_id = 0) {
  if (!InputIs(gate, GateKind::kNot, input_id)) {
    return std::nullopt;
  }
  return gate.input(input_id).first->input(0);
}

}  // namespace

bool FoldGates(GateNetwork& net, const FoldGatesOpts& opts) {
  bool changed;
  bool ever_changed = false;

  assert(net.current_scope().empty());

  do {
    changed = false;
    net.WalkUnordered([&](int, Gate& gate) {
      bool canonicalized = gate.Canonicalize();
      if (gate.kind() == GateKind::kNot) {
        if (auto in = SkipNot(gate); in) {
          gate.ReplaceAllUsesWith(*in);
          changed = true;
        } else if (gate.input(0) == kLowGate) {
          gate.ReplaceAllUsesWith(kHighGate);
          changed = true;
        } else if (gate.input(0) == kHighGate) {
          gate.ReplaceAllUsesWith(kLowGate);
          changed = true;
        }
      }

      if (opts.lower_mux && gate.kind() == GateKind::kMux) {
        ScopeGuard guard(net, gate.scope());
        auto sel = gate.input(0);
        auto not_sel = gate.input(1);
        auto hi = gate.input(2);
        auto lo = gate.input(3);
        gate.ReplaceAllUsesWith(
            net.Nand({net.Nand({sel, hi}), net.Nand({not_sel, lo})}));
        changed = true;
      }

      if (gate.num_inputs() == 0) {
        if (gate.kind() == GateKind::kNor) {
          gate.ReplaceAllUsesWith(kHighGate);
          changed = true;
        } else if (gate.kind() == GateKind::kNand) {
          gate.ReplaceAllUsesWith(kLowGate);
          changed = true;
        }
      }

      if (gate.kind() == GateKind::kTriStateBuffer) {
        if (gate.input(0) == kHighGate) {
          gate.ReplaceAllUsesWith(gate.input(2));
          changed = true;
        }
      }

      if (gate.num_inputs() == 2) {
        if (gate.kind() == GateKind::kNor) {
          if (auto a = SkipNot(gate, 0), b = SkipNot(gate, 1); a && b) {
            ScopeGuard guard(net, gate.scope());
            gate.ReplaceAllUsesWith(net.And(*a, *b));
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kLowGate)) {
            gate.EraseInputs(kLowGate);
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kHighGate)) {
            gate.ReplaceAllUsesWith(kLowGate);
            changed = true;
          }
        }

        if (gate.kind() == GateKind::kNand) {
          if (auto a = SkipNot(gate, 0), b = SkipNot(gate, 1); a && b) {
            ScopeGuard guard(net, gate.scope());
            gate.ReplaceAllUsesWith(net.Or(*a, *b));
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kLowGate)) {
            gate.ReplaceAllUsesWith(kHighGate);
            changed = true;
          } else if (absl::c_contains(gate.inputs(), kHighGate)) {
            gate.EraseInputs(kHighGate);
            changed = true;
          }
        }
      }

      if (gate.kind() == GateKind::kLookup && gate.num_inputs() == 2) {
        changed = true;
        if (gate.lookup_data() == 0) {
          gate.ReplaceAllUsesWith(kLowGate);
        } else if (gate.lookup_data() == 1) {
          gate.ReplaceAllUsesWith(gate.input(1));
        } else if (gate.lookup_data() == 2) {
          gate.ReplaceAllUsesWith(gate.input(0));
        } else {
          assert(gate.lookup_data() == 3);
          gate.ReplaceAllUsesWith(kHighGate);
        }
      }

      opts.callback();
      changed |= canonicalized;
    });

    ever_changed |= changed;
  } while (changed);

  return ever_changed;
}

bool CseGates(GateNetwork& net) {
  bool changed;
  bool ever_changed = false;
  do {
    std::map<Gate, GateTerminal> seen_gates;
    changed = false;
    net.WalkUnordered([&](int, Gate& gate) {
      auto [it, inserted] = seen_gates.try_emplace(gate, gate.output());
      if (!inserted) {
        changed = true;
        gate.ReplaceAllUsesWith(it->second);

        // Truncate the merged gate's scope to the common prefix.
        auto& repl = *it->second.first;
        int max_prefix_len = std::min(gate.scope().size(), repl.scope().size());
        repl.scope().erase(repl.scope().begin() + max_prefix_len,
                           repl.scope().end());

        auto a_it = gate.scope().begin();
        auto b_it = repl.scope().begin();
        for (int i = 0; i < max_prefix_len; ++i) {
          if (*a_it != *b_it) {
            repl.scope().erase(b_it, repl.scope().end());
            break;
          }
          ++a_it;
          ++b_it;
        }

        gate.Erase();
      }
    });
    ever_changed |= changed;
  } while (changed);

  return ever_changed;
}

uint32_t ComposeLut2(uint32_t f_lut, uint32_t in_0_lut, uint32_t in_1_lut) {
  uint32_t ret = 0;
  for (uint32_t ab = 0; ab < 4; ++ab) {
    uint32_t val_0 = (in_0_lut >> ab) & 1;
    uint32_t val_1 = (in_1_lut >> ab) & 1;

    uint32_t out_val = (f_lut >> (val_0 + val_1 * 2)) & 1;
    if (out_val) {
      ret |= ab;
    }
  }
  return ret;
}

namespace {

using Cut = absl::InlinedVector<GateTerminal, 2>;

// For each signal, all possible ways to cut its dominators so there are at
// most two signals on the boundary.
absl::linked_hash_map<GateTerminal, std::vector<Cut>> GetCutCandidates(
    const graph::TopoSort& sort) {
  absl::linked_hash_map<GateTerminal, std::vector<Cut>> candidates;
  for (GateTerminal s : sort.sources()) {
    candidates[s] = {{s}};
  }

  for (GateTerminal out :
       sort.order(/*include_sources=*/false, /*include_sinks=*/false)) {
    auto& cuts = candidates[out];
    auto& gate = *out.first;

    if (gate.num_inputs() <= 2) {
      cuts.push_back({out});
    }

    std::function<void(int, Cut)> collect;
    collect = [&](int input_index, Cut acc) {
      if (input_index == gate.num_inputs()) {
        cuts.push_back(acc);
        return;
      }

      for (auto cut : candidates[gate.input(input_index)]) {
        for (GateTerminal c : acc) {
          if (cut.size() == 0 || (cut.size() == 1 && cut[0] != c) ||
              (cut.size() == 2 && cut[0] != c && cut[1] != c)) {
            cut.push_back(c);
          }
        }
        if (cut.size() <= 2) {
          collect(input_index + 1, cut);
        }
      }
    };
    collect(0, {});
  }

  return candidates;
}

struct ScoredCut {
  GateTerminal output;
  absl::InlinedVector<GateTerminal, 2> inputs;
  int score;
  uint32_t function;
};

template <typename V>
bool WalkCut(const GateNetwork& net, GateTerminal out, Cut cut, V&& visit) {
  std::deque<GateTerminal> worklist(cut.begin(), cut.end());
  absl::flat_hash_map<GateTerminal, uint32_t> seen_inputs;

  while (!worklist.empty()) {
    auto next = worklist.front();
    worklist.pop_front();
    if (next == out) {
      continue;
    }

    for (auto [user, _] : net.GetUsers(next)) {
      if (++seen_inputs[user->output()] == user->num_inputs()) {
        if (!visit(*user)) return false;
        worklist.push_back(user->output());
      }
    }
  }

  return true;
}

std::optional<ScoredCut> ScoreCut(const GateNetwork& net,
                                  const graph::PostDominatorTree& pdt,
                                  GateTerminal out, Cut in) {
  if (!pdt.CheckAll(out, in)) {
    return std::nullopt;
  }

  absl::flat_hash_map<GateTerminal, uint32_t> funs;

  funs[in[0]] = 0b1010;
  if (in.size() == 2) {
    funs[in[1]] = 0b1100;
  }

  int score = 0;
  bool walk_success = WalkCut(net, out, in, [&](Gate& gate) {
    absl::InlinedVector<uint32_t, 4> in_funs;
    for (int i = 0; i < gate.num_inputs(); ++i) {
      in_funs.push_back(funs.at(gate.input(i)));
    }
    switch (gate.kind()) {
      case GateKind::kMux: {
        funs[gate.output()] =
            (in_funs[0] & in_funs[2]) | (in_funs[1] & in_funs[3]);
        score += 4;
        return true;
      }
      case GateKind::kNot:
      case GateKind::kNor: {
        uint32_t o = 0;
        for (uint32_t f : in_funs) o |= f;
        funs[gate.output()] = ~o;
        score += 2 * gate.num_inputs();
        return true;
      }
      case GateKind::kNand: {
        uint32_t o = 0xFFFFFFFF;
        for (uint32_t f : in_funs) o &= f;
        funs[gate.output()] = ~o;
        score += 2 * gate.num_inputs();
        return true;
      }
      case GateKind::kLookup: {
        // only 2-input (+ 2 negated inputs) lookups are implemented.
        assert(gate.num_inputs() == 4);
        funs[gate.output()] =
            ComposeLut2(gate.lookup_data(), in_funs[0], in_funs[1]);
        score += 8;
        return true;
      }
      case GateKind::kDead:
      case GateKind::kTriStateBuffer:
        return false;
    }

  });

  if (!walk_success || score == 0) return std::nullopt;

  return ScoredCut{out, in, score, funs[out] & 0xF};
}

}  // namespace

bool MergeGates(GateNetwork& net) {
  // This pass finds sparsely connected subgraphs and replaces them with with
  // lookup gates.
  // Currently, this only handles 2-input LUTs.

  // TODO: handle flip-flops

  auto sources = net.all_inputs();
  graph::TopoSort sort(net, sources, {net.sink()});
  graph::PostDominatorTree pdt(net, sort);

  const auto& candidates = GetCutCandidates(sort);

  bool changed = false;
  for (auto [out, cuts] : candidates) {
    if (!out.first) continue;

    std::optional<ScoredCut> best = std::nullopt;
    for (auto cut : cuts) {
      if (auto scored = ScoreCut(net, pdt, out, cut)) {
        if (!best || scored->score > best->score) {
          best = std::move(scored);
        }
      }
    }

    if (!best) continue;

    // 12 transistors is the worst case with dedicated NOTs on both inputs.
    // In practice, most LUTs can be lowered to fewer than 12 transistors.
    // TODO: Fix this.
    //
    // TODO: handle better cuts first. The final result should be the same, but
    // that might need fewer iterations.
    if (best->score <= 12) continue;

    absl::InlinedVector<Gate*, 6> to_erase;
    WalkCut(net, out, best->inputs, [&](Gate& g) {
      to_erase.push_back(&g);
      return true;
    });

    auto ins = best->inputs;
    for (int i = 0, e = ins.size(); i < e; ++i) {
      ins.push_back(net.Not(ins[i]));
    }
    auto& repl = net.AddGate(GateKind::kLookup, ins);
    repl.set_lookup_data(best->function);
    out.first->ReplaceAllUsesWith(repl.output());

    for (Gate* g : to_erase) {
      g->Erase();
    }

    changed = true;
  }

  return changed;
}
