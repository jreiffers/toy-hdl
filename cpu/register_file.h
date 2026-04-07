#ifndef REGISTER_FILE_H__
#define REGISTER_FILE_H__

#include "gate_lib.h"

template <int bw, int num_regs>
struct RegisterFile {
  std::array<GateReg<bw>, num_regs> reads;
};

template <int bw>
GateReg<bw> MakeRegister(GateNetwork& net, GateReg<1> reset, GateReg<1> clk,
                         GateReg<1> write_enable, GateReg<bw> write_data) {
  GateReg<bw> muxes = net.Mux(write_enable[0], write_data, write_data);

  GateReg<bw> out;
  for (int i = 0; i < bw; ++i) {
    out[i] = MakeDFlipFlop(net, muxes[i], clk[0], reset[0])[0];
    muxes[i].first->SetInput(3, out[i]);
  }

  return out;
}

namespace detail {

constexpr int log2(int v) {
  int r = 0;
  while (v >>= 1) {
    ++r;
  }
  return r;
}

}  // namespace detail

template <int bw, int num_regs>
RegisterFile<bw, num_regs> MakeRegisterFile(GateNetwork& net, GateReg<1> clock,
                                            GateReg<detail::log2(num_regs)> ra1,
                                            GateReg<detail::log2(num_regs)> ra2,
                                            GateReg<detail::log2(num_regs)> wa,
                                            GateReg<1> write_enable,
                                            GateReg<bw> write_data) {
  static_assert((num_regs & (num_regs - 1)) == 0);
  static_assert(detail::log2(128) == 7);
  absl::InlinedVector<GateReg<bw>, num_regs> regs;
  return {};
}

#endif
