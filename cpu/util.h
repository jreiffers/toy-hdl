#ifndef UTIL_H__
#define UTIL_H__

#include "absl/container/inlined_vector.h"
#include "absl/types/span.h"
#include "rfl.hpp"

namespace detail {

template <typename T>
void UnflattenImpl(T& out, absl::Span<const uint32_t>& in) {
  if constexpr (IsInteger<T>) {
    out = in[0];
    in = in.subspan(1);
  } else {
    rfl::to_view(out).apply(
        [&](const auto& field) { UnflattenImpl(*field.value(), in); });
  }
}

template <typename T>
void FlattenImpl(const T& in, absl::InlinedVector<uint32_t, 4>& out) {
  if constexpr (IsInteger<T>) {
    out.push_back(in.value());
  } else {
    rfl::to_view(in).apply(
        [&](const auto& field) { FlattenImpl(*field.value(), out); });
  }
}

}  // namespace detail

template <typename T>
T Unflatten(absl::Span<const uint32_t> in) {
  T res;
  detail::UnflattenImpl(res, in);
  assert(in.empty());
  return res;
}

template <typename T>
absl::InlinedVector<uint32_t, 4> Flatten(const T& in) {
  absl::InlinedVector<uint32_t, 4> out;
  detail::FlattenImpl(in, out);
  return out;
}

#endif
