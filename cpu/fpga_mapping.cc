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

FpgaChipConfig::FpgaChipConfig(const FpgaSpec& spec) : spec_(&spec) {
  for (auto& row : spec.resources) {
    nodes_.emplace_back();
    for (auto resource : row) {
      nodes_.back().emplace_back(spec.arity(resource), /*ourtput_arity=*/2);
    }
  }
}

std::optional<int> ChipBuilder::Route(GateTerminal source, Coords target) {
  std::set<GlobalLaneId> seen = signals_[source];
  std::map<GlobalLaneId, std::function<std::optional<GlobalLaneId>()>>
      predecessors;
  std::deque<GlobalLaneId> queue{seen.begin(), seen.end()};

  std::optional<GlobalLaneId> result_lane = std::nullopt;
  for (auto loc : seen) {
    if (loc.node == target) {
      return 0;
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
  return cost;
}

std::optional<GateTerminal> FindComplement(GateNetwork& net,
                                           GateTerminal terminal) {
  // TODO ffs have their complement. Not modeled properly.
  for (auto [user, _] : net.GetUsers(terminal)) {
    if (user && user->kind() == GateKind::kNot) return user->output();
  }
  return std::nullopt;
}

std::optional<std::pair<int, ChipBuilder>> ChipBuilder::TryPlace(
    const GateCluster& cluster, GateNetwork& net, GateTerminal signal) const {
  CHECK(!has(signal));

  auto comp = FindComplement(net, signal);

  auto users = [&](GateTerminal terminal)
      -> absl::flat_hash_set<std::pair<GateTerminal, GateTerminal>> {
    absl::flat_hash_set<std::pair<GateTerminal, GateTerminal>> ret;
    for (auto [user, _] : net.GetUsers(terminal)) {
      if (!user) continue;

      if (user->kind() == GateKind::kNot) {
        CHECK(comp);
        for (auto [not_user, _] : net.GetUsers(user->output())) {
          if (not_user && not_user->kind() != GateKind::kDead)
            ret.insert({*comp, not_user->output()});
        }
      } else if (user->kind() != GateKind::kDead) {
        ret.insert({signal, user->output()});
      }
    }
    return ret;
  };

  std::optional<std::pair<int, ChipBuilder>> ret = std::nullopt;

  auto try_loc = [&](Coords loc) {
    auto builder = *this;

    auto dist = [&](Coords loc2) {
      return std::abs(loc.first - loc2.first) +
             std::abs(loc.second - loc2.second);
    };

    auto closest_output = [&]() {
      std::optional<int> best_dist = std::nullopt;
      Coords ret;
      for (auto out_loc : builder.available_.at(FpgaResource::kOut)) {
        auto here = dist(out_loc);
        if (!best_dist || *best_dist > here) {
          best_dist = here;
          ret = out_loc;
        }
      }
      CHECK(best_dist);
      return ret;
    };

    CHECK(builder.available_.at(resources_at_locs_.at(loc)).erase(loc));

    builder.terminals_[signal] = {loc, 0};
    if (comp) {
      builder.terminals_[*comp] = {loc, 1};
    }
    builder.signal_defs_at_locs_[loc] = signal;

    int total_cost = 0;

    // Route from the gate to each user in the cluster. If the gate is an
    // output, also route to an output gate.
    for (auto [used, user] : users(signal)) {
      if (cluster.gates.contains(user)) {
        auto cost = builder.Route(used, builder.terminals_.at(user).first);
        if (!cost) {
          return;
        }
        total_cost += *cost;
      }
    }

    if (cluster.outputs.contains(signal) ||
        (comp && cluster.outputs.contains(*comp))) {
      auto out = closest_output();
      CHECK(builder.available_.at(FpgaResource::kOut).erase(out));
      builder.signal_defs_at_locs_[out] = signal;
      // TODO route complement if it's an output
      auto cost = builder.Route(signal, out);
      // TODO maybe try other outputs
      if (!cost) {
        return;
      }
      total_cost += *cost;
    }

    if (!ret || ret->first > total_cost) {
      ret = {total_cost, builder};
    }
  };

  if (cluster.inputs.contains(signal)) {
    for (auto loc : available_.at(FpgaResource::kIn)) {
      try_loc(loc);
    }
  } else {
    for (auto loc : available_.at(GetResource(signal))) {
      try_loc(loc);
    }
    // TODO reserve LUTs if used and not placed yet.
    if ((signal.first->kind() == GateKind::kNor ||
         signal.first->kind() == GateKind::kNand) &&
        signal.first->num_inputs() == 2) {
      for (auto loc : available_.at(FpgaResource::kLut2)) {
        try_loc(loc);
      }
    }
  }
  return ret;
}

FpgaChipConfig ChipBuilder::Build() const {
  auto ret = config_;

  // Set up input/output bits.
  for (auto [loc, signal] : signal_defs_at_locs_) {
    auto find_signal = [&](GateTerminal signal) -> std::optional<LocalLaneId> {
      if (!signals_.contains(signal)) {
        return std::nullopt;
      }
      const auto& locs = signals_.at(signal);

      for (auto gid : locs) {
        if (gid.node == loc) return gid.lane;
      }
      return std::nullopt;
    };

    auto comp = FindComplement(*net_, signal);
    auto res = resources_at_locs_.at(loc);
    if (signal.first) {
      CHECK(signal.first->kind() != GateKind::kNot);
    } else {
      CHECK(res == FpgaResource::kIn);
    }
    if (res == FpgaResource::kOut) {
      std::optional<LocalLaneId> lane = find_signal(signal);
      CHECK(lane);
      ret[loc].inputs()[0] = *lane;
    } else {
      std::optional<LocalLaneId> lane = find_signal(signal);
      std::optional<LocalLaneId> comp_lane = std::nullopt;
      if (comp) {
        comp_lane = find_signal(*comp);
      }

      // TODO tighten this CHECK
      CHECK(lane || comp_lane);
      if (lane) {
        ret[loc].outputs()[0] = *lane;
      }
      if (comp_lane) {
        ret[loc].outputs()[1] = *comp_lane;
      }

      if (res != FpgaResource::kIn) {
        auto& gate = *signal.first;
        for (int i = 0; i < gate.num_logical_inputs(); ++i) {
          auto in_lane = find_signal(gate.logical_input(i));
          CHECK(in_lane);
          ret[loc].inputs()[i] = *in_lane;
        }
      }
    }
  }
  return ret;
}

std::string FpgaChipConfig::to_ascii() const {
  int bw = spec_->bus_width;
  int g = bw + 2;
  std::vector<std::string> rows(spec_->rows() * g,
                                std::string(spec_->cols() * g, ' '));

  for (int r = 0; r < spec_->rows(); ++r) {
    for (int c = 0; c < spec_->cols(); ++c) {
      const auto& node = (*this)[{r, c}];

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

std::optional<ChipBuilder> RouteCluster(const FpgaSpec& spec,
                                        const GateCluster& cluster,
                                        GateNetwork& net) {
  graph::TopoSort sort(net, net.all_inputs(), {net.sink()});
  std::map<GateTerminal, int> topo_index;

  for (auto terminal : sort.order(false, false)) {
    CHECK(terminal.first);
    // We don't visit NOT gates.
    if (terminal.first->kind() != GateKind::kNot) {
      int index = topo_index.size();
      topo_index[terminal] = index;
    }
  }

  ChipBuilder b(spec, net);

  std::vector<GateTerminal> gates(cluster.gates.begin(), cluster.gates.end());
  std::sort(gates.begin(), gates.end(),
            [&](GateTerminal lhs, GateTerminal rhs) {
              return topo_index.at(lhs) > topo_index.at(rhs);
            });

  for (auto signal : gates) {
    auto cost_and_new_b = b.TryPlace(cluster, net, signal);
    if (!cost_and_new_b) {
      return std::nullopt;
    }
    b = cost_and_new_b->second;
  }

  for (auto in : cluster.inputs) {
    auto cost_and_new_b = b.TryPlace(cluster, net, in);
    if (!cost_and_new_b) {
      return std::nullopt;
    }
    b = cost_and_new_b->second;
  }

  return b;
}
