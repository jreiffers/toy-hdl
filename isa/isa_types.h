#ifndef ISA_H__
#define ISA_H__

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"

namespace isa {

enum class FieldSemantics {
  kPredication,

  kRegReadAddr0,
  kRegReadAddr1,
  kRegWriteAddr,

  kImmediate,

  kAluCmp,
  kAluLhs,
  kAluRhs,

  kTestBitVal,

  kDerefSrc,

  kJumpAddr,
};

enum class InstrSemantics {
  kAluZeroLhs,  // Override the LHS input with 0.
  kAluZeroRhs,  // Override the RHS input with 0.
  kAluNotRhs,   // rhs = ~rhs
  kAluShr,      // result >>= 1
  kAluNot,      // ~result
  kAluAnd,      // and instead of +
  kAluCarryIn,

  kJump,
  kIndirect,  // Indirect jump (via r3)
  kPushPc,
  kPopPc,

  kPushReg,
  kPopReg,

  kStMem,  // Store alu result to address at read port 0.
  kLdGpi,

  kWait,  //  wait for a-b == testbit

  kFlagGet,  // alu rhs = flag
  kFlagSet,

  kMemBankSet,
  kRomBankSet,

  kCmpNz,  // override comparator to kNe
};

enum class Register {
  kR0,
  kR1,
  kR2,
  kR3,
};

enum class Comparator {
  kEq,
  kLt,
  kNe,
  kGe,
};

struct Field {
  uint32_t mask;
  std::string name;
  std::string type;
  std::vector<FieldSemantics> semantics;

  bool Has(FieldSemantics sem) const {
    return absl::c_contains(semantics, sem);
  }
};

struct InstructionMask {
  uint32_t opcode_mask;
  uint32_t opcode;
  uint32_t operand_mask;
};

struct Instruction {
  std::string_view mnemonic;
  InstructionMask mask;
  std::vector<Field> fields;
  std::vector<InstrSemantics> semantics;

  bool HasField(std::string_view name) const {
    for (auto& field : fields)
      if (field.name == name) return true;
    return false;
  }

  bool HasField(FieldSemantics sem) const {
    for (auto& field : fields)
      if (field.Has(sem)) return true;
    return false;
  }

  bool Has(FieldSemantics sem) const { return HasField(sem); }

  bool Has(InstrSemantics sem) const {
    return absl::c_contains(semantics, sem);
  }
};

}  // namespace isa

#endif
