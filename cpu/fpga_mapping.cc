#include "cpu/fpga_mapping.h"

#include <deque>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "cpu/graph.h"

FpgaResource GetResource(GateTerminal terminal) {
  if (!terminal.first) {
    return FpgaResource::kIn;
  }

  switch (terminal.first->kind()) {
    case GateKind::kTriStateBuffer:
      return FpgaResource::kOut;
    case GateKind::kNand:
      return FpgaResource::kNand;
    case GateKind::kNor:
      return FpgaResource::kNor;
    case GateKind::kLookup:
      return FpgaResource::kLut2;
    default:
      CHECK(false);
  }
}

FpgaChipConfig::FpgaChipConfig(const FpgaSpec& spec) {
  for (auto& row : spec.resources) {
    nodes_.emplace_back();
    for (auto resource : row) {
      nodes_.back().emplace_back(spec.arity(resource), /*ourtput_arity=*/2);
    }
  }
}

struct PinCoords {
  Coords node_id;
  int output_id;
};

std::optional<std::pair<int, LocalLaneId>> ChipBuilder::Route(
    GateTerminal source, Coords target) {
  std::set<GlobalLaneId> seen = signals_[source];
  std::map<GlobalLaneId, std::function<std::optional<GlobalLaneId>()>>
      predecessors;
  std::deque<GlobalLaneId> queue{seen.begin(), seen.end()};

  std::optional<GlobalLaneId> result_lane = std::nullopt;
  for (auto loc : seen) {
    if (loc.node == target) {
      return {{0, loc.lane}};
    }
  }

  auto try_enqueue = [&](GlobalLaneId dst,
                         std::function<std::optional<GlobalLaneId>()> link) {
    if (used_lanes_.contains(dst)) return;
    if (!seen.insert(dst).second) return;
    if (dst.node == target) result_lane = dst;
    predecessors[dst] = std::move(link);
    queue.push_back(dst);
  };

  auto terminal_loc = terminals_[source];
  // If the signal isn't yet connected to anything, add the available lanes as
  // candidates.
  if (seen.empty()) {
    for (int i = 0; i < spec_->bus_width; ++i) {
      for (auto d : {BusOrientation::kHorizontal, BusOrientation::kVertical}) {
        GlobalLaneId lane{terminal_loc.first, {d, i}};
        try_enqueue(
            lane, [this, i, d, terminal_loc]() -> std::optional<GlobalLaneId> {
              config_[terminal_loc.first].outputs()[terminal_loc.second] = {d,
                                                                            i};
              return std::nullopt;
            });
      }
    }
  }

  while (!queue.empty() && !result_lane) {
    GlobalLaneId current = queue.front();
    queue.pop_front();

    Coords node_id = current.node;
    LocalLaneId lane = current.lane;

    auto& node = config_[node_id];

    std::optional<LocalLaneId> linked = node.linked_lane(lane);
    if (!linked) {
      for (int i = 0; i < spec_->bus_width; ++i) {
        try_enqueue({node_id, {!lane.orientation, i}},
                    [&node, lane, i, current]() -> std::optional<GlobalLaneId> {
                      node.link_lane(lane, i);
                      return current;
                    });
      }
    }

    for (int d : {0, 1}) {
      bool h = lane.orientation == BusOrientation::kHorizontal;
      int drow = h ? 0 : d;
      int dcol = h ? d : 0;

      // Coordinates of node whose bus we end up on.
      int r2 = node_id.first + (h ? 0 : drow * 2 - 1);
      int c2 = node_id.second + (h ? dcol * 2 - 1 : 0);

      if (r2 >= 0 && c2 >= 0 && r2 < spec_->rows() && c2 < spec_->cols()) {
        // Coordinates of node that owns the passthrough bit.
        int r = node_id.first + drow;
        int c = node_id.second + dcol;

        // The node that owns the configuration bit.
        auto& config_node = config_[{r, c}];
        GlobalLaneId to = {{r2, c2}, lane};
        if (!config_node.bus_passthrough().contains(lane)) {
          try_enqueue(
              to,
              [&config_node, lane, current]() -> std::optional<GlobalLaneId> {
                config_node.bus_passthrough().insert(lane);
                return current;
              });
        }
      }
    }
  }

  if (!result_lane) return std::nullopt;

  std::optional<GlobalLaneId> current = result_lane;
  auto& lanes_for_signal = signals_[source];
  int cost = 0;
  while (current && lanes_for_signal.insert(*current).second) {
    used_lanes_.insert(*current);
    current = predecessors.at(*current)();
    ++cost;
  }
  return {{cost, result_lane->lane}};
}

std::optional<GateTerminal> FindComplement(GateNetwork& net,
                                           GateTerminal terminal) {
  // TODO ffs have their complement. Not modeled properly.
  for (auto [user, _] : net.GetUsers(terminal)) {
    if (user && user->kind() == GateKind::kNot) return user->output();
  }
  return std::nullopt;
}

bool ChipBuilder::AddInput(GateNetwork& net, GateTerminal input,
                           int& use_count) {
  if (!input_use_count_.contains(input) && terminals_.contains(input)) {
    // Not an input.
    use_count = 0;
    return true;
  }

  if (input.first && input.first->kind() == GateKind::kNot) {
    use_count = ++input_use_count_[input.first->input(0)];
  } else {
    use_count = ++input_use_count_[input];
  }

  if (use_count > 1) {
    CHECK(terminals_.contains(input));
    return true;
  }

  auto& available = available_[FpgaResource::kIn];
  // Modifying input use count before this is OK, returning false
  // makes the builder invalid and it won't be used anymore.
  if (available.empty()) return false;

  auto input_loc = available.extract(available.begin()).value();

  if (input.first && input.first->kind() == GateKind::kNot) {
    CHECK(!terminals_.contains(input.first->input(0)));

    terminals_[input.first->input(0)] = {input_loc, 0};
    terminals_[input] = {input_loc, 1};
  } else {
    terminals_[input] = {input_loc, 0};
    // If we have the complement, set it up too.
    if (auto nt = FindComplement(net, input)) {
      terminals_[*nt] = {input_loc, 1};
    }
  }

  return true;
}

std::optional<std::pair<int, ChipBuilder>> ChipBuilder::TryPlace(
    GateNetwork& net, Gate& gate) const {
  CHECK(!has(gate.output()));

  const auto& candidate_locs = available_.at(GetResource(gate.output()));
  if (candidate_locs.empty()) return std::nullopt;

  auto builder = *this;

  // TODO: could try all the possible locations and pick the cheapest one.
  auto gate_loc = *candidate_locs.begin();
  builder.available_[GetResource(gate.output())].erase(gate_loc);
  auto& gate_node = builder.config_[gate_loc];
  builder.terminals_[gate.output()] = {gate_loc, 0};
  if (auto nt = FindComplement(net, gate.output())) {
    builder.terminals_[*nt] = {gate_loc, 1};
  }

  // Each input is either already available, or we need an external input for
  // it.
  // TODO this will break with flipflops?

  int total_cost = 0;

  // TODO fix LUT2 and tri-state-buffer.
  for (int i = 0; i < gate.num_inputs(); ++i) {
    auto in = gate.input(i);
    int use_count = 0;
    if (!builder.AddInput(net, in, use_count)) return std::nullopt;

    auto cost_and_lane = builder.Route(gate.input(i), gate_loc);
    if (!cost_and_lane) return std::nullopt;

    gate_node.inputs()[i] = cost_and_lane->second;

    total_cost += cost_and_lane->first;

    if (use_count > 0) {
      // Charge each use, but make subsequent uses cheaper. This
      // might encourage the swap logic to cluster uses (i.e. if we
      // have two chips that use an input 3 and 2 times respectively,
      // the cost of that is larger than 4 + 1).
      total_cost += 20 - use_count;  // arbitrary
    }
  }

  // Maybe a bonus for sharing uses with gates already on the chip?

  return {{total_cost, builder}};
}

std::string ChipBuilder::to_ascii() const {
  int bw = spec_->bus_width;
  int g = bw + 2;
  std::vector<std::string> rows(spec_->rows() * g,
                                std::string(spec_->cols() * g, ' '));

  for (int r = 0; r < spec_->rows(); ++r) {
    for (int c = 0; c < spec_->cols(); ++c) {
      const auto& node = config_[{r, c}];

      int r0 = r * g;
      int c0 = c * g;

      for (int i = 0; i < bw; ++i) {
        for (int j = 0; j < bw; ++j) {
          rows[r0 + 2 + i][c0 + 2 + j] = '.';
        }
      }

      for (auto [c, r] : node.hv_links()) {
        rows[r0 + 2 + r][c0 + 2 + c] = '+';
      }

      for (auto p : node.bus_passthrough()) {
        if (p.orientation == BusOrientation::kHorizontal) {
          rows[r0 + 2 + p.lane][c0] = '-';
          rows[r0 + 2 + p.lane][c0 + 1] = '-';
        } else {
          rows[r0][c0 + 2 + p.lane] = '|';
          rows[r0 + 1][c0 + 2 + p.lane] = '|';
        }
      }

      for (int i = 0; i < node.inputs().size(); ++i) {
        auto lane = node.inputs()[i];
        if (lane) {
          if (lane->orientation == BusOrientation::kHorizontal) {
            rows[r0 + 2 + lane->lane][c0 + 1] = ('a' + i);
          } else {
            rows[r0 + 1][c0 + 2 + lane->lane] = ('a' + i);
          }
        }
      }

      for (int i = 0; i < node.outputs().size(); ++i) {
        auto lane = node.outputs()[i];
        if (lane) {
          if (lane->orientation == BusOrientation::kHorizontal) {
            rows[r0 + 2 + lane->lane][c0 + 1] = ('A' + i);
          } else {
            rows[r0 + 1][c0 + 2 + lane->lane] = ('A' + i);
          }
        }
      }
    }
  }

  for (auto& s : rows) {
    s = "      " + s;
    auto pos = s.find_last_not_of(" ");
    if (pos == std::string::npos) {
      s.clear();
    } else {
      s.resize(pos + 1);
    }
  }

  return absl::StrJoin(rows, "\n");
}

std::string ChipBuilder::summary() const {
  std::vector<std::string> parts;
  for (auto res : AllFpgaResources()) {
    parts.push_back(absl::StrCat(to_string(res), ": ", num_used(res), "/",
                                 spec_->capacity(res)));
  }
  return absl::StrJoin(parts, ", ");
}

FpgaMapping FpgaMapping::Map(const FpgaSpec& spec, GateNetwork& net) {
  std::map<GlobalLaneId, GateTerminal> signals;

  graph::TopoSort sort(net, net.all_inputs(), {net.sink()});
  std::map<GateTerminal, int> topo_index;

  // Iterate over gates so we preferentially visit gates whose neighbors are
  // already visited. If there are multiple with the same score, take the one
  // with the most unvisited neighbors. This is not optimized at all.

  absl::flat_hash_map<GateTerminal, std::pair<int, int>> visited_and_unvisited;
  std::map<std::pair<int, int>, std::set<GateTerminal>> nodes_to_visit;

  auto inputs = [&](Gate& gate) -> absl::flat_hash_set<GateTerminal> {
    absl::flat_hash_set<GateTerminal> ret;
    for (int i = 0; i < gate.num_inputs(); ++i) {
      GateTerminal in = gate.input(i);
      if (in.first && in.first->kind() == GateKind::kNot) {
        in = in.first->input(0);
      }
      if (in.first) ret.insert(in);
    }
    return ret;
  };

  auto users = [&](GateTerminal terminal) -> absl::flat_hash_set<GateTerminal> {
    absl::flat_hash_set<GateTerminal> ret;
    for (auto [user, _] : net.GetUsers(terminal)) {
      if (!user) continue;

      if (user->kind() == GateKind::kNot) {
        for (auto [not_user, _] : net.GetUsers(user->output())) {
          if (not_user && not_user->kind() != GateKind::kDead)
            ret.insert(not_user->output());
        }
      } else if (user->kind() != GateKind::kDead) {
        ret.insert(user->output());
      }
    }
    return ret;
  };

  std::cerr << "building topo index and maps.\n";
  for (auto terminal : sort.order(false, false)) {
    CHECK(terminal.first);
    auto& gate = *terminal.first;

    // We don't visit NOT gates.
    if (gate.kind() == GateKind::kNot) continue;

    int index = topo_index.size();
    topo_index[terminal] = index;

    std::pair<int, int> key{0, -inputs(gate).size() - users(terminal).size()};
    visited_and_unvisited[terminal] = key;
    nodes_to_visit[key].insert(terminal);
  }

  auto increment_visited = [&](GateTerminal t) {
    auto it = visited_and_unvisited.find(t);
    // Already placed?
    if (it == visited_and_unvisited.end()) return;
    std::pair<int, int> current_key = it->second;
    std::pair<int, int> new_key{current_key.first - 1, current_key.second};

    CHECK(nodes_to_visit[current_key].erase(t));
    CHECK(nodes_to_visit[new_key].insert(t).second);
    if (nodes_to_visit[current_key].empty()) {
      nodes_to_visit.erase(current_key);
    }
    visited_and_unvisited[t] = new_key;
  };

  auto extract_next = [&]() {
    CHECK(!nodes_to_visit.empty());
    auto it = nodes_to_visit.begin();
    auto& terminals = it->second;

    CHECK(!terminals.empty());
    auto terminal = terminals.extract(terminals.begin()).value();
    if (terminals.empty()) {
      nodes_to_visit.erase(it);
    }
    visited_and_unvisited.erase(terminal);

    return terminal;
  };

  struct ChipPlan {
    int current_cost;
    std::vector<GateTerminal> gates;
  };

  auto try_build = [&](const std::vector<GateTerminal>& gates)
      -> std::optional<std::pair<int, ChipBuilder>> {
    int sum_cost = 0;
    ChipBuilder b(spec);

    for (auto gate : gates) {
      auto cost_and_new_b = b.TryPlace(net, *gate.first);
      if (!cost_and_new_b) return std::nullopt;
      b = cost_and_new_b->second;
      sum_cost += cost_and_new_b->first;

      bool has_outside_user =
          absl::c_any_of(users(gate), [&](GateTerminal user) {
            return !absl::c_linear_search(gates, user);
          });

      if (has_outside_user) {
        sum_cost += 40;
      }
    }

    // Charge each used gate, but charge each one less, except for
    // inputs.
    for (auto res : AllFpgaResources()) {
      int used = b.num_used(res);
      if (res == FpgaResource::kIn) {
        // sum_cost += 5 * used * used;
      } else {
        sum_cost += 5 * used * (10 - used);
      }
    }

    return {{sum_cost, b}};
  };

  auto check_plan =
      [&](std::vector<GateTerminal> gates) -> std::optional<ChipPlan> {
    std::sort(gates.begin(), gates.end(),
              [&](GateTerminal lhs, GateTerminal rhs) {
                return topo_index.at(lhs) < topo_index.at(rhs);
              });

    auto cost_and_b = try_build(gates);
    if (!cost_and_b) return std::nullopt;

    return {{cost_and_b->first, std::move(gates)}};
  };

  auto add_gate = [&](const ChipPlan& plan,
                      GateTerminal gate) -> std::optional<ChipPlan> {
    std::vector<GateTerminal> new_gates = plan.gates;
    new_gates.push_back(gate);
    return check_plan(std::move(new_gates));
  };

  auto replace_gate =
      [&](const ChipPlan& plan, GateTerminal to_remove,
          std::optional<GateTerminal> to_add) -> std::optional<ChipPlan> {
    auto new_gates = plan.gates;
    auto it = std::find(new_gates.begin(), new_gates.end(), to_remove);
    CHECK(it != new_gates.end());
    if (to_add) {
      *it = *to_add;
    } else {
      new_gates.erase(it);
    }
    return check_plan(std::move(new_gates));
  };

  ChipPlan empty{0, {}};
  std::vector<ChipPlan> chips;

  int num_placed = 0;
  while (!nodes_to_visit.empty()) {
    auto terminal = extract_next();
    std::cerr << "processing gate " << to_string(terminal) << "\n";
    auto* gate = terminal.first;

    // Find the chip that can accomodate this gate with the lowest cost. If none
    // exists, allocate a new one.
    int best_chip_index = 0;
    int best_incremental_cost = 0;
    std::optional<ChipPlan> best_plan;

    for (int chip = 0; chip < chips.size(); ++chip) {
      std::optional<ChipPlan> new_plan = add_gate(chips[chip], terminal);
      if (!new_plan) continue;

      int incremental_cost = new_plan->current_cost - chips[chip].current_cost;
      if (!best_plan || best_incremental_cost > incremental_cost) {
        best_plan = new_plan;
        best_incremental_cost = incremental_cost;
        best_chip_index = chip;
      }
    }

    if (best_plan) {
      std::cerr << "  fit gate on chip " << best_chip_index << "\n";
      chips[best_chip_index] = *best_plan;
    } else {
      std::cerr << "  no chip found - starting a new one\n";
      auto new_plan = add_gate(empty, terminal);
      CHECK(new_plan) << "Failed to place the first gate?";
      chips.push_back(*new_plan);
    }

    std::cerr << "  updating visit counters\n";

    for (GateTerminal in : inputs(*gate)) {
      std::cerr << "  input " << to_string(in) << "\n";
      increment_visited(in);
    }
    for (GateTerminal out : users(gate->output())) {
      std::cerr << "  output " << to_string(out) << "\n";
      increment_visited(out);
    }

    ++num_placed;
  }

  std::cerr << "Costs:\n";
  int sum = 0;
  for (auto& plan : chips) {
    std::cerr << "  " << plan.current_cost << "\n";
    sum += plan.current_cost;
  }
  std::cerr << "total: " << sum << "\n";

  // The placement algorithm is terrible. Try to swap gates and see if that
  // gives us a better placement.
  bool any_swap;
  do {
    any_swap = false;

    for (auto& plan : chips) {
      for (GateTerminal gate : plan.gates) {
        bool swap_here = false;
        for (auto [user, _] : users(gate)) {
          if (!user) continue;
          if (absl::c_contains(plan.gates, user->output())) continue;

          ChipPlan* user_owner = nullptr;
          auto user_owner_it =
              std::find_if(chips.begin(), chips.end(), [&](ChipPlan& plan) {
                return absl::c_contains(plan.gates, user->output());
              });
          CHECK(user_owner_it != chips.end())
              << "Gate " << to_string(user->output())
              << " not owned by anyone?";

          // check if we can just take the user.
          int current_cost = plan.current_cost + user_owner_it->current_cost;

          auto take_plan = add_gate(plan, user->output());
          if (take_plan) {
            auto other_plan =
                replace_gate(*user_owner_it, user->output(), std::nullopt);
            if (other_plan) {
              int new_cost = take_plan->current_cost + other_plan->current_cost;

              if (new_cost < current_cost) {
                plan = *take_plan;
                *user_owner_it = *other_plan;
                std::cerr << "steal: " << current_cost << " -> " << new_cost
                          << "\n";
                any_swap = true;
                goto next_plan;
              } else {
                std::cerr << "steal failed: " << current_cost << " -> "
                          << new_cost << "\n";
              }
            }
          }

          // otherwise, try a swap
          for (auto to_replace : plan.gates) {
            if (gate.first->kind() != user->kind()) continue;

            auto repl_plan = replace_gate(plan, to_replace, user->output());
            if (!repl_plan) continue;

            auto repl_other_plan =
                replace_gate(*user_owner_it, user->output(), to_replace);

            if (!repl_other_plan) continue;

            int new_cost =
                repl_plan->current_cost + repl_other_plan->current_cost;

            if (new_cost < current_cost) {
              plan = *repl_plan;
              *user_owner_it = *repl_other_plan;
              std::cerr << "swap: " << current_cost << " -> " << new_cost
                        << "\n";
              any_swap = true;
              goto next_plan;
            }
            std::cerr << "swap failed: " << current_cost << " -> " << new_cost
                      << "\n";
          }
        }
      }
    next_plan:
    }
  } while (any_swap);

  std::cerr << "Costs:\n";
  sum = 0;
  for (auto& plan : chips) {
    std::cerr << "  " << plan.current_cost << "\n";
    sum += plan.current_cost;
  }
  std::cerr << "total: " << sum << "\n";

  FpgaMapping mapping;
  for (const auto& plan : chips) {
    auto cost_and_b = try_build(plan.gates);
    CHECK(cost_and_b);
    mapping.chips.push_back(cost_and_b->second);
  }

  std::cerr << "using " << chips.size() << " chips for " << num_placed
            << " gates.\n";

  return mapping;
}
