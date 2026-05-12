#ifndef ASM_DISPATCH_H__
#define ASM_DISPATCH_H__

#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "isa/isa_types.h"
#include "isa/visitor.h"

namespace isa {

bool DispatchInstruction(InstructionVisitor<void>& visitor,
                         std::string_view mnemonic, bool pred,
                         std::optional<bool> deref_src,
                         absl::Span<const Register> registers,
                         absl::Span<const uint32_t> imms,
                         std::optional<Comparator> cmp,
                         std::optional<uint32_t> jumpaddr);

}  // namespace isa

#endif
