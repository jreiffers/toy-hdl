#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "alu.h"
#include "compiler.h"
#include "export.h"
#include "gate_lib.h"
#include "pc_gen.h"
#include "register_file.h"
#include "transistor_lib.h"

ABSL_FLAG(
    std::string, module, "",
    "The module to generate. One of alu, alu2, pcgen, register, register1");
ABSL_FLAG(std::string, format, "gnet",
          "The output to produce. gnet for the gate network, tnet for the "
          "transistor network, netlist for a serialized netlist. Netlist "
          "requires --output to be non-empty.");
ABSL_FLAG(std::string, output, "",
          "The output file to write to. If empty, write to stdout, unless the "
          "output is binary.");

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
  std::cerr << "usage: main --help\n";
  return 1;
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  GateNetwork net;

  std::string mod = absl::GetFlag(FLAGS_module);
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

  std::string format = absl::GetFlag(FLAGS_format);
  if (format == "gnet") {
    print_graphviz(net);
  } else if (format == "tnet") {
    print_graphviz(transistor_net);
  } else if (format == "netlist") {
    assert(!absl::GetFlag(FLAGS_output).empty());

    auto netlist = ExportNetlist(transistor_net);
    std::fstream output(absl::GetFlag(FLAGS_output),
                        std::ios::out | std::ios::trunc | std::ios::binary);
    assert(netlist.SerializeToOstream(&output));
  } else {
    return print_usage();
  }

  return 0;
}
