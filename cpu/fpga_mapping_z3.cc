#include "cpu/fpga_mapping_z3.h"

#include <z3++.h>

#include <array>
#include <set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "cpu/fpga_mapping.h"

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
            u.insert(terminal_ids.at(not_user->output()));
          }
        }
      } else if (user->kind() != GateKind::kDead) {
        u.insert(terminal_ids.at(user->output()));
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
}

std::vector<GateCluster> ClusterGates(const FpgaSpec& spec, GateNetwork& net,
                                      int max_clusters) {
  // Allowing gate replication enables us to trade off inputs for internal
  // signals, but it increases the size of the search space. This clusterer
  // doesn't allow it.
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

      CHECK(gate.kind() == GateKind::kNor || gate.kind() == GateKind::kNand ||
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
    z3::expr nor_overflow =
        z3::ite(used_fixed_nor + used_flex_nor > max_nor,
                used_fixed_nor + used_flex_nor - max_nor, c0);
    z3::expr nand_overflow =
        z3::ite(used_nand > max_nand, used_nand - max_nand, c0);
    constraints.push_back(nor_overflow + nand_overflow + used_lut <= max_lut);
  }

  // The number of inputs/output gates isn't exceeded.
  for (int k = 0; k < max_clusters; ++k) {
    z3::expr_vector input_signals(ctx);
    z3::expr_vector output_signals(ctx);

    for (auto [terminal, id] : index.terminal_ids) {
      z3::expr src_cluster = cluster[id];
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

      input_signals.push_back((src_cluster != k) && is_input);
      output_signals.push_back((src_cluster == k) && is_output);
    }

    constraints.push_back(z3::atmost(input_signals, max_in).simplify());
    constraints.push_back(z3::atmost(output_signals, max_out).simplify());
  }

  z3::solver solver(ctx, "QF_FD");
  solver.add(constraints);

  if (solver.check() != z3::sat) return {};

  z3::model m = solver.get_model();

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

    if (src_cluster >= 0 &&
        index.output_ids.contains(index.terminal_ids.at(terminal))) {
      out[src_cluster].outputs.insert(terminal);
    }
  }

  return out;
}
