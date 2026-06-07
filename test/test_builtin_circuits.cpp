// Verify the recorded large-circuit builtins (aes128 / sha256_256 / sha3_256_256)
// replay through a BooleanContext bit-for-bit identically to their live
// hand-written kernels (which are themselves FIPS/NIST-validated by their own
// tests). Proves the "big circuits -> IR replay" path is faithful. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/context.h"
#include "emp-tool/circuits/builtin_circuit_files.h"
#include "emp-tool/circuits/aes_circuit.h"
#include "emp-tool/circuits/sha256_circuit.h"
#include "emp-tool/circuits/sha3_256.h"
#include "emp-tool/execution/clear_backend.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <vector>

using namespace emp;
using namespace emp::legacy;
namespace ckt = emp::circuit;

static int bad = 0;

// Run `live` (a kernel built over the global ClearBackend) and the recorded
// builtin replayed through ClearCtx on the same random input; require equal.
static void check_builtin(const char* name, int nin, int nout,
                          const std::function<void(const std::vector<bool>&, std::vector<uint8_t>&)>& live) {
    static uint64_t s = 0x9E3779B97F4A7C15ull;
    std::vector<bool> in(nin);
    for (int i = 0; i < nin; ++i) { s = s * 6364136223846793005ull + 1442695040888963407ull; in[i] = (s >> 33) & 1; }

    std::vector<uint8_t> live_out(nout, 0);
    setup_clear_backend("");
    live(in, live_out);
    finalize_clear_backend();

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
        printf("  [FAIL] %s: live-diff=%d scheduled-diff=%d (of %d)\n", name, diff, sdiff, nout); ++bad;
    } else {
        printf("  [ok]   %s: %u gates; scalar==live, scheduled==scalar (%d-bit out)\n",
               name, (uint32_t)prog.gates.size(), nout);
    }
}

int main() {
    check_builtin("aes128", 256, 128, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        Bit_T<ClearWire> pt[128], key[128], ct[128];
        for (int i = 0; i < 128; ++i) backend->public_label(&pt[i].bit,  in[i]);
        for (int i = 0; i < 128; ++i) backend->public_label(&key[i].bit, in[128 + i]);
        AES_Calculator_T<ClearWire> aes; aes.encrypt_with_key(pt, key, ct);
        for (int i = 0; i < 128; ++i) out[i] = ct[i].bit.value & 1;
    });

    check_builtin("sha256_256", 256, 256, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        std::vector<Bit_T<ClearWire>> m(256);
        for (int i = 0; i < 256; ++i) backend->public_label(&m[i].bit, in[i]);
        Bit_T<ClearWire> o[256];
        SHA256_Calculator_T<ClearWire> calc; calc.sha256(o, m.data(), 256);
        for (int i = 0; i < 256; ++i) out[i] = o[i].bit.value & 1;
    });

    check_builtin("sha3_256_256", 256, 256, [](const std::vector<bool>& in, std::vector<uint8_t>& out) {
        std::vector<Bit_T<ClearWire>> m(256);
        for (int i = 0; i < 256; ++i) backend->public_label(&m[i].bit, in[i]);
        Bit_T<ClearWire> o[256];
        SHA3_256_Calculator_T<ClearWire> calc; calc.sha3_256(o, m.data(), (size_t)256);
        for (int i = 0; i < 256; ++i) out[i] = o[i].bit.value & 1;
    });

    printf("test_builtin_circuits: %s\n", bad ? "FAILED" : "all builtins replay == live kernels — PASS");
    return bad ? 1 : 0;
}
