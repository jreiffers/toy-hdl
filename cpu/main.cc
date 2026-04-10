#include <cassert>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "alu.h"
#include "compiler.h"
#include "eval.h"
#include "gate_lib.h"
#include "gate_opt.h"
#include "lower_gates.h"
#include "pc_gen.h"
#include "print.h"
#include "register_file.h"
#include "transistor_lib.h"
#include "transistor_opt.h"

template <int bw>
void BuildAlu(GateNetwork& net) {
  auto a = net.AddInput<bw>("a");
  auto b = net.AddInput<bw>("b");
  auto carry_in = net.AddInput<1>("cin");
  auto neg_b = net.AddInput<1>("negb");
  Alu<bw> alu = MakeAlu<bw>(net, a, b, carry_in, neg_b);
  net.DeclareOutput(alu.res);
  net.DeclareOutput(alu.carry_out);
  net.DeclareOutput(alu.zero);
}

void BuildPcGen(GateNetwork& net) {
  auto current_pc = net.AddInput<5>();
  auto do_jump = net.AddInput<1>();
  auto jump_addr = net.AddInput<5>();
  PcGen<5> pcgen = MakePcGen(net, current_pc, do_jump, jump_addr);
  net.DeclareOutput(pcgen.next_pc);
}

template <int bw>
void BuildRegister(GateNetwork& net) {
  auto reset = net.AddInput<1>("reset");
  auto clk = net.AddInput<1>("clk");
  auto register_addr = net.AddInput<2>("addr");
  auto read_addr_1 = net.AddInput<2>("rd_addr1");
  auto read_addr_2 = net.AddInput<2>("rd_addr2");
  auto write_addr = net.AddInput<2>("wr_addr");
  auto write_data = net.AddInput<bw>("wr_data");
  auto reg = MakeRegister(net, reset, clk, register_addr, read_addr_1,
                          read_addr_2, write_addr, write_data);
  net.DeclareOutput(reg.read_port_1, "rd_port1");
  net.DeclareOutput(reg.read_port_2, " rd_port2");
}

int print_usage() {
  std::cerr << "usage: main [alu|alu2|pcgen|register]\n";
  return 1;
}

int main(int argc, const char* argv[]) {
  GateNetwork net;
  if (argc != 2) {
    return print_usage();
  }

  std::string mod = argv[1];

  if (mod == "alu") {
    BuildAlu<4>(net);
  } else if (mod == "alu2") {
    BuildAlu<2>(net);
  } else if (mod == "pcgen") {
    BuildPcGen(net);
  } else if (mod == "register") {
    BuildRegister<4>(net);
  } else if (mod == "register1") {
    BuildRegister<1>(net);
  } else {
    return print_usage();
  }

  auto transistor_net = Compile(net);
  print_graphviz(net);
  // print_graphviz(transistor_net);

  return 0;
}
