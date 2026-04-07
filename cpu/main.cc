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
  auto a = net.AddInput<bw>();
  auto b = net.AddInput<bw>();
  auto carry_in = net.AddInput<1>();
  auto neg_b = net.AddInput<1>();
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

void BuildRegister(GateNetwork& net) {
  auto reset = net.AddInput<1>();
  auto clk = net.AddInput<1>();
  auto write_enable = net.AddInput<1>();
  auto write_data = net.AddInput<4>();
  GateReg<4> reg = MakeRegister(net, reset, clk, write_enable, write_data);
  net.DeclareOutput(reg);
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
    BuildRegister(net);
  } else {
    return print_usage();
  }

  auto transistor_net = Compile(net);
  print_graphviz(net);
  // print_graphviz(transistor_net);

  return 0;
}
