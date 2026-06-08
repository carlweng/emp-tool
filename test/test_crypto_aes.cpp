// AES-128 block cipher over the BooleanContext value layer: FIPS-197 vector on
// ClearCtx, record/replay equivalence on RecordCtx, and the gate count on CountCtx.
#include "emp-tool/circuits/crypto/aes128.h"
#include "test_crypto_common.h"
#include <cstdio>
#include <type_traits>
using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;

int main() {
  // in = key[128] ++ pt[128]; out = ct[128].
  auto drv = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using W = typename Ctx::Wire;
    W key[128], pt[128], ct[128];
    for (int i = 0; i < 128; ++i) { key[i] = in[i]; pt[i] = in[128 + i]; }
    aes128_encrypt<Ctx>(ctx, pt, key, ct);
    for (int i = 0; i < 128; ++i) out[i] = ct[i];
  };

  // FIPS-197 Appendix B / C.1 example.
  std::vector<uint8_t> in = bits_from_hex("000102030405060708090a0b0c0d0e0f");
  std::vector<uint8_t> pt = bits_from_hex("00112233445566778899aabbccddeeff");
  in.insert(in.end(), pt.begin(), pt.end());
  const std::string want = "69c4e0d86a7b0430d8cdb78070b4c55a";

  std::vector<uint8_t> cl = clear_run(drv, in, 128);
  std::vector<uint8_t> rr = record_replay(drv, in, 128);
  uint64_t ands = count_ands(drv, 256, 128);

  bool vec = (hex_from_bits(cl) == want);
  bool rep = (rr == cl);
  bool cnt = (ands == 6400);

  // AES-128-CTR: the big-endian counter increment (unit-checked) plus the
  // keystream XOR loop, verified against the FIPS-validated block primitive.
  bool ctr_ok;
  {
    ClearCtx cx; using W = ClearCtx::Wire;
    auto rd = [](const W* a, int n) { std::vector<uint8_t> v(n); for (int i = 0; i < n; ++i) v[i] = a[i] & 1; return v; };

    // (a) inc by 1: a zero counter block -> byte 15 (BE LSB) == 0x01.
    W z[128]; for (int i = 0; i < 128; ++i) z[i] = cx.public_bit(false);
    ctr_inc_be64<ClearCtx>(cx, z, 1);
    bool inc_ok = (hex_from_bits(rd(z, 128)) == "00000000000000000000000000000001");
    // (b) carry: byte 15 == 0xff, +1 -> byte 15 = 0x00, byte 14 = 0x01.
    W c[128]; for (int i = 0; i < 128; ++i) c[i] = cx.public_bit(i >= 120);
    ctr_inc_be64<ClearCtx>(cx, c, 1);
    bool carry_ok = (hex_from_bits(rd(c, 128)) == "00000000000000000000000000000100");

    // (c) two-block CTR == pt ^ AES(counter_i), counter_1 = inc(counter_0).
    auto kb = bits_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    auto ivh = bits_from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto pth = bits_from_hex("6bc1bee22e409f96e93d7e117393172a"
                             "ae2d8a571e03ac9c9eb76fac45af8e51");   // 2 blocks
    W key[128], iv[128], pt[256], ct[256];
    for (int i = 0; i < 128; ++i) { key[i] = cx.public_bit(kb[i]); iv[i] = cx.public_bit(ivh[i]); }
    for (int i = 0; i < 256; ++i) pt[i] = cx.public_bit(pth[i]);
    aes128_ctr<ClearCtx>(cx, key, iv, pt, ct, 256, 0);
    W ctr0[128], ctr1[128], ks0[128], ks1[128];
    for (int i = 0; i < 128; ++i) { ctr0[i] = cx.public_bit(ivh[i]); ctr1[i] = cx.public_bit(ivh[i]); }
    ctr_inc_be64<ClearCtx>(cx, ctr1, 1);
    aes128_encrypt<ClearCtx>(cx, ctr0, key, ks0);
    aes128_encrypt<ClearCtx>(cx, ctr1, key, ks1);
    bool loop_ok = true;
    for (int i = 0; i < 128; ++i) loop_ok = loop_ok && ((ct[i] & 1) == (pth[i] ^ (ks0[i] & 1)));
    for (int i = 0; i < 128; ++i) loop_ok = loop_ok && ((ct[128 + i] & 1) == (pth[128 + i] ^ (ks1[i] & 1)));

    ctr_ok = inc_ok && carry_ok && loop_ok;
  }

  bool ok = vec && rep && cnt && ctr_ok;
  printf("crypto AES-128: clear=%s (want %s) record==live=%d ands=%llu (exp 6400) ctr=%d  %s\n",
         hex_from_bits(cl).c_str(), want.c_str(), (int)rep,
         (unsigned long long)ands, (int)ctr_ok, ok ? "GOOD!" : "BAD!");
  printf("test_crypto_aes: %s\n", ok ? "GOOD!" : "BAD!");
  return ok ? 0 : 1;
}
