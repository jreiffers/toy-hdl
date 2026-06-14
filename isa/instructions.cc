#include "isa/instructions.h"

namespace isa {

uint32_t GetFieldBits(isa::FieldSemantics sem) {
  std::optional<uint32_t> mask = std::nullopt;
  for (const auto& field : isa::GetFields()) {
    if (field.Has(sem)) {
      if (mask.value_or(field.mask) != field.mask) {
        throw std::logic_error("inconsistent mask for field semantics");
      }
      mask = field.mask;
    }
  }

  if (!mask) {
    throw std::logic_error("field semantics not found");
  }
  return mask.value();
}

}  // namespace isa
