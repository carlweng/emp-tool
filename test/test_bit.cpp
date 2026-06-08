// Bit_T<Ctx> over ClearCtx: the boolean gate operators and select.
#include "emp-tool/circuits/bit.h"
#include "emp-tool/context/clear.h"
#include <cstdio>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

int main() {
  ClearCtx cx;
  using B = Bit_T<ClearCtx>;
  B t = B::constant(cx, true), f = B::constant(cx, false);

  for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y) {
      B a = B::constant(cx, x), b = B::constant(cx, y);
      chk("and", ((a & b).w & 1) == (x & y));
      chk("xor", ((a ^ b).w & 1) == (x ^ y));
      chk("or",  ((a | b).w & 1) == (x | y));
      chk("eq",  ((a == b).w & 1) == (x == y));
      chk("ne",  ((a != b).w & 1) == (x != y));
      chk("sel", (b.select(a, t).w & 1) == (x ? 1 : y));   // a ? t : b
    }
  chk("not", ((!t).w & 1) == 0 && ((!f).w & 1) == 1);
  { auto e = B::encode(true); bool bb[1] = {(bool)e[0]}; chk("encode/decode", B::decode(bb) == true); }

  printf("test_bit: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
