// Data-oblivious sort over circuit values (sort.h): values are fed and the
// sorted results revealed through a ClearSession, checked against std::sort.
// Covers a non-power-of-2 length with duplicates, descending, and key+payload.
#include "emp-tool/circuits/sort.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace emp;
using Ctx = ClearSession::ctx_t;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

// ---- example: feed values, sort obliviously, reveal the ordered result ----

static void example() {
  ClearSession sess;
  using U = UInt_T<Ctx, 16>;
  std::vector<U> v;
  for (uint16_t x : {42, 7, 100, 3}) v.push_back(sess.input<U>(ALICE, x));
  sort(v);
  uint16_t want[] = {3, 7, 42, 100};
  bool ok = true;
  for (int i = 0; i < 4; ++i) ok = ok && (sess.reveal(v[i], PUBLIC).value() == want[i]);
  chk("example sort", ok);
}

template <int N, class Clear>
static void check_sort(const std::vector<Clear>& in, const char* name) {
  ClearSession sess;
  using U = UInt_T<Ctx, N>;
  std::vector<U> v;
  for (auto x : in) v.push_back(sess.input<U>(ALICE, x));
  sort(v);
  std::vector<Clear> ref = in;
  std::sort(ref.begin(), ref.end());
  bool ok = true;
  for (size_t i = 0; i < ref.size(); ++i)
    ok = ok && ((uint64_t)sess.reveal(v[i], PUBLIC).value() == (uint64_t)ref[i]);
  chk(name, ok);
}

int main() {
  example();

  check_sort<16>(std::vector<uint16_t>{42, 7, 100, 3, 3, 255, 1, 99, 50, 12}, "uint16 x10");
  check_sort<8>(std::vector<uint8_t>{5, 4, 3, 2, 1}, "uint8 x5 reverse");
  check_sort<16>(std::vector<uint16_t>{1}, "singleton");
  check_sort<16>(std::vector<uint16_t>{9, 9, 9, 9}, "all-equal");

  // descending.
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    std::vector<uint16_t> in{5, 1, 9, 3, 7};
    std::vector<U> v; for (auto x : in) v.push_back(sess.input<U>(ALICE, x));
    sort(v, /*ascending=*/false);
    std::sort(in.rbegin(), in.rend());
    bool ok = true;
    for (size_t i = 0; i < in.size(); ++i)
      ok = ok && (sess.reveal(v[i], PUBLIC).value() == in[i]);
    chk("descending", ok);
  }

  // key + payload (data carried alongside the key).
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    std::vector<U> keys, data;
    for (uint16_t x : {30, 10, 20}) keys.push_back(sess.input<U>(ALICE, x));
    for (uint16_t x : {300, 100, 200}) data.push_back(sess.input<U>(ALICE, x));
    sort_by_key(keys, data);                            // ascending by key
    uint16_t ek[] = {10, 20, 30}, ed[] = {100, 200, 300};
    bool ok = true;
    for (int i = 0; i < 3; ++i)
      ok = ok && (sess.reveal(keys[i], PUBLIC).value() == ek[i] &&
                  sess.reveal(data[i], PUBLIC).value() == ed[i]);
    chk("key+payload", ok);
  }

  printf("test_sort: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
