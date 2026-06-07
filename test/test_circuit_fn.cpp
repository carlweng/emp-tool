// Cross-context portability of the BooleanContext circuit frontend: compile a
// circuit ONCE (through a RecordCtx) into a context-free Circuit, then run
// the SAME Circuit on ClearCtx. Exercises both body forms, a nullary
// circuit, the .constant() sugar, and an fp32 IR-replay body. The 32-bit adder
// is the size-optimal 31-AND kernel; recording is deterministic. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/context.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/frontend/circuit_fn.h"
#include "emp-tool/frontend/rec.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using namespace emp;
namespace cf = emp::frontend;

static int bad = 0;
static void chk(const char* what, bool ok) { if (!ok) { printf("  [FAIL] %s\n", what); ++bad; } }

// Decode a value evaluated on ClearCtx (Wire == uint8_t, 0/1) via its codec.
template <class V> static auto clear_of(const V& v) {
    constexpr int W = V::width();
    typename V::Wire wires[W]; v.pack_wires(wires);
    bool bits[W]; for (int i = 0; i < W; ++i) bits[i] = (bool)(wires[i] & 1);
    return V::decode(bits);
}

static uint64_t count_and(const circuit::BooleanProgram& p) {
    uint64_t n = 0; for (const auto& g : p.gates) if (g.op == circuit::Op::And) ++n; return n;
}

int main() {
    ClearCtx cx;

    // 1) compile once: 32-bit add (implicit-context form).
    auto add = [](auto a, auto b) { return a + b; };
    auto c = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);
    chk("add: dims", c.program().num_inputs == 64 && c.program().outputs.size() == 32);
    chk("add: AND == 31 (size-optimal adder)", count_and(c.program()) == 31);

    // run the SAME compiled circuit on ClearCtx, two input sets.
    auto run_add = [&](uint32_t A, uint32_t B) {
        auto x = UInt_T<ClearCtx, 32>::constant(cx, A);
        auto y = UInt_T<ClearCtx, 32>::constant(cx, B);
        return (uint32_t)clear_of(cf::run(cx, c, x, y));
    };
    chk("add run 1", run_add(12345678u, 87654321u) == (uint32_t)(12345678u + 87654321u));
    chk("add run 2", run_add(0xDEADBEEFu, 0x12345678u) == (uint32_t)(0xDEADBEEFu + 0x12345678u));

    // recording is deterministic: recompiling yields the identical program.
    auto c2 = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);
    chk("add: deterministic digest", digest_program(c.program()) == digest_program(c2.program()));

    // 2) explicit-context form: a*b + 1 (constant via ctx, no anchor needed).
    auto mul_plus1 = [](auto& ctx, auto a, auto b) {
        using C = std::remove_reference_t<decltype(ctx)>;
        return a * b + UInt_T<C, 32>::constant(ctx, 1);
    };
    auto cmp = cf::compile<rec::UInt<32>, rec::UInt<32>>(mul_plus1);
    {
        uint32_t A = 123456u, B = 789u;
        auto x = UInt_T<ClearCtx, 32>::constant(cx, A);
        auto y = UInt_T<ClearCtx, 32>::constant(cx, B);
        chk("explicit-ctx a*b+1", (uint32_t)clear_of(cf::run(cx, cmp, x, y)) == (uint32_t)(A * B + 1));
    }

    // 3) nullary circuit (explicit-context only): a public constant.
    auto kc = cf::compile<>([](auto& ctx) {
        using C = std::remove_reference_t<decltype(ctx)>;
        return UInt_T<C, 16>::constant(ctx, 4242);
    });
    chk("nullary const", (uint32_t)clear_of(cf::run(cx, kc)) == 4242u);

    // 4) .constant() sugar (implicit form): a + a.constant(7).
    auto add7 = [](auto a) { return a + a.constant(7); };
    auto ck = cf::compile<rec::UInt<32>>(add7);
    {
        uint32_t A = 1000u;
        auto x = UInt_T<ClearCtx, 32>::constant(cx, A);
        chk("implicit .constant", (uint32_t)clear_of(cf::run(cx, ck, x)) == A + 7u);
    }

    // 5) fp32 IR-replay body — compile once, run on ClearCtx (clear outputs).
    auto fadd = [](auto a, auto b) { return a + b; };
    auto cfp = cf::compile<rec::Float<32>, rec::Float<32>>(fadd);
    {
        auto x = Float_T<ClearCtx, 32>::constant(cx, 1.5f);
        auto y = Float_T<ClearCtx, 32>::constant(cx, 2.25f);
        chk("fp32 add (compiled replay)", clear_of(cf::run(cx, cfp, x, y)) == 3.75f);
    }

    // 6) live run (implicit form) agrees with compiled run.
    {
        auto x = UInt_T<ClearCtx, 32>::constant(cx, 5u);
        auto y = UInt_T<ClearCtx, 32>::constant(cx, 9u);
        chk("live run", (uint32_t)clear_of(cf::run(add, x, y)) == 14u);
    }

    // 7) Bits_T circuit (bit-vector value): nibble swap, compiled once, run on ClearCtx.
    {
        auto swap = [](auto b) { return b.template slice<4, 8>().concat(b.template slice<0, 4>()); };
        auto cbits = cf::compile<rec::Bits<8>>(swap);
        std::array<bool, 8> v{}; for (int i = 0; i < 8; ++i) v[i] = (0xB5u >> i) & 1;
        auto out = cf::run(cx, cbits, Bits_T<ClearCtx, 8>::constant(cx, v));
        uint32_t got = 0; for (int i = 0; i < 8; ++i) if (out.w[i] & 1) got |= (1u << i);
        chk("Bits_T nibble-swap circuit", got == 0x5Bu);   // 0xB5 -> 0x5B
    }

    printf("test_circuit_fn: %s\n", bad ? "FAILED" : "compile-once / run-anywhere — PASS");
    return bad ? 1 : 0;
}
