#include "node_detail.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <tuple>
#include <utility>

/**
 * Stencils for the copy-and-patch JIT. Each stencil is one node of a program,
 * evaluated on 8 rows at once. Live values form a stack in r0..r7; a stencil
 * at depth K finds K values live and passes the stack on to the next stencil.
 * Every stencil shares one signature, so the values stay in ymm0..ymm7.
 */

using v8 = __m256;
using stencil_t = void(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3,
                       v8 r4, v8 r5, v8 r6, v8 r7);

/** Patched with the node's value: a feature's byte offset, a constant's bits,
 * or a function's address. */
extern "C" char HOLE[];

/** The next stencil. */
extern "C" stencil_t CONTINUE;

namespace {

uintptr_t hole() { return reinterpret_cast<uintptr_t>(HOLE); }

/** Marks values at depth `live` and above as dead, so a stencil that calls
 * libm does not save them around the call. */
void kill(v8 (&r)[8], int live) {
  for (int j = live; j < 8; ++j)
    r[j] = _mm256_undefined_ps();
}

/** The libmvec function patched into the hole, called through a 64-bit
 * address since it may be out of range of a relative call. */
v8 (*libm())(v8) {
  uintptr_t f = hole();
  asm("" : "+r"(f));
  return reinterpret_cast<v8 (*)(v8)>(f);
}

} // namespace

namespace op {

struct add {
  static v8 apply(v8 a, v8 b) { return a + b; }
};
struct sub {
  static v8 apply(v8 a, v8 b) { return a - b; }
};
struct rsub {
  static v8 apply(v8 a, v8 b) { return b - a; }
};
struct mul {
  static v8 apply(v8 a, v8 b) { return a * b; }
};
/** fdimf: 0 when a <= b, otherwise a - b (NaN when either is NaN). */
struct fdim {
  static v8 apply(v8 a, v8 b) {
    return _mm256_andnot_ps(_mm256_cmp_ps(a, b, _CMP_LE_OQ), a - b);
  }
};
struct rfdim {
  static v8 apply(v8 a, v8 b) { return fdim::apply(b, a); }
};

struct abs {
  static v8 apply(v8 a) { return _mm256_andnot_ps(_mm256_set1_ps(-0.0f), a); }
};
struct sq {
  static v8 apply(v8 a) { return a * a; }
};
struct sqrt {
  static v8 apply(v8 a) { return _mm256_sqrt_ps(abs::apply(a)); }
};
struct sin {
  static v8 apply(v8 a) { return libm()(a); }
};
struct cos {
  static v8 apply(v8 a) { return libm()(a); }
};
/** 0 when |a| < MIN_VAL, otherwise log |a|. */
struct log {
  static v8 apply(v8 a) {
    v8 v = abs::apply(a);
    v8 small = _mm256_cmp_ps(v, _mm256_set1_ps(genetic::detail::MIN_VAL),
                             _CMP_LT_OQ);
    return _mm256_andnot_ps(small, libm()(v));
  }
};

} // namespace op

template <int K>
void var(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3, v8 r4, v8 r5,
         v8 r6, v8 r7) {
  v8 r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  r[K] = _mm256_loadu_ps(
      reinterpret_cast<const float *>(reinterpret_cast<const char *>(x) +
                                      hole()));
  [[clang::musttail]] return CONTINUE(x, y, r[0], r[1], r[2], r[3], r[4], r[5],
                                      r[6], r[7]);
}

template <int K>
void constant(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3, v8 r4,
              v8 r5, v8 r6, v8 r7) {
  v8 r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  uint32_t bits = static_cast<uint32_t>(hole());
  float c;
  std::memcpy(&c, &bits, sizeof(c));
  r[K] = _mm256_set1_ps(c);
  [[clang::musttail]] return CONTINUE(x, y, r[0], r[1], r[2], r[3], r[4], r[5],
                                      r[6], r[7]);
}

template <typename Op, int K>
void unary(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3, v8 r4, v8 r5,
           v8 r6, v8 r7) {
  v8 r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  r[K - 1] = Op::apply(r[K - 1]);
  [[clang::musttail]] return CONTINUE(x, y, r[0], r[1], r[2], r[3], r[4], r[5],
                                      r[6], r[7]);
}

/** A unary op that calls libm. */
template <typename Op, int K>
void call(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3, v8 r4, v8 r5,
          v8 r6, v8 r7) {
  v8 r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  kill(r, K);
  r[K - 1] = Op::apply(r[K - 1]);
  [[clang::musttail]] return CONTINUE(x, y, r[0], r[1], r[2], r[3], r[4], r[5],
                                      r[6], r[7]);
}

/** Combines r[K-2] (first operand) and r[K-1] (second) into r[K-2]. */
template <typename Op, int K>
void binary(const float *x, float *y, v8 r0, v8 r1, v8 r2, v8 r3, v8 r4, v8 r5,
            v8 r6, v8 r7) {
  v8 r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  r[K - 2] = Op::apply(r[K - 2], r[K - 1]);
  [[clang::musttail]] return CONTINUE(x, y, r[0], r[1], r[2], r[3], r[4], r[5],
                                      r[6], r[7]);
}

void store(const float *, float *y, v8 r0, v8, v8, v8, v8, v8, v8, v8) {
  _mm256_storeu_ps(y, r0);
}

namespace {

template <int... K>
constexpr std::array<stencil_t *, 2 * sizeof...(K)>
leaves(std::integer_sequence<int, K...>) {
  return {&var<K>..., &constant<K>...};
}

template <typename Op, int... K>
constexpr std::array<stencil_t *, sizeof...(K)>
unaries(std::integer_sequence<int, K...>) {
  return {&unary<Op, K + 1>...};
}

template <typename Op, int... K>
constexpr std::array<stencil_t *, sizeof...(K)>
calls(std::integer_sequence<int, K...>) {
  return {&call<Op, K + 1>...};
}

template <typename Op, int... K>
constexpr std::array<stencil_t *, sizeof...(K)>
binaries(std::integer_sequence<int, K...>) {
  return {&binary<Op, K + 2>...};
}

constexpr auto depths = std::make_integer_sequence<int, 8>();
constexpr auto pairs = std::make_integer_sequence<int, 7>();

} // namespace

/** Instantiates every stencil; extract.py finds them by symbol. */
[[gnu::used]] const auto instances = std::make_tuple(
    leaves(depths), unaries<op::abs>(depths), unaries<op::sq>(depths),
    unaries<op::sqrt>(depths), calls<op::sin>(depths),
    calls<op::cos>(depths), calls<op::log>(depths),
    binaries<op::add>(pairs), binaries<op::sub>(pairs),
    binaries<op::rsub>(pairs), binaries<op::mul>(pairs),
    binaries<op::fdim>(pairs), binaries<op::rfdim>(pairs), &store);
