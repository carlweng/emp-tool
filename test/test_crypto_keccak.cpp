// Keccak-f[1600] / SHA3-256 over the BooleanContext value layer: the SHA3-256("")
// vector on ClearCtx, plus a fixed 256-bit-message wrapper for record/replay
// equivalence and the gate count.
#include "emp-tool/circuits/crypto/keccak.h"
#include "test_crypto_common.h"
#include <cstdio>
#include <type_traits>
using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;

int main() {
  // Empty message (known vector).
  auto drv0 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using B = Bit_T<Ctx>;
    (void)in;
    B dig[256], dummy[1];
    sha3_256<Ctx, 0>(ctx, dig, dummy);
    for (int i = 0; i < 256; ++i) out[i] = dig[i].w;
  };
  // Fixed 256-bit message (one Keccak-f) for the circuit-shape checks.
  auto drv256 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using B = Bit_T<Ctx>;
    B msg[256], dig[256];
    for (int i = 0; i < 256; ++i) msg[i] = B(ctx, in[i]);
    sha3_256<Ctx, 256>(ctx, dig, msg);
    for (int i = 0; i < 256; ++i) out[i] = dig[i].w;
  };

  const std::string want =
      "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a";
  std::vector<uint8_t> cl = clear_run(drv0, {}, 256);
  bool vec = (hex_from_bits(cl) == want);

  std::vector<uint8_t> msg256(256);
  for (int i = 0; i < 256; ++i) msg256[i] = (uint8_t)((i * 3 + 2) & 1);
  std::vector<uint8_t> live = clear_run(drv256, msg256, 256);
  std::vector<uint8_t> rep  = record_replay(drv256, msg256, 256);
  uint64_t ands = count_ands(drv256, 256, 256);

  bool rok = (rep == live);
  bool cnt = (ands == 38400);
  bool ok = vec && rok && cnt;
  printf("crypto SHA3-256: empty=%s (want a7ffc6f8...) record==live=%d ands=%llu (exp 38400)  %s\n",
         hex_from_bits(cl).substr(0, 16).c_str(), (int)rok,
         (unsigned long long)ands, ok ? "GOOD!" : "BAD!");
  printf("test_crypto_keccak: %s\n", ok ? "GOOD!" : "BAD!");
  return ok ? 0 : 1;
}
