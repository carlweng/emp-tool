// BitVec_T<Ctx,N> over ClearCtx: constant, indexing, as_uint, slice/concat, codec.
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/context/clear.h"
#include <array>
#include <cstdint>
#include <cstdio>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }
template <int N> static uint64_t rdv(const BitVec_T<ClearCtx, N>& v) {
  uint64_t r = 0; for (int i = 0; i < N; ++i) r |= (uint64_t)(v.w[i] & 1) << i; return r;
}
template <int N> static uint64_t rdu(const UInt_T<ClearCtx, N>& u) {
  uint64_t r = 0; for (int i = 0; i < N; ++i) r |= (uint64_t)(u.w[i] & 1) << i; return r;
}
template <int N> static std::array<bool, N> bits_of(uint64_t v) {
  std::array<bool, N> b{}; for (int i = 0; i < N; ++i) b[i] = (v >> i) & 1; return b;
}

int main() {
  ClearCtx cx;
  using V8 = BitVec_T<ClearCtx, 8>;
  V8 v = V8::constant(cx, bits_of<8>(0xB5));

  chk("constant", rdv<8>(v) == 0xB5);
  chk("index", (v[3].w & 1) == ((0xB5 >> 3) & 1));
  chk("as_uint", rdu<8>(v.as_uint()) == 0xB5);
  chk("slice", rdv<4>(v.slice<0, 4>()) == (0xB5 & 0xf));

  V8 hi = V8::constant(cx, bits_of<8>(0x3C));
  chk("concat", rdv<16>(v.concat(hi)) == ((0x3C << 8) | 0xB5));   // v is the low half

  // assemble from Bit_T values, then reinterpret.
  Bit_T<ClearCtx> bits[8];
  for (int i = 0; i < 8; ++i) bits[i] = Bit_T<ClearCtx>::constant(cx, (0x5A >> i) & 1);
  chk("from_bit_values", rdv<8>(V8::from_bit_values(cx, bits)) == 0x5A);

  // bitwise / equality / select / shifts.
  V8 x = V8::constant(cx, bits_of<8>(0x5A));
  chk("and", rdv<8>(v & x) == (0xB5 & 0x5A));
  chk("or",  rdv<8>(v | x) == (0xB5 | 0x5A));
  chk("xor", rdv<8>(v ^ x) == (0xB5 ^ 0x5A));
  chk("not", rdv<8>(~v) == (uint8_t)(~0xB5));
  chk("eq",  ((v == v).w & 1) == 1);
  chk("ne",  ((v != x).w & 1) == 1);
  chk("select", rdv<8>(v.select((v == v), x)) == 0x5A);          // true ? x : v
  chk("shl", rdv<8>(v << 2) == (uint8_t)(0xB5 << 2));
  chk("shr", rdv<8>(v >> 2) == (0xB5 >> 2));

  auto c = bits_of<8>(0xB5);
  { auto e = V8::encode(c); bool bb[8]; for (int i = 0; i < 8; ++i) bb[i] = e[i]; chk("encode/decode", V8::decode(bb) == c); }

  printf("test_bitvec: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
