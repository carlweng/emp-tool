// UInt_T<Ctx,N> over ClearCtx: arithmetic, comparisons, shifts/rotates, and the
// width-changing views.
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/context/clear.h"
#include <cstdint>
#include <cstdio>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }
template <int N> static uint64_t rd(const UInt_T<ClearCtx, N>& u) {
  uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(u.w[i] & 1) << i; return v;
}

int main() {
  ClearCtx cx;
  using U = UInt_T<ClearCtx, 32>;
  const uint32_t A = 1234567u, B = 7654321u;
  U a = U::constant(cx, A), b = U::constant(cx, B);

  chk("add", rd<32>(a + b) == (uint32_t)(A + B));
  chk("sub", rd<32>(a - b) == (uint32_t)(A - B));
  chk("mul", rd<32>(a * b) == (uint32_t)(A * B));
  chk("div", rd<32>(b / a) == (B / A));
  chk("mod", rd<32>(b % a) == (B % A));
  chk("and", rd<32>(a & b) == (A & B));
  chk("or",  rd<32>(a | b) == (A | B));
  chk("xor", rd<32>(a ^ b) == (A ^ B));
  chk("not", rd<32>(~a) == (uint32_t)(~A));

  chk("lt", ((a < b).w & 1) == (A < B ? 1 : 0));
  chk("gt", ((a > b).w & 1) == (A > B ? 1 : 0));
  chk("le", ((a <= b).w & 1) == 1);
  chk("eq", ((a == b).w & 1) == 0);
  chk("ne", ((a != b).w & 1) == 1);
  chk("select", rd<32>(a.select((a < b), b)) == (A < B ? B : A));

  chk("shl", rd<32>(a << 5) == (uint32_t)(A << 5));
  chk("shr", rd<32>(a >> 5) == (A >> 5));
  chk("rotl", rd<32>(a.rotl(7)) == (uint32_t)((A << 7) | (A >> 25)));
  chk("rotr", rd<32>(a.rotr(7)) == (uint32_t)((A >> 7) | (A << 25)));

  chk("trunc", rd<16>(a.trunc<16>()) == (A & 0xffff));
  chk("zext", rd<48>(a.zext<48>()) == (uint64_t)A);
  chk("slice", rd<16>(a.slice<8, 24>()) == ((A >> 8) & 0xffff));
  chk("concat", rd<48>(U::constant(cx, 0xABCD).trunc<16>().concat(UInt_T<ClearCtx, 32>::constant(cx, A)))
                    == (((uint64_t)A << 16) | 0xABCD));

  // secret-amount (barrel) shifts.
  chk("dyn_shl", rd<32>(a << U::constant(cx, 5)) == (uint32_t)(A << 5));
  chk("dyn_shr", rd<32>(a >> U::constant(cx, 5)) == (A >> 5));
  chk("dyn_shl_overflow", rd<32>(a << U::constant(cx, 40)) == 0);

  // bit counting.
  chk("popcount", rd<6>(a.hamming_weight()) == (uint64_t)__builtin_popcount(A));
  chk("leading_zeros", rd<6>(a.leading_zeros()) == (uint64_t)__builtin_clz(A));
  chk("mod_exp 7^13 mod 1000",
      rd<32>(U::constant(cx, 7).mod_exp(U::constant(cx, 13), U::constant(cx, 1000))) == 407);

  { auto e = U::encode(A); bool bb[32]; for (int i = 0; i < 32; ++i) bb[i] = e[i]; chk("encode/decode", U::decode(bb) == A); }

  printf("test_uint: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
