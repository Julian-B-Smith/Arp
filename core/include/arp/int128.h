// arp/int128.h - the signed 128-bit integer behind the exact position (SPEC 2.2).
//
// Positions and pending event times are signed 128-bit: 64-bit storage overflows in
// under two hours at 768 kHz. Two interchangeable implementations with ONE interface:
//   I128Native  a thin wrapper on __int128 (GCC/Clang), compiled whenever the compiler has it
//   I128Soft    two 64-bit words, plain C++, for compilers without __int128 (horde may build with MSVC)
// `I128` is Native where available, else Soft. -DARP_FORCE_INT128_FALLBACK makes it Soft anyway,
// which is how the test suite runs the whole core on the fallback path. Both classes are always
// compiled together when __int128 exists, so the tests can cross-check them operation by operation.
//
// The only divisions the core needs divide by a positive int64 (D, a step length times D, an
// increment per sample) and yield an int64, so that is all this offers; the division is a
// mathematical floor or ceiling for either sign of the dividend (never C's truncating `/`).
#pragma once

#include <cstdint>
#include <cmath>

#if defined(__SIZEOF_INT128__)
#define ARP_HAS_NATIVE_INT128 1
#else
#define ARP_HAS_NATIVE_INT128 0
#endif

namespace arp {

class I128Soft {
 public:
  constexpr I128Soft() = default;
  static constexpr I128Soft fromInt(std::int64_t v) {
    return I128Soft(v < 0 ? ~std::uint64_t{0} : 0, static_cast<std::uint64_t>(v));
  }
  static constexpr I128Soft fromParts(std::int64_t hi, std::uint64_t lo) {
    return I128Soft(static_cast<std::uint64_t>(hi), lo);
  }
  // Exact signed 64 x 64 -> 128 product.
  static I128Soft mul(std::int64_t a, std::int64_t b) {
    const bool neg = (a < 0) != (b < 0);
    const std::uint64_t ua = a < 0 ? 0 - static_cast<std::uint64_t>(a) : static_cast<std::uint64_t>(a);
    const std::uint64_t ub = b < 0 ? 0 - static_cast<std::uint64_t>(b) : static_cast<std::uint64_t>(b);
    constexpr std::uint64_t M = 0xFFFFFFFFu;
    const std::uint64_t a0 = ua & M, a1 = ua >> 32, b0 = ub & M, b1 = ub >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & M) + (p10 & M);
    I128Soft r(p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (p00 & M) | (mid << 32));
    return neg ? -r : r;
  }

  friend constexpr I128Soft operator+(I128Soft a, I128Soft b) {
    const std::uint64_t lo = a.lo_ + b.lo_;
    return I128Soft(a.hi_ + b.hi_ + (lo < a.lo_ ? 1 : 0), lo);
  }
  friend constexpr I128Soft operator-(I128Soft a) {
    const std::uint64_t lo = ~a.lo_ + 1;
    return I128Soft(~a.hi_ + (lo == 0 ? 1 : 0), lo);
  }
  friend constexpr I128Soft operator-(I128Soft a, I128Soft b) { return a + (-b); }
  I128Soft& operator+=(I128Soft o) { return *this = *this + o; }
  I128Soft& operator-=(I128Soft o) { return *this = *this - o; }

  friend constexpr bool operator==(I128Soft a, I128Soft b) { return a.hi_ == b.hi_ && a.lo_ == b.lo_; }
  friend constexpr bool operator<(I128Soft a, I128Soft b) {
    const auto ah = static_cast<std::int64_t>(a.hi_), bh = static_cast<std::int64_t>(b.hi_);
    return ah != bh ? ah < bh : a.lo_ < b.lo_;
  }
  friend constexpr bool operator!=(I128Soft a, I128Soft b) { return !(a == b); }
  friend constexpr bool operator>(I128Soft a, I128Soft b) { return b < a; }
  friend constexpr bool operator<=(I128Soft a, I128Soft b) { return !(b < a); }
  friend constexpr bool operator>=(I128Soft a, I128Soft b) { return !(a < b); }

  constexpr bool isNegative() const { return (hi_ >> 63) != 0; }
  constexpr std::int64_t hi() const { return static_cast<std::int64_t>(hi_); }
  constexpr std::uint64_t lo() const { return lo_; }

  // floor(this / d) and ceil(this / d) for d > 0. The quotient saturates at the int64 range
  // (the core's divisors keep it far inside; saturating beats undefined behaviour).
  std::int64_t floorDiv(std::int64_t d) const { return divide(d, false); }
  std::int64_t ceilDiv(std::int64_t d) const { return divide(d, true); }

  // Via the magnitude: hi*2^64 + lo on a two's-complement NEGATIVE value subtracts two huge doubles and loses a small result.
  double toDouble() const {
    if (isNegative()) return -(-*this).toDouble();
    return std::ldexp(static_cast<double>(hi_), 64) + static_cast<double>(lo_);
  }

 private:
  constexpr I128Soft(std::uint64_t hi, std::uint64_t lo) : hi_(hi), lo_(lo) {}

  std::int64_t divide(std::int64_t d, bool wantCeil) const {
    const bool neg = isNegative();
    const I128Soft mag = neg ? -*this : *this;   // |this| (INT128_MIN is out of the core's range)
    std::uint64_t qhi = 0, qlo = 0, rem = 0;
    const auto ud = static_cast<std::uint64_t>(d);
    if (mag.hi_ == 0) {
      qlo = mag.lo_ / ud;
      rem = mag.lo_ % ud;
    } else {
      // Bitwise long division. rem < d <= 2^63 - 1 always, so rem*2+1 never overflows 64 bits.
      for (int i = 127; i >= 0; --i) {
        const std::uint64_t bit = i >= 64 ? (mag.hi_ >> (i - 64)) & 1 : (mag.lo_ >> i) & 1;
        rem = (rem << 1) | bit;
        if (rem >= ud) {
          rem -= ud;
          if (i >= 64) qhi |= std::uint64_t{1} << (i - 64); else qlo |= std::uint64_t{1} << i;
        }
      }
    }
    // magnitude quotient q (qhi:qlo); floor of a negative dividend rounds the magnitude UP, ceil rounds it down
    const bool bump = rem != 0 && (neg != wantCeil);
    if (bump && ++qlo == 0) ++qhi;
    if (!neg) return (qhi != 0 || qlo > static_cast<std::uint64_t>(INT64_MAX)) ? INT64_MAX : static_cast<std::int64_t>(qlo);
    if (qhi != 0 || qlo > (std::uint64_t{1} << 63)) return INT64_MIN;
    return static_cast<std::int64_t>(0 - qlo);
  }

  std::uint64_t hi_ = 0;   // two's complement, high word
  std::uint64_t lo_ = 0;
};

#if ARP_HAS_NATIVE_INT128
class I128Native {
 public:
  __extension__ typedef __int128 Raw;
  constexpr I128Native() = default;
  static constexpr I128Native fromInt(std::int64_t v) { return I128Native(static_cast<Raw>(v)); }
  static constexpr I128Native fromParts(std::int64_t hi, std::uint64_t lo) {
    return I128Native((static_cast<Raw>(hi) * 2 * (static_cast<Raw>(1) << 63)) + static_cast<Raw>(lo));
  }
  static constexpr I128Native mul(std::int64_t a, std::int64_t b) { return I128Native(static_cast<Raw>(a) * static_cast<Raw>(b)); }

  friend constexpr I128Native operator+(I128Native a, I128Native b) { return I128Native(a.v_ + b.v_); }
  friend constexpr I128Native operator-(I128Native a) { return I128Native(-a.v_); }
  friend constexpr I128Native operator-(I128Native a, I128Native b) { return I128Native(a.v_ - b.v_); }
  I128Native& operator+=(I128Native o) { v_ += o.v_; return *this; }
  I128Native& operator-=(I128Native o) { v_ -= o.v_; return *this; }

  friend constexpr bool operator==(I128Native a, I128Native b) { return a.v_ == b.v_; }
  friend constexpr bool operator!=(I128Native a, I128Native b) { return a.v_ != b.v_; }
  friend constexpr bool operator<(I128Native a, I128Native b) { return a.v_ < b.v_; }
  friend constexpr bool operator>(I128Native a, I128Native b) { return a.v_ > b.v_; }
  friend constexpr bool operator<=(I128Native a, I128Native b) { return a.v_ <= b.v_; }
  friend constexpr bool operator>=(I128Native a, I128Native b) { return a.v_ >= b.v_; }

  constexpr bool isNegative() const { return v_ < 0; }
  constexpr std::int64_t hi() const { return static_cast<std::int64_t>(v_ >> 64); }
  constexpr std::uint64_t lo() const { return static_cast<std::uint64_t>(v_); }

  // A 128-bit division is a library call (tens of ns); nearly every dividend the core has fits in 64 bits (a position difference, an absolute tick
  // times D for hours of music), where the hardware does it in a few cycles. Same result, same rounding: the quotient of an int64 by a positive
  // int64 cannot overflow, and the saturation below only ever applied to the wide case.
  std::int64_t floorDiv(std::int64_t d) const {
    if (fitsInt64()) {
      const auto n = static_cast<std::int64_t>(v_);
      const std::int64_t q = n / d;
      return (n % d != 0 && n < 0) ? q - 1 : q;
    }
    Raw q = v_ / d;
    if (v_ % d != 0 && v_ < 0) --q;
    return saturate(q);
  }
  std::int64_t ceilDiv(std::int64_t d) const {
    if (fitsInt64()) {
      const auto n = static_cast<std::int64_t>(v_);
      const std::int64_t q = n / d;
      return (n % d != 0 && n > 0) ? q + 1 : q;
    }
    Raw q = v_ / d;
    if (v_ % d != 0 && v_ > 0) ++q;
    return saturate(q);
  }
  double toDouble() const { return static_cast<double>(v_); }

 private:
  constexpr explicit I128Native(Raw v) : v_(v) {}
  constexpr bool fitsInt64() const { return v_ >= static_cast<Raw>(INT64_MIN) && v_ <= static_cast<Raw>(INT64_MAX); }
  static constexpr std::int64_t saturate(Raw q) {
    return q > INT64_MAX ? INT64_MAX : q < INT64_MIN ? INT64_MIN : static_cast<std::int64_t>(q);
  }
  Raw v_ = 0;
};
#endif

#if ARP_HAS_NATIVE_INT128 && !defined(ARP_FORCE_INT128_FALLBACK)
using I128 = I128Native;
inline constexpr bool kInt128IsNative = true;
#else
using I128 = I128Soft;
inline constexpr bool kInt128IsNative = false;
#endif

}  // namespace arp
