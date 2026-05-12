#ifndef ENCDEC_H__
#define ENCDEC_H__

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "isa/visitor.h"

namespace isa {
namespace detail {
class EncodeVisitor;
}

class Encoder {
 public:
  Encoder();
  Encoder(Encoder&&) = delete;

  InstructionVisitor<void>& visitor() { return *visitor_; }
  absl::Span<const uint16_t> instructions() { return instrs_; }

 private:
  std::unique_ptr<InstructionVisitor<void>> visitor_;
  std::vector<uint16_t> instrs_;

  friend class ::isa::detail::EncodeVisitor;
};

void DecodeInstruction(uint16_t instr, InstructionVisitor<void>& visitor);
absl::Status DecodeInstruction(uint16_t instr,
                               InstructionVisitor<absl::Status>& visitor);

}  // namespace isa

#endif
