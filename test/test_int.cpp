// Int_T<Ctx,N> over ClearCtx: signed arithmetic, comparison, division/remainder,
// shifts (arithmetic right), and sign-extend / truncate.
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/context/clear.h"
#include <cstdint>
#include <cstdio>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }
template <int N> static int64_t rds(const Int_T<ClearCtx, N>& x) {
  uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(x.w[i] & 1) << i;
  if (N < 64 && ((v >> (N - 1)) & 1)) v |= ~((uint64_t(1) << N) - 1);
  return (int64_t)v;
}

int main() {
  ClearCtx cx;
  using I = Int_T<ClearCtx, 32>;
  const int32_t A = -1234567, B = 7654321;
  I a = I::constant(cx, A), b = I::constant(cx, B);

  chk("add", rds<32>(a + b) == (int32_t)(A + B));
  chk("sub", rds<32>(a - b) == (int32_t)(A - B));
  chk("mul", rds<32>(a * b) == (int32_t)(A * B));
  chk("div", rds<32>(b / a) == (int32_t)(B / A));
  chk("mod", rds<32>(b % a) == (int32_t)(B % A));
  chk("neg", rds<32>(-a) == (int32_t)(-A));
  chk("and", rds<32>(a & b) == (int32_t)(A & B));
  chk("xor", rds<32>(a ^ b) == (int32_t)(A ^ B));
  chk("not", rds<32>(~a) == (int32_t)(~A));

  chk("lt", ((a < b).w & 1) == (A < B ? 1 : 0));     // signed: -1234567 < 7654321
  chk("gt", ((b > a).w & 1) == 1);
  chk("eq", ((a == b).w & 1) == 0);

  chk("shl", rds<32>(a << 3) == (int32_t)((uint32_t)A << 3));
  chk("shr_arith", rds<32>(a >> 4) == (int32_t)(A >> 4));   // sign-filling
  chk("sec_shl", rds<32>(a << UInt_T<ClearCtx, 32>::constant(cx, 3)) == (int32_t)((uint32_t)A << 3));
  chk("sec_shr_arith", rds<32>(a >> UInt_T<ClearCtx, 32>::constant(cx, 4)) == (int32_t)(A >> 4));
  chk("sext", rds<48>(a.sext<48>()) == (int64_t)A);
  chk("trunc", rds<16>(a.trunc<16>()) == (int16_t)(A & 0xffff));

  // as_unsigned / as_signed reinterpret (same wires, zero gates).
  chk("reinterpret roundtrip", rds<32>(a.as_unsigned().as_signed()) == (int64_t)A);
  { auto u = a.as_unsigned(); uint64_t uv = 0;
    for (int i = 0; i < 32; ++i) uv |= (uint64_t)(u.w[i] & 1) << i;
    chk("as_unsigned bits", uv == (uint32_t)A); }

  { auto e = I::encode(A); bool bb[32]; for (int i = 0; i < 32; ++i) bb[i] = e[i]; chk("encode/decode", I::decode(bb) == A); }

  printf("test_int: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
