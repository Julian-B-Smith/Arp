// The portable int128 (SPEC 2.2): known answers on every build, plus -- where the compiler has
// __int128 -- a randomised operation-by-operation cross-check of the two-word fallback against it.
// Planted control: a deliberately truncating floor division must be caught by the same cross-check.
#include "test_int128.h"

#include <cmath>
#include <cstdint>
#include <functional>

#include "arp/int128.h"
#include "check.h"

namespace arp_test {
using namespace arp;

namespace {

// xorshift64*: a fixed-seed generator, so a failure reproduces
struct Rng {
  std::uint64_t s = 0x9E3779B97F4A7C15ull;
  std::uint64_t next() {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
  }
  // a mix of magnitudes and edge values, either sign
  std::int64_t i64() {
    switch (next() % 6) {
      case 0: return static_cast<std::int64_t>(next() % 1000) - 500;
      case 1: return static_cast<std::int64_t>(next());
      case 2: return INT64_MAX - static_cast<std::int64_t>(next() % 4);
      case 3: return INT64_MIN + static_cast<std::int64_t>(next() % 4);
      case 4: return static_cast<std::int64_t>(next() % 100000000000ull) * ((next() & 1) ? 1 : -1);
      default: return static_cast<std::int64_t>(next() >> 20) * ((next() & 1) ? 1 : -1);
    }
  }
  std::int64_t posDivisor() {   // > 0, up to 2^62
    switch (next() % 4) {
      case 0: return 1 + static_cast<std::int64_t>(next() % 7);
      case 1: return 1 + static_cast<std::int64_t>(next() % 1000000);
      case 2: return 60ll * 48000 * 1000;   // a real D
      default: return 1 + static_cast<std::int64_t>(next() >> 2);
    }
  }
};

template <class T>
bool knownAnswers() {
  bool ok = true;
  auto eq = [&](bool c) { ok = ok && c; };
  const T big = T::mul(INT64_MAX, INT64_MAX);   // 2^126 - 2^64 + 1
  eq(big.hi() == (std::int64_t{1} << 62) - 1 && big.lo() == 1);
  const T pow = T::mul(INT64_MIN, INT64_MIN);   // 2^126
  eq(pow.hi() == (std::int64_t{1} << 62) && pow.lo() == 0);
  const T m1 = T::mul(-1, 1);
  eq(m1.hi() == -1 && m1.lo() == ~std::uint64_t{0} && m1.isNegative());
  // floor / ceil for both signs of the dividend (C's `/` truncates and would fail the negative rows)
  eq(T::fromInt(7).floorDiv(2) == 3 && T::fromInt(7).ceilDiv(2) == 4);
  eq(T::fromInt(-7).floorDiv(2) == -4 && T::fromInt(-7).ceilDiv(2) == -3);
  eq(T::fromInt(-1).floorDiv(3) == -1 && T::fromInt(-1).ceilDiv(3) == 0);
  eq(T::fromInt(-6).floorDiv(3) == -2 && T::fromInt(-6).ceilDiv(3) == -2);
  eq(T::fromInt(0).floorDiv(5) == 0 && T::fromInt(0).ceilDiv(5) == 0);
  // above 64 bits: (10^18 * 10^10 + r) / 10^10
  const T x = T::mul(1000000000000000000ll, 10000000000ll);
  eq(x.floorDiv(10000000000ll) == 1000000000000000000ll && x.ceilDiv(10000000000ll) == 1000000000000000000ll);
  eq((x + T::fromInt(5)).floorDiv(10000000000ll) == 1000000000000000000ll && (x + T::fromInt(5)).ceilDiv(10000000000ll) == 1000000000000000001ll);
  const T nx = -x;
  eq(nx.floorDiv(10000000000ll) == -1000000000000000000ll);
  eq((nx - T::fromInt(5)).floorDiv(10000000000ll) == -1000000000000000001ll && (nx - T::fromInt(5)).ceilDiv(10000000000ll) == -1000000000000000000ll);
  // order and carry
  eq(T::fromInt(-1) < T::fromInt(0) && T::mul(1ll << 40, 1ll << 40) > T::fromInt(INT64_MAX) && -T::mul(1ll << 40, 1ll << 40) < T::fromInt(INT64_MIN));
  eq((T::fromParts(0, ~std::uint64_t{0}) + T::fromInt(1)) == T::fromParts(1, 0));
  eq((T::fromParts(1, 0) - T::fromInt(1)) == T::fromParts(0, ~std::uint64_t{0}));
  return ok;
}

#if ARP_HAS_NATIVE_INT128
// Soft vs Native on the same operands. `softFloor` is injectable so the planted control can swap in a wrong one.
long crossCheck(const std::function<std::int64_t(const I128Soft&, std::int64_t)>& softFloor, int cases) {
  Rng r;
  long bad = 0;
  for (int i = 0; i < cases; ++i) {
    const std::int64_t a = r.i64(), b = r.i64(), c = r.i64(), d = r.i64(), q = r.posDivisor();
    const I128Soft sx = I128Soft::mul(a, b) + I128Soft::mul(c, d), sy = I128Soft::mul(c, a) - I128Soft::mul(b, d);
    const I128Native nx = I128Native::mul(a, b) + I128Native::mul(c, d), ny = I128Native::mul(c, a) - I128Native::mul(b, d);
    bool same = sx.hi() == nx.hi() && sx.lo() == nx.lo() && sy.hi() == ny.hi() && sy.lo() == ny.lo();
    same = same && (sx < sy) == (nx < ny) && (sx == sy) == (nx == ny) && (sx <= sy) == (nx <= ny);
    const I128Soft sn = -sx;
    const I128Native nn = -nx;
    same = same && sn.hi() == nn.hi() && sn.lo() == nn.lo() && sx.isNegative() == nx.isNegative();
    same = same && softFloor(sx, q) == nx.floorDiv(q) && sx.ceilDiv(q) == nx.ceilDiv(q) && softFloor(sn, q) == nn.floorDiv(q) && sn.ceilDiv(q) == nn.ceilDiv(q);
    const double ds = sx.toDouble(), dn = nx.toDouble();
    same = same && std::fabs(ds - dn) <= 1e-14 * std::fabs(dn);   // Soft rounds twice (hi and lo words), Native once
    if (!same) ++bad;
  }
  return bad;
}
#endif

}  // namespace

void runInt128Tests() {
  std::puts("int128");
#if defined(ARP_FORCE_INT128_FALLBACK)
  const bool expectNative = false;
#else
  const bool expectNative = ARP_HAS_NATIVE_INT128 != 0;
#endif
  check(kInt128IsNative == expectNative, std::string("int128 path under test is ") + (kInt128IsNative ? "native (__int128)" : "the two-word fallback"));
  check(knownAnswers<I128Soft>(), "I128Soft: known answers (products at the edges, floor/ceil for both signs, carry, order)");
#if ARP_HAS_NATIVE_INT128
  check(knownAnswers<I128Native>(), "I128Native: the same known answers");
  const auto truncating = [](const I128Soft& x, std::int64_t d) { return x.isNegative() ? x.ceilDiv(d) : x.floorDiv(d); };   // C's `/`: WRONG for a negative dividend
  plant(crossCheck(truncating, 2000) > 0, "the Soft-vs-Native cross-check catches a truncating floor division");
  const long bad = crossCheck([](const I128Soft& x, std::int64_t d) { return x.floorDiv(d); }, 30000);
  check(bad == 0, "I128Soft equals I128Native on 30000 random cases (mul, add, sub, neg, order, floor/ceil division, toDouble)");
#else
  std::puts("  (no __int128 on this compiler: the Soft-vs-Native cross-check is skipped; the known answers above still gate)");
#endif
}

}  // namespace arp_test
