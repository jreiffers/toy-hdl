#ifndef FPGA_MAPPING_H__
#define FPGA_MAPPING_H__

#include <bitset>
#include <deque>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "cpu/fpga.h"
#include "cpu/gate_lib.h"

struct GateCluster {
  std::set<GateTerminal> inputs;
  std::set<GateTerminal> gates;
  std::set<GateTerminal> outputs;
};

enum class BusOrientation { kHorizontal, kVertical };

inline BusOrientation operator!(BusOrientation o) {
  return o == BusOrientation::kHorizontal ? BusOrientation::kVertical
                                          : BusOrientation::kHorizontal;
}

struct LocalLaneId {
  BusOrientation orientation;
  int lane;

  bool operator<(LocalLaneId other) const {
    return (orientation < other.orientation) ||
           (orientation == other.orientation && lane < other.lane);
  }
};

using Coords = std::pair<int, int> /* row, col */;

struct GlobalLaneId {
  Coords node;
  LocalLaneId lane;

  bool operator<(GlobalLaneId other) const {
    return node < other.node || (node == other.node && lane < other.lane);
  }
};

struct FpgaNode {
 public:
  FpgaNode(int input_arity, int output_arity)
      : inputs_(input_arity), outputs_(output_arity) {}

  std::set<LocalLaneId>& bus_passthrough() { return bus_passthrough_; }
  std::vector<std::optional<LocalLaneId>>& inputs() { return inputs_; }
  std::vector<std::optional<LocalLaneId>>& outputs() { return outputs_; }
  std::vector<std::pair<int, int>>& hv_links() { return hv_links_; }

  const std::set<LocalLaneId>& bus_passthrough() const {
    return bus_passthrough_;
  }
  const std::vector<std::optional<LocalLaneId>>& inputs() const {
    return inputs_;
  }
  const std::vector<std::optional<LocalLaneId>>& outputs() const {
    return outputs_;
  }
  const std::vector<std::pair<int, int>>& hv_links() const { return hv_links_; }

  void link_lane(LocalLaneId src, int dst) {
    if (src.orientation == BusOrientation::kVertical) {
      hv_links_.push_back({src.lane, dst});
    } else {
      hv_links_.push_back({dst, src.lane});
    }
  }

  std::optional<LocalLaneId> linked_lane(LocalLaneId lane) const {
    if (lane.orientation == BusOrientation::kHorizontal) {
      for (auto [h, v] : hv_links_) {
        if (v == lane.lane) return {{BusOrientation::kVertical, h}};
      }
    } else {
      for (auto [h, v] : hv_links_) {
        if (h == lane.lane) return {{BusOrientation::kHorizontal, v}};
      }
    }
    return std::nullopt;
  }

 private:
  // Connections for the gate inhabiting this node.
  std::vector<std::optional<LocalLaneId>> inputs_;
  std::vector<std::optional<LocalLaneId>> outputs_;

  // Whether the given bus lane is connected.
  // TBD: where is the connection exactly? it could be between this node's and
  // the neighbor node's bus segment (in either direction, probably doesn't
  // matter), or it could be between inputs and outputs, which would allow us
  // to use the same lane for an input and for an output. this might be
  // better? On the other hand, it means that we need one extra bus segment if
  // the destination is on the wrong side of the bus.
  std::set<LocalLaneId> bus_passthrough_;

  // horizontal/vertical bus lanes we're linking at this node.
  std::vector<std::pair<int, int>> hv_links_;
};

struct FpgaChipConfig {
  explicit FpgaChipConfig(const FpgaSpec& spec);

  FpgaNode& operator[](Coords coords) {
    return nodes_[coords.first][coords.second];
  }

  const FpgaNode& operator[](Coords coords) const {
    return nodes_[coords.first][coords.second];
  }

  std::string to_ascii() const;

 private:
  const FpgaSpec* spec_;
  std::vector<std::vector<FpgaNode>> nodes_;
};

struct ChipBuilder {
  explicit ChipBuilder(const FpgaSpec& spec, GateNetwork& net)
      : spec_(&spec), config_(spec), net_(&net) {
    for (int row = 0; row < spec.resources.size(); ++row) {
      auto& r = spec.resources[row];
      for (int col = 0; col < r.size(); ++col) {
        available_[r[col]].emplace(row, col);
        resources_at_locs_[{row, col}] = r[col];
      }
    }
    for (auto res : AllFpgaResources()) available_[res];
  }

  ChipBuilder(const ChipBuilder& other) = default;

  // Attempts to place the given gate on this chip. Returns the cost and a new
  // builder on success, or nullptr.
  std::optional<std::pair<int, ChipBuilder>> TryPlace(
      const GateCluster& cluster, GateNetwork& net, GateTerminal signal) const;
  bool has(GateTerminal terminal) const {
    return terminals_.contains(terminal);
  }

  int num_used(FpgaResource res) const {
    return spec_->capacity(res) - available_.at(res).size();
  }

  std::set<GateTerminal> Signals() {
    std::set<GateTerminal> result;
    for (auto [key, _] : terminals_) {
      result.insert(key);
    }
    return result;
  }

  FpgaChipConfig Build() const;

 private:
  // Returns the cost of the route.
  std::optional<int> Route(GateTerminal source, Coords dst);
  std::set<Coords>& available(FpgaResource res) { return available_.at(res); }

  const FpgaSpec* spec_;
  FpgaChipConfig config_;
  GateNetwork* net_;
  std::set<GlobalLaneId> used_lanes_;
  std::map<FpgaResource, std::set<Coords>> available_;
  std::map<Coords, FpgaResource> resources_at_locs_;
  std::map<Coords, GateTerminal> signal_defs_at_locs_;
  absl::flat_hash_map<GateTerminal, std::pair<Coords, int /* output ID */>>
      terminals_;

  // For each signal, the set of lanes that currently hold it.
  std::map<GateTerminal, std::set<GlobalLaneId>> signals_;
};

std::optional<ChipBuilder> RouteCluster(const FpgaSpec& spec,
                                        const GateCluster& cluster,
                                        GateNetwork& net);

#endif
