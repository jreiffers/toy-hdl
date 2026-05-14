#ifndef EMULATOR_H__
#define EMULATOR_H__

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "isa/visitor.h"

namespace isa {

struct uint4_t {
  uint8_t val;

  uint4_t() : val(0) {}
  uint4_t(uint32_t v) : val(v & 15) {}
  operator uint32_t() const { return val; }
};

struct MachineState {
  uint4_t Load(uint4_t addr) { return ram[membank][addr]; }
  void Store(uint4_t addr, uint4_t val) { ram[membank][addr] = val; }

  std::array<uint4_t, 4> registers;
  std::array<uint4_t, 16> gpi;
  std::array<std::array<uint4_t, 16>, 2> ram;
  uint16_t pc = 0;
  uint4_t bak = 0;  // "stack"
  bool flag = false;
  std::deque<uint16_t> call_stack;
  int membank = 0;
  int rombank = 0;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const MachineState& p) {
    auto format_mem = [](auto mem) {
      return absl::StrFormat("%x%x%x%x %x%x%x%x %x%x%x%x %x%x%x%x", mem[0],
                             mem[1], mem[2], mem[3], mem[4], mem[5], mem[6],
                             mem[7], mem[8], mem[9], mem[10], mem[11], mem[12],
                             mem[13], mem[14], mem[15]);
    };

    absl::Format(&sink, "pc%d f%d r%x%x%x%x b%x m{{%s} {%s}}", p.pc, p.flag,
                 p.registers[0], p.registers[1], p.registers[2], p.registers[3],
                 p.bak, format_mem(p.ram[0]), format_mem(p.ram[1]));
  }
};

class Emulator : private InstructionVisitorWithSemantics<absl::Status> {
 public:
  explicit Emulator(absl::Span<const uint16_t> rom) : rom_(rom) {}

  absl::Status step();
  MachineState& state() { return state_; }

 private:
  absl::Status Op(
      std::string_view mnemonic, absl::Span<const uint32_t> args,
      absl::Span<const InstrSemantics> instr_semantics,
      absl::Span<const absl::Span<const FieldSemantics>> field_semantics);

  absl::Span<const uint16_t> rom_;
  MachineState state_;
};

}  // namespace isa

#endif
