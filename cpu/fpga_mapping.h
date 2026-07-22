#ifndef FPGA_MAPPING_H__
#define FPGA_MAPPING_H__

#include <bitset>
#include <deque>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "cpu/fpga.h"
#include "cpu/gate_lib.h"

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

 private:
  std::vector<std::vector<FpgaNode>> nodes_;
};

struct ChipBuilder {
  explicit ChipBuilder(const FpgaSpec& spec) : spec_(&spec), config_(spec) {
    for (int row = 0; row < spec.resources.size(); ++row) {
      auto& r = spec.resources[row];
      for (int col = 0; col < r.size(); ++col) {
        available_[r[col]].emplace(row, col);
      }
    }
    for (auto res : AllFpgaResources()) available_[res];
  }

  ChipBuilder(const ChipBuilder& other) = default;

  // Attempts to place the given gate on this chip. Returns the cost and a new
  // builder on success, or nullptr.
  std::optional<std::pair<int, ChipBuilder>> TryPlace(GateNetwork& net,
                                                      Gate& gate) const;
  bool has(GateTerminal terminal) const {
    return terminals_.contains(terminal);
  }

  std::string to_ascii() const;
  std::string bottleneck_resource() const {
    if (available_.at(FpgaResource::kIn).empty()) return "input";
    if (available_.at(FpgaResource::kNor).empty()) return "nor";
    if (available_.at(FpgaResource::kNand).empty()) return "nand";
    return "none";
  }
  std::string summary() const;
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

 private:
  // Adds `input` as an input to the chip if possible and it isn't already an
  // input. Returns true on success.
  bool AddInput(GateNetwork& net, GateTerminal input, int& use_count);
  // Returns the cost of the route and the lane we reached (or nullopt if we
  // couldn't find a route).
  std::optional<std::pair<int, LocalLaneId>> Route(GateTerminal source,
                                                   Coords dst);
  std::set<Coords>& available(FpgaResource res) { return available_.at(res); }

  const FpgaSpec* spec_;
  FpgaChipConfig config_;
  std::set<GlobalLaneId> used_lanes_;
  std::map<FpgaResource, std::set<Coords>> available_;
  absl::flat_hash_map<GateTerminal, std::pair<Coords, int /* output ID */>>
      terminals_;
  absl::flat_hash_map<GateTerminal, int> input_use_count_;

  // For each signal, the set of lanes that currently hold it.
  std::map<GateTerminal, std::set<GlobalLaneId>> signals_;
};

struct FpgaMapping {
  // naively, we need 2 * 2 * bus_width * (#in + #out) pins per gate,
  // since each connection uses two pins.
  //
  // we can do a lot better though, if we restrict connectivity a bit:
  //   - it probably rarely makes sense to put the same signal on more than one
  //   lane
  //     exception: nor gates that aren't fully utilized, output buffers
  //   - we might not need arbitrary horizontal/vertical lane connectivity
  //
  // since we know out and ~out are never connected to the same output,
  // this can immediately be reduced to
  //   2 * 1.5 * bus_width * #out
  //
  // normally, we don't have the same input on multiple pins either, with
  // two important execptions:
  //   - nor gates that don't use all inputs
  //   - output buffers
  //
  // ignoring this for now, the same applies to inputs (e.g. in0 bus in1 in2 bus
  // in3)
  //
  // 3 * bus_width * (#inputs + #outputs) pins per gate
  //
  // assuming 1.27mm pitch pin headers
  // 72 for an arity 4 gate (2 x 4 x 9) -> about 2x5x11mm
  // 48 for an arity 2 gate (2 x 4 x 6) -> about 2x5x8mm
  //
  // additionally, we need:
  //
  //   a small crossbar. we don't need n:m (4 * 6 pins -> 24), 1:1 is enough
  //
  //   e.g. 1:1ish
  //
  //          h1
  //       h0 v1 h3
  //    h1 v0 h2 v2 h1    13 pins
  //       h3 v3 h0
  //          h1
  //
  //   or h_i <-> v_i with very limited lane switching:
  //
  //   h0 v0 h1 v1 h2 v2 h3 v3    8 pins
  //
  //   passthroughs (2 x 2 x 4) -> 2 x 2.5mm x 5mm

  std::vector<ChipBuilder> chips;

  static FpgaMapping Map(const FpgaSpec& spec, GateNetwork& net);
};

#endif
