// Float_T<Ctx,W> over ClearCtx: arithmetic / comparison / classify / sign ops,
// all replaying the fp<W>_<op>.empbc builtins through the context.
#include "emp-tool/circuits/float.h"
#include "emp-tool/context/clear.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }
static float rdf(const Float_T<ClearCtx, 32>& f) {
  uint32_t bits = 0; for (int i = 0; i < 32; ++i) bits |= (uint32_t)(f.w[i] & 1) << i;
  float r; std::memcpy(&r, &bits, 4); return r;
}

int main() {
  ClearCtx cx;
  using F = Float_T<ClearCtx, 32>;
  F a = F::constant(cx, 1.5f), b = F::constant(cx, 2.25f);

  chk("add", rdf(a + b) == 3.75f);
  chk("sub", rdf(b - a) == 0.75f);
  chk("mul", rdf(a * b) == 3.375f);
  chk("div", rdf(b / a) == 1.5f);
  chk("min", rdf(a.min(b)) == 1.5f);
  chk("max", rdf(a.max(b)) == 2.25f);

  chk("lt", ((a < b).w & 1) == 1);
  chk("gt", ((a > b).w & 1) == 0);
  chk("eq", ((a == a).w & 1) == 1);
  chk("ne", ((a != b).w & 1) == 1);

  chk("abs", rdf((-a).abs()) == 1.5f);
  chk("neg", rdf(-a) == -1.5f);
  chk("is_nan", ((a.is_nan()).w & 1) == 0);
  chk("is_zero", ((F::constant(cx, 0.0f).is_zero()).w & 1) == 1);

  { auto e = F::encode(3.75f); bool bb[32]; for (int i = 0; i < 32; ++i) bb[i] = e[i]; chk("encode/decode", F::decode(bb) == 3.75f); }

  printf("test_float: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
