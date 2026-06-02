#ifndef INSTRUCTIONS_H__
#define INSTRUCTIONS_H__

#include "absl/types/span.h"
#include "isa/isa_types.h"

namespace isa {

absl::Span<const Field> GetFields();
absl::Span<const Instruction> GetInstructions();

}  // namespace isa

#endif
