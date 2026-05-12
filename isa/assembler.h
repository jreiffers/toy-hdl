#include "isa/visitor.h"
#include "jank/context.h"

namespace isa {

bool ParseAssembly(jank::Context& context, InstructionVisitor<void>& visitor);

}
