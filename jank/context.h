#ifndef JANK_CONTEXT_H__
#define JANK_CONTEXT_H__

#include <string>
#include <string_view>

#include "absl/types/span.h"

namespace jank {

struct SourceLocation {
  int line_number;
  int column_number;
};

struct Error {
  std::string message;
  SourceLocation loc;
};

std::string PrettyPrintSourceLoc(std::string_view source, SourceLocation loc);

class Context {
 public:
  explicit Context(std::string source_code) : source_code_(source_code) {}

  std::string_view source_code() const { return source_code_; }

  absl::Span<const Error> errors() const { return errors_; }
  std::string error_string() const;

  void AddError(std::string message, SourceLocation loc) {
    errors_.push_back({std::move(message), loc});
  }

 private:
  std::string source_code_;
  std::vector<Error> errors_;
};

}  // namespace jank

#endif
