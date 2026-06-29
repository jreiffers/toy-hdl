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
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/linked_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_join.h"
#include "cpu/export.h"
#include "cpu/flags.h"
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

      if (gate.kind() == GateKind::kMux) {
        if (opts.lower_mux) {
          ScopeGuard guard(net, gate.scope());
          auto sel = gate.input(0);
          auto not_sel = gate.input(1);
          auto hi = gate.input(2);
          auto lo = gate.input(3);
          gate.ReplaceAllUsesWith(
              net.Nand({net.Nand({sel, hi}), net.Nand({not_sel, lo})}));
          changed = true;
        } else {
          if (gate.input(0) == kHighGate) {
            gate.ReplaceAllUsesWith(gate.input(2));
            changed = true;
          } else if (gate.input(0) == kLowGate) {
            gate.ReplaceAllUsesWith(gate.input(3));
            changed = true;
          }
          // More folding is possible, but this is all that's needed by tests
          // right now.
        }
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

  if (absl::GetFlag(FLAGS_dump_after_all)) {
    print_graphviz(net, "fold-gates");
  }

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

  if (absl::GetFlag(FLAGS_dump_after_all)) {
    print_graphviz(net, "cse-gates");
  }

  return ever_changed;
}

uint32_t ComposeLut2(uint32_t f_lut, uint32_t in_0_lut, uint32_t in_1_lut) {
  uint32_t ret = 0;
  for (uint32_t ab = 0; ab < 4; ++ab) {
    uint32_t val_0 = (in_0_lut >> ab) & 1;
    uint32_t val_1 = (in_1_lut >> ab) & 1;

    uint32_t out_val = (f_lut >> (val_0 + val_1 * 2)) & 1;
    if (out_val) {
      ret |= 1u << ab;
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
  absl::flat_hash_set<Gate*> removable_gates;
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
  absl::flat_hash_map<GateTerminal, uint32_t> funs;

  funs[in[0]] = 0b1010;
  if (in.size() == 2) {
    funs[in[1]] = 0b1100;
  }

  absl::flat_hash_set<Gate*> removable;
  bool walk_success = WalkCut(net, out, in, [&](Gate& gate) {
    // Gate created in this pass iteration - stop here.
    if (!pdt.IsKnown(gate.output())) {
      return false;
    }

    absl::InlinedVector<uint32_t, 4> in_funs;
    for (int i = 0; i < gate.num_inputs(); ++i) {
      in_funs.push_back(funs.at(gate.input(i)));
    }

    if (pdt.Check(out, gate.output())) {
      removable.insert(&gate);
    }
    switch (gate.kind()) {
      case GateKind::kMux: {
        funs[gate.output()] =
            (in_funs[0] & in_funs[2]) | (in_funs[1] & in_funs[3]);
        return true;
      }
      case GateKind::kNot:
      case GateKind::kNor: {
        uint32_t o = 0;
        for (uint32_t f : in_funs) o |= f;
        funs[gate.output()] = ~o;
        return true;
      }
      case GateKind::kNand: {
        uint32_t o = 0xFFFFFFFF;
        for (uint32_t f : in_funs) o &= f;
        funs[gate.output()] = ~o;
        return true;
      }
      case GateKind::kLookup: {
        // only 2-input (+ 2 negated inputs) lookups are implemented.
        assert(gate.num_inputs() == 4);
        funs[gate.output()] =
            ComposeLut2(gate.lookup_data(), in_funs[0], in_funs[1]);
        return true;
      }
      case GateKind::kDead:
      case GateKind::kTriStateBuffer:
        return false;
    }
  });

  if (!walk_success || removable.empty()) return std::nullopt;

  int score = 0;
  for (auto* gate : removable) {
    score += gate->GetCost();
  }

  return ScoredCut{out, in, score, funs[out] & 0xF, std::move(removable)};
}

std::optional<GateTerminal> FindNot(GateNetwork& net, GateTerminal t) {
  if (t.first && t.first->kind() == GateKind::kNot) {
    return t.first->input(0);
  }
  for (auto [user, _] : net.GetUsers(t)) {
    if (user->kind() == GateKind::kNot) {
      return user->output();
    }
  }
  return std::nullopt;
}

}  // namespace

bool MergeGates(GateNetwork& net) {
  // This pass finds sparsely connected subgraphs and replaces them with with
  // lookup gates.
  // Currently, this only handles 2-input LUTs.

  std::vector<GateTerminal> sources = net.all_inputs();
  std::vector<GateTerminal> sinks{net.sink()};
  graph::Sccs sccs(net);
  for (const auto& scc : sccs.sccs()) {
    for (auto source : scc.sources) {
      sources.push_back(source);
    }
    for (auto sink : scc.sinks) {
      sinks.push_back(sink);
    }
  }

  graph::TopoSort sort(net, sources, sinks);
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
    if (best->score <= 8) continue;

    int replacement_cost = 8;
    absl::InlinedVector<std::optional<GateTerminal>, 2> existing_nots;
    for (GateTerminal in : best->inputs) {
      if (auto existing_not = FindNot(net, in)) {
        if (best->removable_gates.erase(existing_not->first)) {
          best->score -= 2;
        }
        existing_nots.push_back(existing_not);
      } else {
        replacement_cost += 2;
        existing_nots.push_back(std::nullopt);
      }
    }

    if (best->score <= replacement_cost) continue;

    auto ins = best->inputs;
    for (int i = 0, e = ins.size(); i < e; ++i) {
      ins.push_back(existing_nots[i] ? *existing_nots[i] : net.Not(ins[i]));
    }

    // TODO: set the scope
    auto& repl = net.AddGate(GateKind::kLookup, ins);
    repl.set_lookup_data(best->function);
    out.first->ReplaceAllUsesWith(repl.output());

    for (Gate* g : best->removable_gates) {
      g->Erase();
    }

    changed = true;
  }

  if (absl::GetFlag(FLAGS_dump_after_all)) {
    print_graphviz(net, "merge-gates");
  }

  return changed;
}

namespace {

absl::InlinedVector<Gate*, 6> Intersect(const absl::flat_hash_set<Gate*>& a,
                                        const absl::flat_hash_set<Gate*>& b) {
  if (a.size() > b.size()) return Intersect(b, a);

  absl::InlinedVector<Gate*, 6> res;
  for (Gate* g : a)
    if (b.contains(g)) res.push_back(g);
  return res;
}

}  // namespace

bool FactorGates(GateNetwork& net) {
  using GatesWithInputs =
      absl::linked_hash_map<absl::flat_hash_set<GateTerminal>,
                            absl::flat_hash_set<Gate*>>;

  auto get_seed_groups = [&](GateKind kind) -> GatesWithInputs {
    GatesWithInputs result;
    absl::linked_hash_map<GateTerminal, int> counts;

    net.WalkUnordered([&](int, Gate& gate) {
      if (gate.kind() == kind) {
        for (int i = 0; i < gate.num_inputs(); ++i) {
          result[{gate.input(i)}].insert(&gate);
          ++counts[gate.input(i)];
        }
      }
    });

    // Clear singleton groups for inputs that are used only once; they are not
    // useful.
    for (auto& [terminal, count] : counts) {
      if (count == 1) {
        result.erase({terminal});
      }
    }
    return result;
  };

  auto max_benefit = [&](const auto& gates, const auto& inputs) {
    int k = gates.size();
    int n = inputs.size();
    return 2 * k * n - 2 * n - 4 * k;
  };

  auto benefit = [&](const auto& gates, int num_inputs) {
    // When we factor, we remove k inputs from each gate, add a new gate with k
    // inputs and add one and/or for each original input. The savings for group
    // size k with n members are at least:
    //        (n-1) * k * 2 - 6n = 2nk - 2k - 6n
    // For each gate whoser only user is a not, we save two extra transistors.
    int k = gates.size();
    int res = 2 * k * num_inputs - 2 * num_inputs - 6 * k;
    for (auto* gate : gates) {
      if (net.IsOnlyUsedByNot(gate->output())) res += 2;
    }
    return res;
  };

  auto run = [&](GateKind kind) {
    // Naive algorithm:
    // 1. Collect gates of kind 'kind'
    // 2. Iteratively build groups with k common inputs:
    //   a. start with groups with a single common input: {input} -> {gates}
    //   b. grow the groups by intersecting size k groups with size 1 groups
    // 3. Start with the largest group and greedily factor.
    //
    // If there's a possible optimization with the newly introduced nor, we'll
    // miss that.

    // group size-1 -> ({shared inputs} -> {gates})
    std::vector<GatesWithInputs> groups_by_size;
    groups_by_size.push_back(get_seed_groups(kind));

    bool success;
    do {
      success = false;
      auto& size_n1_groups = groups_by_size.emplace_back();
      auto& size_one_groups = groups_by_size.front();
      auto& size_n_groups = groups_by_size[groups_by_size.size() - 2];

      for (auto& [size_one_inputs, size_one_gates] : size_one_groups) {
        auto size_one_input = *size_one_inputs.begin();
        for (auto& [size_n_inputs, size_n_gates] : size_n_groups) {
          // Already merged?
          if (size_n_inputs.contains(size_one_input)) continue;

          auto intersection = Intersect(size_n_gates, size_one_gates);
          if (intersection.empty()) continue;

          auto size_n1_inputs = size_n_inputs;
          size_n1_inputs.insert(size_one_input);

          size_n1_groups[size_n1_inputs] = absl::flat_hash_set<Gate*>(
              intersection.begin(), intersection.end());

          success = true;
        }
      }
    } while (success);

    bool any_change = false;
    absl::flat_hash_set<Gate*> invalidated_gates;
    for (int size = groups_by_size.size(); size >= 2; --size) {
      for (const auto& [inputs, gates] : groups_by_size[size - 1]) {
        if (max_benefit(gates, inputs) <= 0) continue;

        absl::InlinedVector<Gate*, 6> valid_gates;
        for (auto* gate : gates) {
          if (!invalidated_gates.contains(gate)) valid_gates.push_back(gate);
        }

        if (benefit(valid_gates, inputs.size()) <= 0) continue;

        auto factored = net.AddGate(kind, absl::InlinedVector<GateTerminal, 2>(
                                              inputs.begin(), inputs.end()))
                            .output();

        for (auto gate : valid_gates) {
          for (auto input : inputs) {
            if (!gate->EraseInputs(input)) {
              throw std::logic_error("Expected input to be erase.");
            }
          }

          GateTerminal rep;
          if (kind == GateKind::kNor) {
            rep = net.And(factored, gate->output());
          } else {
            assert(kind == GateKind::kNand);
            rep = net.Or(factored, gate->output());
          }
          gate->ReplaceAllUsesWith(rep, rep.first->input(0).first);

          invalidated_gates.insert(gate);
        }

        any_change = true;
      }
    }
    return any_change;
  };

  bool nor = run(GateKind::kNor);
  bool nand = run(GateKind::kNand);

  if (absl::GetFlag(FLAGS_dump_after_all)) {
    print_graphviz(net, "factor-gates");
  }

  return nor || nand;
}

bool OptimizeCNF(GateNetwork& net) {
  // Not necessarily full CNFs; having a CNF-like subset is enough. The values
  // are the cnf-like subset.
  absl::linked_hash_map<Gate*, absl::InlinedVector<Gate*, 10>> cnfs;
  net.WalkUnordered([&](int, Gate& gate) {
    if (gate.kind() == GateKind::kNor) {
      absl::InlinedVector<Gate*, 10> clauses;
      for (int i = 0; i < gate.num_inputs(); ++i) {
        auto* in = gate.input(i).first;
        if (in &&
            (in->kind() == GateKind::kNor || in->kind() == GateKind::kNot)) {
          clauses.push_back(in);
        }
      }

      if (clauses.size() > 1) {
        cnfs[&gate] = std::move(clauses);
      }
    }
  });

  auto get_input = [](Gate* gate, int input) -> std::pair<GateTerminal, bool> {
    auto var = gate->input(input);
    if (var.first && var.first->kind() == GateKind::kNot) {
      return {var.first->input(0), true};
    }
    return {var, false};
  };

  bool ever_changed = false;
  for (auto& [cnf, clauses] : cnfs) {
    absl::flat_hash_map<GateTerminal, int> input_indices;
    absl::InlinedVector<GateTerminal, 10> inputs;

    for (auto* in : clauses) {
      for (int j = 0; j < in->num_inputs(); ++j) {
        auto [input, negated] = get_input(in, j);
        if (input_indices.try_emplace(input, input_indices.size()).second) {
          inputs.push_back(input);
        }
      }
    }

    if (input_indices.size() > 64) continue;

    // mask: 1 if used.
    // parity: 1 if negated.
    std::map<uint64_t, std::set<uint64_t>> parities_for_masks;

    for (auto* in : clauses) {
      uint64_t mask = 0;
      uint64_t parity = 0;
      for (int j = 0; j < in->num_inputs(); ++j) {
        auto v = get_input(in, j);
        int index = input_indices[v.first];

        // TODO: something should rewrite this to false.
        ABSL_CHECK(!(mask & (1ull << index)));

        mask |= 1ull << index;
        if (v.second) {
          parity |= 1ull << index;
        }
      }

      parities_for_masks[mask].insert(parity);
    }

    absl::InlinedVector<std::pair<uint64_t, uint64_t>, 6> to_erase;
    absl::InlinedVector<std::pair<uint64_t, uint64_t>, 3> to_add;
    bool changed = false;

    do {
      to_erase.clear();
      to_add.clear();

      // Look for pairs with the same mask and only one bit difference in
      // parity.
      for (auto& [mask, parities] : parities_for_masks) {
        if (parities.size() == 1) continue;

        absl::flat_hash_set<uint64_t> erased;
        for (uint64_t p1 : parities) {
          if (erased.contains(p1)) continue;
          for (uint64_t p2 : parities) {
            if (p2 <= p1) continue;
            if (erased.contains(p1)) break;
            if (erased.contains(p2)) continue;

            uint64_t diff = p1 ^ p2;
            if (__builtin_popcount(diff) == 1) {
              ABSL_CHECK(mask & diff);

              // This rewrite can actually increase transistor count, so one
              // might want to do something non-local here. I spent a few
              // minutes trying to come up with a heuristic, but the easy thing
              // didn't do anything useful (check if the clauses will still be
              // alive after the rewrite, take number of co-occurrences into
              // account).
              erased.insert(p1);
              erased.insert(p2);
              to_erase.emplace_back(mask, p1);
              to_erase.emplace_back(mask, p2);
              to_add.emplace_back(mask ^ diff, p1 & p2);
              changed = true;
              ever_changed = true;
            }
          }
        }
      }

      for (auto [mask, parity] : to_erase) {
        ABSL_CHECK(parities_for_masks[mask].erase(parity));
      }
      for (auto [mask, parity] : to_add) {
        ABSL_CHECK(parities_for_masks[mask].insert(parity).second);
      }
    } while (!to_erase.empty());

    if (changed) {
      absl::InlinedVector<GateTerminal, 10> negated_inputs;
      for (auto input : inputs) negated_inputs.push_back(net.Not(input));

      absl::InlinedVector<GateTerminal, 2> new_inputs;
      for (const auto& [mask, parities] : parities_for_masks) {
        for (uint64_t parity : parities) {
          absl::InlinedVector<GateTerminal, 2> inner_inputs;
          for (int i = 0; i < inputs.size(); ++i) {
            uint64_t bit = 1 << i;
            if (!(mask & bit)) continue;
            inner_inputs.push_back((parity & bit ? negated_inputs : inputs)[i]);
          }
          // TODO: Scope
          new_inputs.push_back(net.Nor(inner_inputs));
        }
      }

      ScopeGuard guard(net, cnf->scope());
      cnf->ReplaceAllUsesWith(net.Nor(new_inputs));
    }
  }

  if (absl::GetFlag(FLAGS_dump_after_all)) {
    print_graphviz(net, "optimize-cnf");
  }

  return ever_changed;
}

bool RunCanonicalizer(GateNetwork& net) {
  bool changed = false;
  net.WalkUnordered([&](int, Gate& gate) { changed |= gate.Canonicalize(); });
  return changed;
}

bool RunGateOptPipeline(GateNetwork& net, const FoldGatesOpts& opts) {
  bool changed;
  bool ever_changed = false;

  do {
    changed = false;

    bool sub_changed;
    // Sub-pipeline without folding (so CNFs stay CNFs). Run this before
    // FoldGates.
    do {
      sub_changed = false;
      // There's a nondet somewhere around here (:decoder_test).
      sub_changed |= OptimizeCNF(net);
      sub_changed |= FactorGates(net);
      sub_changed |= RunCanonicalizer(net);
      sub_changed |= CseGates(net);
      changed |= sub_changed;
    } while (sub_changed);

    changed |= CseGates(net);
    changed |= FoldGates(net, opts);
    changed |= MergeGates(net);

    ever_changed |= changed;
  } while (changed);

  return ever_changed;
}
