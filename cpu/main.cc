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
#include "cpu/alu.h"
#include "cpu/compiler.h"
#include "cpu/export.h"
#include "cpu/gate_lib.h"
#include "cpu/register_file.h"
#include "cpu/transistor_lib.h"

ABSL_FLAG(std::string, module, "",
          "The module to generate. One of alu, alu1, alu2, pcgen, register, "
          "register1, decoder, mux");
ABSL_FLAG(std::string, format, "gnet",
          "The output to produce. gnet for the gate network, tnet for the "
          "transistor network, netlist for a serialized netlist. Netlist "
          "requires --output to be non-empty.");
ABSL_FLAG(std::string, output, "",
          "The output file to write to. If empty, write to stdout, unless the "
          "output is binary.");
ABSL_FLAG(bool, avoid_transmission_gates, true,
          "If set, do not emit transmission gates.");

template <int bw, int addrbits>
void BuildRegister(GateNetwork& net) {
  auto reset = net.AddInput<1>("reset");
  auto clk = net.AddInput<1>("clk");
  auto register_addr = net.AddInput<addrbits>("addr");
  auto read_addr_1 = net.AddInput<addrbits>("rd_addr1");
  auto read_addr_2 = net.AddInput<addrbits>("rd_addr2");
  auto write_addr = net.AddInput<addrbits>("wr_addr");
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
    net.Build<Alu<4>>();
  } else if (mod == "alu2") {
    net.Build<Alu<2>>();
  } else if (mod == "alu1") {
    net.Build<Alu<1>>();
  } else if (mod == "register") {
    BuildRegister<4, 2>(net);
  } else if (mod == "register1") {
    BuildRegister<1, 2>(net);
  } else if (mod == "pc") {
    BuildRegister<5, 0>(net);
  } else if (mod == "mux") {
    auto a = net.AddInput<1>("a");
    auto b = net.AddInput<1>("b");
    auto sel = net.AddInput<1>("sel");
    net.DeclareOutput(DynGateReg({net.Mux(sel[0], a[0], b[0])}));
  } else {
    return print_usage();
  }

  CompileOpts opts;
  opts.avoid_transmission_gates = absl::GetFlag(FLAGS_avoid_transmission_gates);
  auto transistor_net = Compile(net, opts);

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
    bool succ = netlist.SerializeToOstream(&output);
    (void)succ;
    assert(succ);
  } else {
    return print_usage();
  }

  return 0;
}
