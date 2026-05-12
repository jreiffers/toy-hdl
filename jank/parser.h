#ifndef PARSER_H__
#define PARSER_H__

#include "jank/ast.h"
#include "jank/context.h"
#include "jank/lexer.h"

namespace jank {

Module Parse(Context& ctx);

}  // namespace jank

#endif
