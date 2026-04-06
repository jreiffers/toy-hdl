#ifndef PC_GEN_H__
#define PC_GEN_H__

template <int bw>
struct PcGen {
  GateReg<bw> next_pc;

  static absl::InlinedVector<uint32_t, 4> spec(
      absl::Span<const uint32_t> inputs) {
    constexpr uint32_t mask = ((1ull << bw) - 1);

    uint32_t pc = inputs[0];
    bool jump = inputs[1];
    uint32_t jump_addr = inputs[2];

    return {jump ? jump_addr : (pc + 1) & mask};
  }
};

template <int bw>
PcGen<bw> MakePcGen(GateNetwork& net, GateReg<bw> current_pc,
                    GateReg<1> do_jump, GateReg<bw> jump_addr) {
  GateReg<bw> zero;
  for (int i = 0; i < bw; ++i) {
    zero[i] = kLowGate;
  }

  GateReg<bw> pc_plus_one =
      MakeAdder(net, current_pc, zero, /*carry_in=*/kHighGate).first;
  GateReg<bw> next_pc = net.Mux(do_jump[0], jump_addr, pc_plus_one);
  return {next_pc};
}

#endif
