// Verify the recorded large-circuit builtins (aes128 / sha256_256 / sha3_256_256)
// replay through a BooleanContext bit-for-bit identically to the BooleanContext-
// native crypto kernels (which are themselves FIPS/NIST-validated by test_crypto_*).
// Proves the "big circuits -> IR replay" path is faithful, and that the new kernels
// agree with the shipped assets. C++20.

#include "emp-tool/context/context.h"
#include "emp-tool/ir/builtins.h"
#include "emp-tool/circuits/crypto/aes128.h"
#include "emp-tool/circuits/crypto/sha256.h"
#include "emp-tool/circuits/crypto/keccak.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <vector>

using namespace emp;
using namespace emp::circuit::crypto;
namespace ckt = emp::circuit;

static int bad = 0;

// Run `live` (a kernel over ClearCtx) and the recorded builtin replayed through
// ClearCtx on the same random input; require equal (scalar and scheduled replay).
static void check_builtin(const char* name, int nin, int nout,
                          const std::function<void(const std::vector<bool>&, std::vector<uint8_t>&)>& live) {
    static uint64_t s = 0x9E3779B97F4A7C15ull;
    std::vector<bool> in(nin);
    for (int i = 0; i < nin; ++i) { s = s * 6364136223846793005ull + 1442695040888963407ull; in[i] = (s >> 33) & 1; }

    std::vector<uint8_t> live_out(nout, 0);
    live(in, live_out);

    const ckt::BooleanProgram& prog = ckt::builtin_circuit(name);
    if (prog.num_inputs != (uint32_t)nin || prog.outputs.size() != (size_t)nout) {
        printf("  [FAIL] %s: shape %u/%zu != %d/%d\n", name, prog.num_inputs, prog.outputs.size(), nin, nout);
        ++bad; return;
    }
    std::vector<uint8_t> rin(nin);
    for (int i = 0; i < nin; ++i) rin[i] = in[i];
    ClearCtx cx;
    std::vector<uint8_t> rout = execute_program(cx, prog, std::span<const uint8_t>(rin.data(), nin));

    int diff = 0;
    for (int i = 0; i < nout; ++i) if ((rout[i] & 1) != live_out[i]) ++diff;

    // scheduled (bulk, AND-layer-batched) replay must match the scalar one
    ClearCtx cx2;
    std::vector<uint8_t> sout = scheduled_execute_program(cx2, prog, std::span<const uint8_t>(rin.data(), nin));
    int sdiff = 0;
    for (int i = 0; i < nout; ++i) if ((sout[i] & 1) != (rout[i] & 1)) ++sdiff;

    if (diff || sdiff) {
        printf("  [FAIL] %s: kernel-diff=%d scheduled-diff=%d (of %d)\n", name, diff, sdiff, nout); ++bad;
    } else {
        printf("  [ok]   %s: %u gates; scalar==kernel, scheduled==scalar (%d-bit out)\n",
               name, (uint32_t)prog.gates.size(), nout);
    }
}

int main() {
    // aes128: 256 inputs = pt[0..127] ‖ key[128..255] -> 128-bit ciphertext.
    check_builtin("aes128", 256, 128, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        ClearCtx cx; using W = ClearCtx::Wire;
        W pt[128], key[128], ct[128];
        for (int i = 0; i < 128; ++i) pt[i]  = cx.public_bit(in[i]);
        for (int i = 0; i < 128; ++i) key[i] = cx.public_bit(in[128 + i]);
        aes128_encrypt<ClearCtx>(cx, pt, key, ct);
        for (int i = 0; i < 128; ++i) out[i] = ct[i] & 1;
    });

    check_builtin("sha256_256", 256, 256, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        ClearCtx cx; using B = Bit_T<ClearCtx>;
        B m[256], o[256];
        for (int i = 0; i < 256; ++i) m[i] = B(cx, in[i] ? 1 : 0);
        sha256<ClearCtx, 256>(cx, o, m);
        for (int i = 0; i < 256; ++i) out[i] = o[i].w & 1;
    });

    check_builtin("sha3_256_256", 256, 256, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        ClearCtx cx; using B = Bit_T<ClearCtx>;
        B m[256], o[256];
        for (int i = 0; i < 256; ++i) m[i] = B(cx, in[i] ? 1 : 0);
        sha3_256<ClearCtx, 256>(cx, o, m);
        for (int i = 0; i < 256; ++i) out[i] = o[i].w & 1;
    });

    printf("test_builtin_circuits: %s\n", bad ? "FAILED" : "all builtins replay == native kernels — PASS");
    return bad ? 1 : 0;
}
