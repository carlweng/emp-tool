// Data-oblivious sort over circuit values (sort.h), checked on ClearCtx against
// std::sort. Covers a non-power-of-2 length with duplicates.
#include "emp-tool/circuits/sort.h"
#include "emp-tool/ir/context/clear.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace emp;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

template <int N, class Clear>
static void check_sort(const std::vector<Clear>& in, const char* name) {
  ClearCtx cx;
  using U = UInt_T<ClearCtx, N>;
  std::vector<U> v;
  for (auto x : in) v.push_back(U::constant(cx, (uint64_t)x));
  sort(v);
  std::vector<Clear> ref = in;
  std::sort(ref.begin(), ref.end());
  bool ok = true;
  for (size_t i = 0; i < ref.size(); ++i) {
    uint64_t r = 0; for (int j = 0; j < N; ++j) r |= (uint64_t)(v[i].w[j] & 1) << j;
    ok = ok && (r == (uint64_t)ref[i]);
  }
  chk(name, ok);
}

int main() {
  check_sort<16>(std::vector<uint16_t>{42, 7, 100, 3, 3, 255, 1, 99, 50, 12}, "uint16 x10");
  check_sort<8>(std::vector<uint8_t>{5, 4, 3, 2, 1}, "uint8 x5 reverse");
  check_sort<16>(std::vector<uint16_t>{1}, "singleton");
  check_sort<16>(std::vector<uint16_t>{9, 9, 9, 9}, "all-equal");

  // descending.
  {
    ClearCtx cx; using U = UInt_T<ClearCtx, 16>;
    std::vector<uint16_t> in{5, 1, 9, 3, 7};
    std::vector<U> v; for (auto x : in) v.push_back(U::constant(cx, x));
    sort(v, /*ascending=*/false);
    std::sort(in.rbegin(), in.rend());
    bool ok = true;
    for (size_t i = 0; i < in.size(); ++i) {
      uint64_t r = 0; for (int j = 0; j < 16; ++j) r |= (uint64_t)(v[i].w[j] & 1) << j;
      ok = ok && (r == in[i]);
    }
    chk("descending", ok);
  }

  // key + payload (data carried alongside the key).
  {
    ClearCtx cx; using U = UInt_T<ClearCtx, 16>;
    std::vector<U> keys, data;
    for (uint16_t x : {30, 10, 20}) keys.push_back(U::constant(cx, x));
    for (uint16_t x : {300, 100, 200}) data.push_back(U::constant(cx, x));
    sort_by_key(keys, data);                            // ascending by key
    uint16_t ek[] = {10, 20, 30}, ed[] = {100, 200, 300};
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
      uint64_t rk = 0, rd = 0;
      for (int j = 0; j < 16; ++j) { rk |= (uint64_t)(keys[i].w[j] & 1) << j; rd |= (uint64_t)(data[i].w[j] & 1) << j; }
      ok = ok && (rk == ek[i] && rd == ed[i]);
    }
    chk("key+payload", ok);
  }

  printf("test_sort: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
