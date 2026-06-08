// SHA-256 over the BooleanContext value layer: the "abc" vector on ClearCtx, a
// fixed 256-bit-message wrapper for record/replay equivalence and the gate count.
#include "emp-tool/circuits/crypto/sha256.h"
#include "test_crypto_common.h"
#include <cstdio>
#include <type_traits>
using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;

int main() {
  // 24-bit message "abc" -> 256-bit digest (known vector).
  auto drv24 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using B = Bit_T<Ctx>;
    B msg[24], dig[256];
    for (int i = 0; i < 24; ++i) msg[i] = B(ctx, in[i]);
    sha256<Ctx, 24>(ctx, dig, msg);
    for (int i = 0; i < 256; ++i) out[i] = dig[i].w;
  };
  // Fixed 256-bit message (one padded block) for the circuit-shape checks.
  auto drv256 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using B = Bit_T<Ctx>;
    B msg[256], dig[256];
    for (int i = 0; i < 256; ++i) msg[i] = B(ctx, in[i]);
    sha256<Ctx, 256>(ctx, dig, msg);
    for (int i = 0; i < 256; ++i) out[i] = dig[i].w;
  };

  std::vector<uint8_t> abc = bits_from_hex("616263");   // "abc"
  const std::string want =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  std::vector<uint8_t> cl = clear_run(drv24, abc, 256);
  bool vec = (hex_from_bits(cl) == want);

  std::vector<uint8_t> msg256(256);
  for (int i = 0; i < 256; ++i) msg256[i] = (uint8_t)((i * 5 + 1) & 1);
  std::vector<uint8_t> live = clear_run(drv256, msg256, 256);
  std::vector<uint8_t> rep  = record_replay(drv256, msg256, 256);
  uint64_t ands = count_ands(drv256, 256, 256);

  bool rok = (rep == live);
  bool cnt = (ands == 24744);
  bool ok = vec && rok && cnt;
  printf("crypto SHA-256: abc=%s (want ba7816bf...) record==live=%d ands=%llu (exp 24744)  %s\n",
         hex_from_bits(cl).substr(0, 16).c_str(), (int)rok,
         (unsigned long long)ands, ok ? "GOOD!" : "BAD!");
  printf("test_crypto_sha256: %s\n", ok ? "GOOD!" : "BAD!");
  return ok ? 0 : 1;
}
