#include "cpu/fpga_mapping_z3.h"

#include <z3++.h>

#include <array>
#include <set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "cpu/fpga_mapping.h"

struct BusLane {
  BusLane(z3::context& ctx, std::string_view prefix)
      : val(ctx.int_const(absl::StrCat(prefix, "_val").c_str())),
        dist(ctx.int_const(absl::StrCat(prefix, "_dist").c_str())),
        pt(ctx.bool_const(absl::StrCat(prefix, "_dist").c_str())) {}

  z3::expr val;   // Value index on this bus lane
  z3::expr dist;  // #hops (crossbar + passthrough)
  z3::expr pt;    // passthrough to predecessor enabled?
};

struct Crossbar {
  Crossbar(z3::context& ctx, std::string_view prefix) : crossbar{
    z3::expr_vector(ctx),
    z3::expr_vector(ctx),
    z3::expr_vector(ctx),
    z3::expr_vector(ctx)}
   {
    for (int h = 0; h < 4; ++h) {
      for (int v = 0; v < 4; ++v) {
        crossbar[h].push_back(
            ctx.bool_const(absl::StrCat(prefix, "_cb_", h, "_", v).c_str()));
      }
    }
  }

  z3::expr operator()(int h, int v) { return crossbar[h][v]; }

  std::array<z3::expr_vector, 4> crossbar;
};

struct Bus {
  Bus(z3::context& ctx, std::string_view prefix, bool h)
      : ctx(ctx),
        h(h),
        lanes{BusLane{ctx, absl::StrCat(prefix, "_0")},
             BusLane{ctx, absl::StrCat(prefix, "_1")},
             BusLane{ctx, absl::StrCat(prefix, "_2")},
             BusLane{ctx, absl::StrCat(prefix, "_3")}} {}

  BusLane& operator[](int i) { return lanes[i]; }

  z3::expr Has(int id) {
    z3::expr ret = ctx.bool_val(false);
    for (int i = 0; i < 4; ++i) {
      ret = ret | (lanes[i].val == id);
    }
    return ret;
  }

  z3::expr Has(z3::expr id) {
    z3::expr ret = ctx.bool_val(false);
    for (int i = 0; i < 4; ++i) {
      ret = ret | (lanes[i].val == id);
    }
    return ret;
  }

  z3::expr ProducedHere(int lane) { return lanes[lane].dist == 0; }

  z3::expr ReceivedFromCrossbar(int lane_id, Bus& other, Crossbar& cb) {
    CHECK(h != other.h);
    z3::expr ret = ctx.bool_val(false);
    auto& lane = lanes[lane_id];
    for (int i = 0; i < 4; ++i) {
      auto& peer = other.lanes[i];
      ret = ret | ((peer.val == lane.val) && (peer.dist == lane.dist - 1) &&
             (h ? cb(lane_id, i) : cb(i, lane_id)));
    }
    return ret;
  }

  z3::expr ReceivedFromLower(int lane_id, Bus* lower) {
    if (!lower) {
      return ctx.bool_val(false);
    }

    auto& lane = lanes[lane_id];
    auto& peer = lower->lanes[lane_id];
    return lane.pt && (peer.val == lane.val) && (peer.dist == lane.dist - 1);
  }

  z3::expr ReceivedFromUpper(int lane_id, Bus* upper) {
    if (!upper) {
      return ctx.bool_val(false);
    }

    auto& lane = lanes[lane_id];
    auto& peer = upper->lanes[lane_id];
    return peer.pt && (peer.val == lane.val) && (peer.dist == lane.dist - 1);
  }

  z3::expr IsRoutingValid(Crossbar& cb, Bus& pair_here, Bus* lower,
                          Bus* upper) {
    auto ret = ctx.bool_val(true);
    for (int lane_id = 0; lane_id < 4; ++lane_id) {
      ret = ret & z3::implies(lanes[lane_id].val > -1,
                         ProducedHere(lane_id) ||
                             ReceivedFromCrossbar(lane_id, pair_here, cb) ||
                             ReceivedFromLower(lane_id, lower) ||
                             ReceivedFromUpper(lane_id, upper));
    }
    return ret;
  }

  z3::context& ctx;
  bool h;
  std::array<BusLane, 4> lanes;
};

struct Node {
  Node(z3::context& ctx, std::string_view prefix)
      : ctx(ctx),
        h(ctx, absl::StrCat(prefix, "_h"), /*h=*/true),
        v(ctx, absl::StrCat(prefix, "_v"), /*h=*/false),
        cb(ctx, prefix),
        terminal(ctx.int_const(absl::StrCat(prefix, "_terminal").c_str())) {}

  z3::expr Has(int id) { return h.Has(id) || v.Has(id); }

  z3::expr Has(z3::expr id) { return h.Has(id) || v.Has(id); }

  void IsRoutingValid(Node* h_lo, Node* h_hi, Node* v_lo, Node* v_hi,
                      z3::expr_vector& ret) {
    ret.push_back(h.IsRoutingValid(cb, v, h_lo ? &h_lo->h : nullptr,
                                   h_hi ? &h_hi->h : nullptr));
    ret.push_back(v.IsRoutingValid(cb, h, v_lo ? &v_lo->v : nullptr,
                                   v_hi ? &v_hi->v : nullptr));

    // TODO: check physical constraints of crossbar

    // Lanes that claim to hold a value produced here must match the terminal.
    for (int lane_id = 0; lane_id < 4; ++lane_id) {
      auto& hl = h[lane_id];
      auto& vl = v[lane_id];
      ret.push_back(z3::implies(hl.dist == 0, terminal == hl.val));
      ret.push_back(z3::implies(vl.dist == 0, terminal == vl.val));
    }
  }

  z3::context& ctx;
  Bus h;
  Bus v;
  Crossbar cb;
  z3::expr terminal;
};

struct Output {
  z3::expr val;
  int row;
  int col;
};

struct Chip {
  Chip(z3::context& ctx, std::string_view prefix, const FpgaSpec& spec)
      : ctx(ctx) {
    for (int row_id = 0; row_id < spec.rows(); ++row_id) {
      auto& row = nodes.emplace_back();
      for (int col_id = 0; col_id < spec.cols(); ++col_id) {
        row.emplace_back(ctx, absl::StrCat(prefix, "_", row_id, "_", col_id));
        if (spec.resources[row_id][col_id] == FpgaResource::kOut) {
          outputs.emplace_back(
              ctx.int_const(absl::StrCat(prefix, "_o", outputs.size()).c_str()),
              row_id, col_id);
        }
      }
    }
  }

  z3::expr HasOutput(int id) {
    z3::expr ret = ctx.bool_val(false);
    for (auto& output : outputs) {
      ret = ret | (output.val == id);
    }
    return ret;
  }

  z3::expr_vector IsRoutingValid() {
    z3::expr_vector ret(ctx);

    for (int row = 0; row < nodes.size(); ++row) {
      for (int col = 0; col < nodes[0].size(); ++col) {
        nodes[row][col].IsRoutingValid(get(col - 1, row), get(col + 1, row),
                                       get(col, row - 1), get(col, row + 1),
                                       ret);
      }
    }

    for (auto& output : outputs) {
      ret.push_back(z3::implies(output.val != -1,
                                nodes[output.row][output.col].Has(output.val)));
    }

    return ret;
  }

  Node* get(int h, int v) {
    if (h < 0 || h >= nodes[0].size() || v < 0 || v >= nodes.size()) {
      return nullptr;
    }
    return &nodes[v][h];
  }

  z3::context& ctx;
  std::vector<std::vector<Node>> nodes;
  std::vector<Output> outputs;
};

struct GateIndex {
  explicit GateIndex(GateNetwork& net);

  std::vector<GateTerminal> terminals;
  absl::flat_hash_map<FpgaResource, std::vector<int>> terminal_indices_by_type;
  absl::flat_hash_map<GateTerminal, int> terminal_ids;

  absl::flat_hash_map<GateTerminal, std::set<int>> users;

  std::vector<int> input_ids;
  std::vector<int> non_input_ids;
  std::set<int> output_ids;
};

GateTerminal CanonicalTerminal(GateTerminal t) {
  if (t.first && t.first->kind() == GateKind::kNot) {
    return t.first->input(0);
  }
  return t;
}

GateIndex::GateIndex(GateNetwork& net) : terminals(net.all_inputs()) {
  std::cerr << "Index\n";
  for (int i = 0; i < terminals.size(); ++i) {
    terminal_indices_by_type[FpgaResource::kIn].push_back(i);
    input_ids.push_back(i);
  }

  for (auto res : AllFpgaResources()) {
    terminal_indices_by_type[res];
  }

  net.WalkUnordered([&](int, Gate& gate) {
    switch (gate.kind()) {
      case GateKind::kDead:
      case GateKind::kTriStateBuffer:
        LOG(FATAL) << "Unsupported gate.";
        break;
      case GateKind::kMux:
        terminal_indices_by_type[FpgaResource::kMux].push_back(
            terminals.size());
        break;
      case GateKind::kNot:
        return;
      case GateKind::kNand:
        CHECK(gate.num_inputs() == 2);
        terminal_indices_by_type[FpgaResource::kNand].push_back(
            terminals.size());
        terminal_indices_by_type[FpgaResource::kLut2].push_back(
            terminals.size());
        break;
      case GateKind::kNor:
        CHECK(gate.num_inputs() <= 4);
        CHECK(gate.num_inputs() > 1);
        terminal_indices_by_type[FpgaResource::kNor].push_back(
            terminals.size());
        if (gate.num_inputs() == 2) {
          terminal_indices_by_type[FpgaResource::kLut2].push_back(
              terminals.size());
        }
        break;
      case GateKind::kLookup:
        CHECK(gate.num_inputs() == 4);
        terminal_indices_by_type[FpgaResource::kLut2].push_back(
            terminals.size());
        break;
    }

    terminals.push_back(gate.output());
  });

  for (int i = 0; i < terminals.size(); ++i) {
    terminal_ids[terminals[i]] = i;
  }

  for (int i = 0; i < terminals.size(); ++i) {
    auto& u = users[terminals[i]];
    for (auto [user, _] : net.GetUsers(terminals[i])) {
      CHECK(user);

      if (user->kind() == GateKind::kNot) {
        for (auto [not_user, _] : net.GetUsers(user->output())) {
          if (not_user && not_user->kind() != GateKind::kDead) {
            std::cerr << "O\n";
            u.insert(terminal_ids.at(not_user->output()));
            std::cerr << "/O\n";
          }
        }
      } else if (user->kind() != GateKind::kDead) {
            std::cerr << "o\n";
        u.insert(terminal_ids.at(user->output()));
            std::cerr << "/o\n";
      }
    }
  }

  for (int i = input_ids.size(); i < terminals.size(); ++i) {
    non_input_ids.push_back(i);
  }

  for (auto out : net.GetOutputs()) {
    for (int i = 0; i < out.bitwidth(); ++i) {
      output_ids.insert(terminal_ids.at(CanonicalTerminal(out[i])));
    }
  }
  std::cerr << "/Index\n";
}

std::vector<FpgaChipConfig> MapFpgaZ3(const FpgaSpec& spec, GateNetwork& net,
                                      int max_chips) {
  CHECK(spec.bus_width == 4);
  CHECK(spec.nor_arity == 4);

  // 1. Gates are mapped to nodes. Each node can hold only one gate.
  //    Mapping gates to more than one node is allowed if they're on different
  //    chips.
  z3::context ctx;
  z3::solver opt(ctx);

  GateIndex index(net);
  std::vector<Chip> chips;

  for (int i = 0; i < max_chips; ++i) {
    auto& chip = chips.emplace_back(ctx, absl::StrCat("chip", i), spec);
    opt.add(chip.IsRoutingValid());

    for (int row_idx = 0; row_idx < spec.rows(); ++row_idx) {
      for (int col_idx = 0; col_idx < spec.cols(); ++col_idx) {
        auto res = spec.resources[row_idx][col_idx];
        auto& node = *chip.get(col_idx, row_idx);
        //auto in_arity = z3::ite(node.terminal == -1, ctx.int_val(0),
        //                        ctx.int_val(res == FpgaResource::kMux ? 3 : 2));

        // Returns an expression indicating whether the node has all the
        // inputs to `gate`.
        auto has_inputs = [&](Gate& gate) {
          int n = gate.num_logical_inputs();
          auto has_all = ctx.bool_val(true);
          for (int i = 0; i < n; ++i) {
            has_all = has_all & node.Has(index.terminal_ids.at(gate.logical_input(i)));
          }
          return has_all;
        };

        auto cond = node.terminal == -1;
        // output nodes always have node.terminal == -1.
        for (int terminal_idx : index.terminal_indices_by_type[res]) {
          //if (res == FpgaResource::kNor) {
          //  in_arity +=
          //      z3::ite(node.terminal == terminal_idx,
          //              terminals[terminal_idx].first->input_count() - 2, 0);
          //}
          cond = cond || (node.terminal == terminal_idx);

          auto term = index.terminals[terminal_idx];
          if (term.first) {
            // network inputs have no inputs
            opt.add(z3::implies(node.terminal == terminal_idx,
                              has_inputs(*term.first)));
          }
        }
        opt.add(cond);
      }
    }
  }

  // All outputs should be placed.
  for (auto out : net.GetOutputs()) {
    for (int i = 0; i < out.bitwidth(); ++i) {
      z3::expr available = ctx.bool_val(false);
      for (auto& chip : chips) {
        available = available | chip.HasOutput(index.terminal_ids.at(out[i]));
      }
      opt.add(available);
    }
  }

  std::cerr << opt << "\n";

  std::cerr << opt.check() << "\n";

  z3::model m = opt.get_model();
  std::cerr << m << "\n";

  return {};
}

std::vector<GateCluster> ClusterGates2(const FpgaSpec& spec, GateNetwork& net,
    int max_clusters) {
  // Allowing gate replication enables us to trade off inputs for internal signals, but it increases
  // the size of the search space. This clusterer doesn't allow it.
  z3::context ctx;
  z3::expr c0 = ctx.int_val(0);
  z3::expr c1 = ctx.int_val(1);
  const GateIndex index(net);
  z3::expr_vector cluster(ctx);
  z3::expr_vector constraints(ctx);

  int next = 0;
  for (int i : index.input_ids) {
    CHECK(i == next++);
    // Inputs are in no cluster.
    cluster.push_back(ctx.int_val(-1));
  }

  bool first = true;
  for (int i : index.non_input_ids) {
    CHECK(i == next++);
    // Each internal gate is in exactly one cluster.
    auto c = ctx.int_const(absl::StrCat("cluster_", i).c_str());
    cluster.push_back(c);

    if (first) {
      constraints.push_back(c == 0);
      first = false;
    } else {
      constraints.push_back(c >= 0);
      constraints.push_back(c < max_clusters);
    }
  }

  // Cluster gate capacities aren't exceeded.
  int max_in = spec.capacity(FpgaResource::kIn);
  int max_out = spec.capacity(FpgaResource::kOut);
  int max_nor = spec.capacity(FpgaResource::kNor);
  int max_nand = spec.capacity(FpgaResource::kNand);
  int max_lut = spec.capacity(FpgaResource::kLut2);

  for (int k = 0; k < max_clusters; ++k) {
    z3::expr used_fixed_nor = c0;
    z3::expr used_flex_nor = c0;
    z3::expr used_nand = c0;
    z3::expr used_lut = c0;

    for (int id : index.non_input_ids) {
      auto& gate = *index.terminals[id].first;       

      CHECK(gate.kind() == GateKind::kNor ||
            gate.kind() == GateKind::kNand ||
            gate.kind() == GateKind::kLookup);

      z3::expr weight = z3::ite(cluster[id] == k, c1, c0);

      if (gate.num_logical_inputs() > 2) {
        CHECK(gate.kind() == GateKind::kNor);
        used_fixed_nor = used_fixed_nor + weight;
      } else {
        CHECK(gate.num_logical_inputs() == 2);

        switch (gate.kind()) {
          case GateKind::kNor:
            used_flex_nor = used_flex_nor + weight;
            break;
          case GateKind::kNand:
            used_nand = used_nand + weight;
            break;
          case GateKind::kLookup:
            used_lut = used_lut + weight;
            break;
        }
      }
    }

    constraints.push_back(used_fixed_nor <= max_nor);
    z3::expr nor_overflow = z3::ite(used_fixed_nor + used_flex_nor > max_nor,
                            used_fixed_nor + used_flex_nor - max_nor, c0);
    z3::expr nand_overflow = z3::ite(used_nand > max_nand, used_nand - max_nand,
        c0);
    constraints.push_back(nor_overflow + nand_overflow + used_lut <= max_lut);
  }

  // The number of inputs/output gates isn't exceeded.
  for (int k = 0; k < max_clusters; ++k) {
    z3::expr_vector input_signals(ctx);
    z3::expr_vector output_signals(ctx);

    for (auto [terminal, id] : index.terminal_ids) {
      z3::expr is_input = ctx.bool_val(false);

      const std::set<int>& users = index.users.at(terminal);
      bool is_net_output = index.output_ids.contains(id);

      z3::expr is_output = ctx.bool_val(is_net_output);
      for (int user : users) {
        // input into cluster k?
        is_input = is_input | (cluster[user] == k);

        // output from cluster k?
        is_output = is_output | (cluster[user] != k);
      }

      z3::expr src_cluster = cluster[id];
      input_signals.push_back((src_cluster != k) && is_input);
      output_signals.push_back((src_cluster == k) && is_output);
    }

    constraints.push_back(z3::atmost(input_signals, max_in).simplify());
    constraints.push_back(z3::atmost(output_signals, max_out).simplify());
  }

  z3::solver solver(ctx, "QF_FD");
  solver.add(constraints);
  std::cerr << solver << "\n";

  std::cerr << solver.check() << "\n";

  z3::model m = solver.get_model();
  std::cerr << m << "\n";

  std::vector<GateCluster> out(max_clusters);
  
  std::vector<int> clusters_for_terminal_ids;
  absl::flat_hash_map<GateTerminal, int> clusters_for_terminals;

  for (int i = 0; i < cluster.size(); ++i) {
    int c = m.eval(cluster[i]).get_numeral_int();
    CHECK_EQ(c < 0, index.terminals[i].first == nullptr); 
    clusters_for_terminals[index.terminals[i]] = c;
    clusters_for_terminal_ids.push_back(c);
    if (c >= 0) {
      out[c].gates.insert(index.terminals[i]);
    }
  }

  for (const auto& [terminal, users] : index.users) {
    int src_cluster = clusters_for_terminals.at(terminal);
    for (int user : users) {
      int dst_cluster = clusters_for_terminal_ids.at(user);

      if (src_cluster != dst_cluster) {
        out[dst_cluster].inputs.insert(terminal);
        if (src_cluster >= 0) {
          out[src_cluster].outputs.insert(terminal);
        }
      }
    }

    if (src_cluster >= 0 && index.output_ids.contains(index.terminal_ids.at(terminal))) {
      out[src_cluster].outputs.insert(terminal);
    }
  }

  for (const auto& cluster : out) {
    std::cerr << "cluster with " << cluster.inputs.size() << " inputs, " << cluster.gates.size() << " gates, " << cluster.outputs.size() << " outputs\n";
  }

  return out;
}
