#ifndef REGISTER_FILE_H__
#define REGISTER_FILE_H__

#include "gate_lib.h"

template <int bw, int num_regs>
struct RegisterFile {
  std::array<GateReg<bw>, num_regs> reads;
};

template <int bw>
struct RegisterOutput {
  // Outputs will be hi-z if the register isn't selected for reading.
  GateReg<bw> read_port_1;
  GateReg<bw> read_port_2;
};

template <int bw, int register_addr_bits>
RegisterOutput<bw> MakeRegister(GateNetwork& net, GateReg<1> reset,
                                GateReg<1> clk,
                                GateReg<register_addr_bits> my_addr,
                                GateReg<register_addr_bits> read_addr_1,
                                GateReg<register_addr_bits> read_addr_2,
                                GateReg<register_addr_bits> write_addr,
                                GateReg<bw> write_data) {
  GateTerminal write_enable;
  {
    ScopeGuard guard(net, "write_enable");
    write_enable = net.Eq(my_addr, write_addr);
  }

  GateReg<bw> muxes;
  {
    ScopeGuard guard(net, "write_data");
    muxes = net.Mux(write_enable, write_data, write_data);
  }

  GateReg<bw> value;
  for (int i = 0; i < bw; ++i) {
    ScopeGuard guard(net, absl::StrCat("bit", i));
    value[i] = MakeDFlipFlop(net, muxes[i], clk[0], reset[0])[0];
    muxes[i].first->SetInput(3, value[i]);
  }
  RegisterOutput<bw> out;

  auto make_read_port = [&net, &value, &my_addr](
                            int index, GateReg<register_addr_bits>& read_addr) {
    ScopeGuard guard(net, absl::StrCat("read_port", index));

    GateTerminal read_enable;
    {
      ScopeGuard re_guard(net, "read_enable");
      read_enable = net.Eq(my_addr, read_addr);
    }

    {
      ScopeGuard output_guard(net, "output_buffer");
      return net.TriStateBuffer(read_enable, value);
    }
  };

  out.read_port_1 = make_read_port(0, read_addr_1);
  out.read_port_2 = make_read_port(1, read_addr_2);

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
