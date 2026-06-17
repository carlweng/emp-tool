// Bit_T<Ctx>: the boolean gate operators and select, exercised through a
// ClearSession — inputs are fed, ops applied, and results revealed (the user
// flow); the values themselves are pure circuit values.
#include "emp-tool/circuits/bit.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <cstdio>
using namespace emp;
using Ctx = ClearSession::ctx_t;
using B   = Bit_T<Ctx>;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

// ---- example: how a user feeds inputs, computes, and reveals --------------

static void example() {
  ClearSession sess;
  B a = sess.input<B>(ALICE, true);
  B b = sess.input<B>(BOB, false);
  chk("example and", sess.reveal(a & b, PUBLIC).value() == false);  // 1 & 0
  chk("example xor", sess.reveal(a ^ b, PUBLIC).value() == true);   // 1 ^ 0
  chk("example sel", sess.reveal(a.select(a, b), PUBLIC).value() == false);  // a ? b : a — a true, picks b
}

int main() {
  example();

  ClearSession sess;
  Ctx& cx = sess.ctx();                              // raw ctx for public constants
  B t = B::constant(cx, true), f = B::constant(cx, false);

  for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y) {
      B a = sess.input<B>(ALICE, (bool)x);
      B b = sess.input<B>(BOB, (bool)y);
      chk("and", sess.reveal(a & b, PUBLIC).value() == (bool)(x & y));
      chk("xor", sess.reveal(a ^ b, PUBLIC).value() == (bool)(x ^ y));
      chk("or",  sess.reveal(a | b, PUBLIC).value() == (bool)(x | y));
      chk("eq",  sess.reveal(a == b, PUBLIC).value() == (x == y));
      chk("ne",  sess.reveal(a != b, PUBLIC).value() == (x != y));
      chk("sel", sess.reveal(b.select(a, t), PUBLIC).value() == (bool)(x ? 1 : y));  // a ? t : b
    }
  chk("not", sess.reveal(!t, PUBLIC).value() == false &&
             sess.reveal(!f, PUBLIC).value() == true);

  { auto e = B::encode(true); bool bb[1] = {(bool)e[0]}; chk("encode/decode", B::decode(bb) == true); }

  printf("test_bit: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
