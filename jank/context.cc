#include "jank/context.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace jank {

std::string PrettyPrintSourceLoc(std::string_view source, SourceLocation loc) {
  SourceLocation l = loc;

  while (loc.line_number > 0 && !source.empty()) {
    int n = source.find('\n');
    if (n == source.npos) {
      return "<<invalid source location>>";
    }

    source = source.substr(n + 1);
    --loc.line_number;
  }

  int n = source.find('\n');
  std::string ret;
  absl::Format(&ret, "%4d | %s\n     | %s^", l.line_number + 1,
               source.substr(0, n), std::string(l.column_number, ' '));
  return ret;
}

std::string Context::error_string() const {
  std::string e;
  bool first = true;
  for (const auto& error : errors_) {
    if (!first) absl::StrAppend(&e, "\n");
    first = false;
    absl::StrAppend(&e, error.message, "\n",
                    PrettyPrintSourceLoc(source_code_, error.loc));
  }
  return e;
}

}  // namespace jank
