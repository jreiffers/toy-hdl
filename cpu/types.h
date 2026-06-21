#ifndef TYPES_H__
#define TYPES_H__

template <int bitwidth>
struct Integer {
 public:
  Integer() : rep_(0) {}
  Integer(uint64_t val) : rep_(val) {
    assert(bitwidth <= 64);
    assert(bitwidth == 64 || (val < (1ull << bitwidth)));
  }

  uint64_t value() const { return rep_; }

  Integer operator&(Integer other) const { return {rep_ & other.rep_}; }
  Integer operator|(Integer other) const { return {rep_ | other.rep_}; }
  Integer operator^(Integer other) const { return {rep_ ^ other.rep_}; }
  Integer operator~() const {
    if (bitwidth == 64) return {~rep_};
    return (~rep_) & ((1ull << bitwidth) - 1);
  }
  Integer operator>>(int n) const { return {rep_ >> n}; }

  template <int offset, int bw>
  Integer<bw> Slice() const {
    return (rep_ >> offset) & ((1ull << bw) - 1);
  }

  Integer<1> operator==(Integer other) const { return {rep_ == other.rep_}; }

  Integer<1> operator[](int index) const { return {(rep_ >> index) & 1}; }

 private:
  uint64_t rep_;
};

template <typename T>
inline constexpr bool is_integer = false;
template <int bw>
inline constexpr bool is_integer<Integer<bw>> = true;
template <typename T>
concept IsInteger = is_integer<std::remove_cvref_t<T>>;

#endif
