#ifndef FPGA_MAPPING_H__
#define FPGA_MAPPING_H__

#include <bitset>
#include <vector>

#include "cpu/fpga.h"
#include "cpu/gate_lib.h"

template <FpgaSpec spec>
struct FpgaMapping {
  struct FpgaChipConfig {
    // output signals (including inputs) -> bus
    std::array<std::bitset<spec.bus_width>, spec.num_out_signals()> outputs;

    // bus -> input signals (including output buffers)
    std::array<std::bitset<spec.bus_width>, spec.num_in_signals()> inputs;

    std::array<std::bitset<4>, spec.num_lut2s> luts;

    bool is_lane_used(int lane) const {
      for (const auto& out : outputs)
        if (out[lane]) return true;
      for (const auto& in : inputs)
        if (in[lane]) return true;
      return false;
    }

    bool is_output_connected(int output_index) const {
      return outputs[output_index].any();
    }

    bool is_input_connected(int input_index) const {
      return inputs[input_index].any();
    }

    bool is_used(FpgaResource res, int i) const {
      if (res == FpgaResource::kOutput) {
        return is_input_connected(spec.input_index(res, i, 0)) ||
               is_input_connected(spec.input_index(res, i, 1));
      }
      return is_output_connected(spec.output_index(res, i, false)) ||
             is_output_connected(spec.output_index(res, i, true));
    }

    // Finds an unused lane on the internal bus and returns its index.
    std::optional<int> allocate_lane() const {
      for (int i = 0; i < spec.bus_width; ++i)
        if (!is_lane_used(i)) return i;
      return std::nullopt;
    }

    // Finds an unused instance of the given resource and returns its index.
    std::optional<int> allocate(FpgaResource res) const {
      for (int i = 0; i < spec.capacity(res); ++i) {
        if (!is_used(res, i)) return i;
      }
      return std::nullopt;
    }
  };

  std::vector<FpgaChipConfig> chips;

  static FpgaMapping Map(const GateNetwork& net) {
    FpgaMapping res;

    // Naive algorithm for now:
    // 1. keep a queue of producers ready to be placed. initialize it with
    // outputs.
    // 2. while the queue is not empty:
    //   2.1. get the next gate
    //   2.2. find a chip that can hold it. try to place it on a chip that
    //   already holds consumers or inputs of the gate.
    //   2.3. place the gate, keeping track of where it is
    // 3. return the result.
    //
    // Currently, this maps each gate to exactly one chip. That may not always
    // be smart, since outputs are a limited resource.
    //
    // We don't yet handle flipflops.
    return {};
  }
};

#endif
