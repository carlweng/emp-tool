// Dispatch-strategy benchmark. Measures the cost of three ways to execute the
// same boolean circuit — virtual emp::Backend, static BooleanContext (inlined),
// and recorded-IR replay — over mul64 (controlled, all arms identical
// source/circuit) and AES-128 / SHA-256 (existing kernels, virtual baseline +
// IR replay). Runs entirely on the canonical BooleanContext surface
// (circuits/context.h); the superseded experimental/ prototype is gone.
//
// Fairness: the controlled virtual arm uses NoFoldClearBackend (same uint8 bit
// semantics as ClearContext, NO constant folding), so virtual-vs-static differs
// only in dispatch. Throughput is normalized by the LOGICAL gate count from the
// recorded BooleanProgram, never ClearBackend::num_and() (which folds).

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/context.h"          // ClearContext, BackendContext, execute_program
#include "emp-tool/circuits/aes_circuit.h"
#include "emp-tool/circuits/sha256_circuit.h"
#include "emp-tool/frontend/record_backend.h"
#include "emp-tool/execution/clear_backend.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <vector>

using namespace emp;
using emp::frontend::RecordBackend;
using emp::frontend::RecWire;
namespace ckt = emp::circuit;
using clk = std::chrono::high_resolution_clock;

// --- the controlled virtual baseline: same uint8 semantics as ClearContext, NO
// constant folding, so every gate is a real virtual call. wire_bytes()==1. ---
namespace {
class NoFoldClearBackend : public Backend {
public:
    uint64_t ands = 0;
    NoFoldClearBackend() : Backend(PUBLIC) {}
    size_t wire_bytes() const override { return 1; }
    void public_label(void* out, bool b) override { *(uint8_t*)out = b ? 1 : 0; }
    void and_gate(void* o, const void* l, const void* r) override {
        *(uint8_t*)o = *(const uint8_t*)l & *(const uint8_t*)r; ++ands;
    }
    void xor_gate(void* o, const void* l, const void* r) override {
        *(uint8_t*)o = *(const uint8_t*)l ^ *(const uint8_t*)r;
    }
    void not_gate(void* o, const void* in) override { *(uint8_t*)o = *(const uint8_t*)in ^ 1u; }
    void feed(void* out, int, const bool* in, size_t n) override {
        for (size_t i = 0; i < n; ++i) ((uint8_t*)out)[i] = in[i] ? 1 : 0;
    }
    void reveal(bool* out, int, const void* in, size_t n) override {
        for (size_t i = 0; i < n; ++i) out[i] = ((const uint8_t*)in)[i] != 0;
    }
    uint64_t num_and() override { return ands; }
};

// 64x64 -> 128 schoolbook unsigned multiply, written once against the
// BooleanContext concept so the SAME source/circuit runs through every dispatch
// arm (static ClearContext, virtual BackendContext, and IR replay once recorded
// through RecordContext). No folding: every gate is materialized identically.
template <class Ctx>
inline void mul64(Ctx& ctx, const typename Ctx::Wire a[64],
                  const typename Ctx::Wire b[64], typename Ctx::Wire out[128]) {
    using W = typename Ctx::Wire;
    W zero = ctx.public_bit(false);
    W acc[128];
    for (int i = 0; i < 128; ++i) acc[i] = zero;
    for (int i = 0; i < 64; ++i) {
        W carry = zero;
        for (int j = 0; j < 64; ++j) {
            W pp  = ctx.and_gate(a[j], b[i]);
            W s1  = ctx.xor_gate(acc[i + j], pp);
            W sum = ctx.xor_gate(s1, carry);
            W c1  = ctx.and_gate(acc[i + j], pp);
            W c2  = ctx.and_gate(s1, carry);
            carry = ctx.xor_gate(c1, c2);              // c1,c2 disjoint -> OR == XOR
            acc[i + j] = sum;
        }
        for (int k = i + 64; k < 128; ++k) {           // ripple final carry up
            W s = ctx.xor_gate(acc[k], carry);
            carry = ctx.and_gate(acc[k], carry);
            acc[k] = s;
        }
    }
    for (int i = 0; i < 128; ++i) out[i] = acc[i];
}
}  // namespace

// Adaptive throughput: 32 warm-ups, then scale iters until >= `seconds`.
template <typename Fn>
static double run_for(double seconds, Fn&& fn, void* clob) {
    for (int i = 0; i < 32; ++i) { fn(); asm volatile("" : "+m"(*(char*)clob) : : "memory"); }
    int64_t iters = 64;
    while (true) {
        auto a = clk::now();
        for (int64_t i = 0; i < iters; ++i) { fn(); asm volatile("" : "+m"(*(char*)clob) : : "memory"); }
        double el = std::chrono::duration<double>(clk::now() - a).count();
        if (el >= seconds) return double(iters) / el;
        iters *= 2;
    }
}

static uint64_t count_and(const ckt::BooleanProgram& p) {
    uint64_t n = 0; for (const auto& g : p.gates) if (g.op == ckt::Op::And) ++n; return n;
}

// IR replay through any context: writes into ws.out, returns it (no per-call alloc).
template <class Ctx>
static const std::vector<uint8_t>& replay(Ctx& ctx, const ckt::BooleanProgram& p,
                                          const uint8_t* in, ProgramWorkspace<uint8_t>& ws) {
    return execute_program(ctx, p, std::span<const uint8_t>(in, p.num_inputs), ws);
}

// ----- recording (the IDENTICAL circuits the arms run) --------------------
static ckt::BooleanProgram record_mul64() {
    RecordContext rc;
    uint32_t base = rc.external_input(128);
    uint32_t a[64], b[64], out[128];
    for (int i = 0; i < 64; ++i) { a[i] = base + i; b[i] = base + 64 + i; }
    mul64(rc, a, b, out);
    rc.finish(std::span<const uint32_t>(out, 128));   // sets outputs/dims + validates
    return std::move(rc.prog);
}

static ckt::BooleanProgram record_aes() {
    RecordBackend rec;
    Backend* saved = backend; backend = &rec;
    int base = rec.external_input(256);            // 128 plaintext ‖ 128 key
    Bit_T<RecWire> pt[128], key[128], ct[128];
    for (int i = 0; i < 128; ++i) pt[i].bit  = RecWire(base + i);
    for (int i = 0; i < 128; ++i) key[i].bit = RecWire(base + 128 + i);
    AES_Calculator_T<RecWire> aes;
    aes.encrypt_with_key(pt, key, ct);
    rec.finalize();
    ckt::BooleanProgram prog = std::move(rec.prog);
    prog.outputs.clear();
    for (int i = 0; i < 128; ++i) prog.outputs.push_back((uint32_t)ct[i].bit.id);
    backend = saved;
    ckt::validate_program(prog);
    return prog;
}

static ckt::BooleanProgram record_sha(int msg_bits) {
    RecordBackend rec;
    Backend* saved = backend; backend = &rec;
    int base = rec.external_input(msg_bits);
    std::vector<Bit_T<RecWire>> in(msg_bits);
    for (int i = 0; i < msg_bits; ++i) in[i].bit = RecWire(base + i);
    Bit_T<RecWire> out[256];
    SHA256_Calculator_T<RecWire> calc;
    calc.sha256(out, in.data(), msg_bits);
    rec.finalize();
    ckt::BooleanProgram prog = std::move(rec.prog);
    prog.outputs.clear();
    for (int i = 0; i < 256; ++i) prog.outputs.push_back((uint32_t)out[i].bit.id);
    backend = saved;
    ckt::validate_program(prog);
    return prog;
}

// ----- live (existing-kernel) reference + baseline ------------------------
static void live_aes(const bool in[256], bool out[128], uint64_t* ands) {
    ClearBackend cb; Backend* saved = backend; backend = &cb;
    Bit_T<ClearWire> pt[128], key[128], ct[128];
    for (int i = 0; i < 128; ++i) pt[i]  = Bit_T<ClearWire>(in[i], ALICE);
    for (int i = 0; i < 128; ++i) key[i] = Bit_T<ClearWire>(in[128 + i], ALICE);
    AES_Calculator_T<ClearWire> aes;
    aes.encrypt_with_key(pt, key, ct);
    for (int i = 0; i < 128; ++i) out[i] = ct[i].template reveal<bool>(PUBLIC);
    if (ands) *ands = cb.num_and();
    backend = saved;
}

static void live_sha(const bool* in, int msg_bits, bool out[256], uint64_t* ands) {
    ClearBackend cb; Backend* saved = backend; backend = &cb;
    std::vector<Bit_T<ClearWire>> bin(msg_bits);
    for (int i = 0; i < msg_bits; ++i) bin[i] = Bit_T<ClearWire>(in[i], ALICE);
    Bit_T<ClearWire> bout[256];
    SHA256_Calculator_T<ClearWire> calc;
    calc.sha256(bout, bin.data(), msg_bits);
    for (int i = 0; i < 256; ++i) out[i] = bout[i].template reveal<bool>(PUBLIC);
    if (ands) *ands = cb.num_and();
    backend = saved;
}

struct Row { const char* kernel; const char* arm; uint64_t gates, ands; double gps, aps; };
static std::vector<Row> rows;
static void add_row(const char* k, const char* arm, const ckt::BooleanProgram& p, double ops) {
    uint64_t g = p.gates.size(), a = count_and(p);
    rows.push_back({k, arm, g, a, ops * g, ops * a});
}

int main() {
    std::mt19937_64 rng(0xC0FFEE);
    NoFoldClearBackend nf;
    ClearContext cc_ir;                      // reused IR-static context
    BackendContext<uint8_t> bc_ir(&nf);      // reused IR-virtual context
    ProgramWorkspace<uint8_t> ws_s, ws_v;    // reused replay workspaces

    // ===================== mul64 (full controlled matrix) =====================
    ckt::BooleanProgram pm = record_mul64();
    printf("mul64: %zu gates (%llu AND), %u inputs, %zu outputs\n",
           pm.gates.size(), (unsigned long long)count_and(pm), pm.num_inputs, pm.outputs.size());

    uint64_t a_val = rng(), b_val = rng();
    uint8_t a8[64], b8[64], in_m[128];
    for (int i = 0; i < 64; ++i) { a8[i] = (a_val >> i) & 1; b8[i] = (b_val >> i) & 1; }
    for (int i = 0; i < 64; ++i) { in_m[i] = a8[i]; in_m[64 + i] = b8[i]; }

    auto recon = [](const uint8_t o[128]) {
        __uint128_t v = 0; for (int i = 0; i < 128; ++i) v |= (__uint128_t)(o[i] & 1) << i; return v;
    };
    __uint128_t ref = (__uint128_t)a_val * (__uint128_t)b_val;
    int bad = 0;

    uint8_t out_s[128];
    { ClearContext cc; mul64(cc, a8, b8, out_s); if (recon(out_s) != ref) { printf("  [FAIL] mul static\n"); bad++; } }

    uint8_t out_v[128];
    { BackendContext<uint8_t> bc(&nf); mul64(bc, a8, b8, out_v); if (recon(out_v) != ref) { printf("  [FAIL] mul virtual\n"); bad++; } }

    if (recon(replay(cc_ir, pm, in_m, ws_s).data()) != ref) { printf("  [FAIL] mul IR-static\n"); bad++; }
    if (recon(replay(bc_ir, pm, in_m, ws_v).data()) != ref) { printf("  [FAIL] mul IR-virtual\n"); bad++; }

    add_row("mul64", "static",     pm, run_for(0.5, [&]{ ClearContext cc; mul64(cc, a8, b8, out_s); }, out_s));
    add_row("mul64", "virtual",    pm, run_for(0.5, [&]{ BackendContext<uint8_t> bc(&nf); mul64(bc, a8, b8, out_v); }, out_v));
    add_row("mul64", "IR-static",  pm, run_for(0.5, [&]{ replay(cc_ir, pm, in_m, ws_s); }, ws_s.out.data()));
    add_row("mul64", "IR-virtual", pm, run_for(0.5, [&]{ replay(bc_ir, pm, in_m, ws_v); }, ws_v.out.data()));

    // ===================== AES-128 (baseline + IR) =====================
    ckt::BooleanProgram pa = record_aes();
    printf("aes128: %zu gates (%llu AND), %u inputs, %zu outputs\n",
           pa.gates.size(), (unsigned long long)count_and(pa), pa.num_inputs, pa.outputs.size());
    bool ain[256]; for (int i = 0; i < 256; ++i) ain[i] = rng() & 1;
    bool aout_live[128]; uint64_t a_fold = 0; live_aes(ain, aout_live, &a_fold);
    uint8_t ain8[256]; for (int i = 0; i < 256; ++i) ain8[i] = ain[i];
    if (true) { const auto& o = replay(cc_ir, pa, ain8, ws_s);
        for (int i = 0; i < 128; ++i) if ((o[i] & 1) != aout_live[i]) { printf("  [FAIL] AES IR-static != live @%d\n", i); bad++; break; } }
    if (true) { const auto& o = replay(bc_ir, pa, ain8, ws_v);
        for (int i = 0; i < 128; ++i) if ((o[i] & 1) != aout_live[i]) { printf("  [FAIL] AES IR-virtual != live @%d\n", i); bad++; break; } }

    { // baseline (live kernel via real ClearBackend; folds — anchor only)
        ClearBackend cb; Backend* saved = backend; backend = &cb;
        Bit_T<ClearWire> pt[128], key[128], ct[128];
        for (int i = 0; i < 128; ++i) pt[i]  = Bit_T<ClearWire>(ain[i], ALICE);
        for (int i = 0; i < 128; ++i) key[i] = Bit_T<ClearWire>(ain[128 + i], ALICE);
        AES_Calculator_T<ClearWire> aes;
        double ops = run_for(0.5, [&]{ aes.encrypt_with_key(pt, key, ct); }, &ct[0].bit);
        backend = saved;
        add_row("aes128", "baseline*", pa, ops);
    }
    add_row("aes128", "IR-static",  pa, run_for(0.5, [&]{ replay(cc_ir, pa, ain8, ws_s); }, ws_s.out.data()));
    add_row("aes128", "IR-virtual", pa, run_for(0.5, [&]{ replay(bc_ir, pa, ain8, ws_v); }, ws_v.out.data()));
    printf("aes128 baseline folded-AND=%llu (vs %llu recorded)\n",
           (unsigned long long)a_fold, (unsigned long long)count_and(pa));

    // ===================== SHA-256 (baseline + IR) =====================
    const int MSG = 24;                       // one compression block (padded)
    ckt::BooleanProgram ps = record_sha(MSG);
    printf("sha256(%d-bit msg): %zu gates (%llu AND), %u inputs, %zu outputs\n",
           MSG, ps.gates.size(), (unsigned long long)count_and(ps), ps.num_inputs, ps.outputs.size());
    bool sin[MSG]; for (int i = 0; i < MSG; ++i) sin[i] = rng() & 1;
    bool sout_live[256]; uint64_t s_fold = 0; live_sha(sin, MSG, sout_live, &s_fold);
    uint8_t sin8[MSG]; for (int i = 0; i < MSG; ++i) sin8[i] = sin[i];
    if (true) { const auto& o = replay(cc_ir, ps, sin8, ws_s);
        for (int i = 0; i < 256; ++i) if ((o[i] & 1) != sout_live[i]) { printf("  [FAIL] SHA IR-static != live @%d\n", i); bad++; break; } }
    if (true) { const auto& o = replay(bc_ir, ps, sin8, ws_v);
        for (int i = 0; i < 256; ++i) if ((o[i] & 1) != sout_live[i]) { printf("  [FAIL] SHA IR-virtual != live @%d\n", i); bad++; break; } }

    { ClearBackend cb; Backend* saved = backend; backend = &cb;
      std::vector<Bit_T<ClearWire>> bin(MSG);
      for (int i = 0; i < MSG; ++i) bin[i] = Bit_T<ClearWire>(sin[i], ALICE);
      Bit_T<ClearWire> bout[256];
      SHA256_Calculator_T<ClearWire> calc;
      double ops = run_for(0.5, [&]{ calc.sha256(bout, bin.data(), MSG); }, bout);
      backend = saved;
      add_row("sha256", "baseline*", ps, ops);
    }
    add_row("sha256", "IR-static",  ps, run_for(0.5, [&]{ replay(cc_ir, ps, sin8, ws_s); }, ws_s.out.data()));
    add_row("sha256", "IR-virtual", ps, run_for(0.5, [&]{ replay(bc_ir, ps, sin8, ws_v); }, ws_v.out.data()));
    printf("sha256 baseline folded-AND=%llu (vs %llu recorded)\n",
           (unsigned long long)s_fold, (unsigned long long)count_and(ps));

    // ===================== table =====================
    printf("\ncorrectness gate: %s\n", bad ? "FAILED" : "PASS (all arms agree; mul vs host product)");
    printf("\n%-8s %-11s %12s %12s %14s %14s\n", "kernel", "arm", "gates", "AND", "Mgate/s", "MAND/s");
    printf("%-8s %-11s %12s %12s %14s %14s\n", "------", "---", "-----", "---", "-------", "------");
    for (const auto& r : rows)
        printf("%-8s %-11s %12llu %12llu %14.1f %14.1f\n", r.kernel, r.arm,
               (unsigned long long)r.gates, (unsigned long long)r.ands, r.gps / 1e6, r.aps / 1e6);
    printf("\n* baseline = existing Bit_T kernel via real ClearBackend (folds constants;\n"
           "  Mgate/s normalized by the RECORDED gate count, so it overstates — anchor only).\n"
           "  AES/SHA static-dispatch is NOT measured this cut; infer from mul64 static vs IR-static.\n");
    return bad ? 1 : 0;
}
