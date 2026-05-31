#ifndef EMULATOR_H__
#define EMULATOR_H__

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "isa/visitor.h"

namespace isa {

template <int n>
struct uintn_t {
  uint8_t val;

  uintn_t() : val(0) {}
  uintn_t(uint32_t v) : val(v & ((1 << n) - 1)) {}
  operator uint32_t() const { return val; }
};

using uint2_t = uintn_t<2>;
using uint4_t = uintn_t<4>;
using uint6_t = uintn_t<6>;

class MachineState {
 public:
  virtual ~MachineState() = default;

  virtual uint4_t load(uint4_t bank, uint4_t addr) { return ram[bank][addr]; }
  virtual uint4_t load(uint4_t addr) { return ram[membank][addr]; }
  virtual void store(uint4_t addr, uint4_t val) { ram[membank][addr] = val; }
  virtual uint4_t read(uint2_t reg) { return registers[reg]; }

  virtual uint4_t load_gpi(uint4_t addr) { return gpi[addr]; }

  virtual void write(uint2_t reg, uint4_t val) { registers[reg] = val; }

  virtual void set_pc(uint16_t pc) { pc_ = pc; }
  virtual void push(uint4_t val) { bak_ = val; }
  virtual uint4_t pop() { return bak_; }

  virtual uint16_t pc() { return pc_; }
  virtual void jump(uint6_t addr) { pc_ = (rombank << 6) | addr; }
  virtual void push_pc() {
    if (call_stack_.size() >= 4) {
      throw std::logic_error("Call stack overflow.");
    }
    call_stack_.push_back(pc_);
  }
  virtual void pop_pc() {
    if (call_stack_.empty()) {
      throw std::logic_error("Call stack underflow");
    }
    pc_ = call_stack_.back();
    call_stack_.pop_back();
  }

  virtual void set_membank(uint4_t bank) { membank = bank; }
  virtual void set_rombank(uint4_t bank) { rombank = bank; }
  virtual bool flag() { return flag_; }
  virtual void set_flag(bool flag) { flag_ = flag; }

  void set_gpi(uint4_t addr, uint4_t val) { gpi[addr] = val; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const MachineState& p) {
    auto format_mem = [](auto mem) {
      return absl::StrFormat("%x%x%x%x %x%x%x%x %x%x%x%x %x%x%x%x", mem[0],
                             mem[1], mem[2], mem[3], mem[4], mem[5], mem[6],
                             mem[7], mem[8], mem[9], mem[10], mem[11], mem[12],
                             mem[13], mem[14], mem[15]);
    };

    absl::Format(&sink, "pc%d f%d r%x%x%x%x b%x m{{%s} {%s}}", p.pc_, p.flag_,
                 p.registers[0], p.registers[1], p.registers[2], p.registers[3],
                 p.bak_, format_mem(p.ram[0]), format_mem(p.ram[1]));
  }

 private:
  std::array<uint4_t, 4> registers;
  std::array<uint4_t, 16> gpi;
  std::array<std::array<uint4_t, 16>, 2> ram;
  uint16_t pc_ = 0;
  uint4_t bak_ = 0;  // "stack"
  bool flag_ = false;
  std::deque<uint16_t> call_stack_;
  int membank = 0;
  int rombank = 0;
};

class Emulator : private InstructionVisitorWithSemantics<absl::Status> {
 public:
  explicit Emulator(absl::Span<const uint16_t> rom,
                    std::unique_ptr<MachineState> state)
      : rom_(rom), state_(std::move(state)){};
  explicit Emulator(absl::Span<const uint16_t> rom)
      : rom_(rom), state_(std::make_unique<MachineState>()) {}

  absl::Status step();
  MachineState& state() { return *state_; }

 private:
  absl::Status Op(
      std::string_view mnemonic, absl::Span<const uint32_t> args,
      absl::Span<const InstrSemantics> instr_semantics,
      absl::Span<const absl::Span<const FieldSemantics>> field_semantics);

  absl::Span<const uint16_t> rom_;
  std::unique_ptr<MachineState> state_;
};

}  // namespace isa

#endif
