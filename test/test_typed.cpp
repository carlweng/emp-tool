// Typed values over a BooleanContext (emp-tool/circuits/typed.h): exercise
// UInt/SignedInt arithmetic+compare and Float arithmetic (IR replay) on
// ClearContext vs host, plus a record->replay round-trip. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/typed.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

using namespace emp;
namespace ckt = emp::circuit;

static int bad = 0;
static void chk(const char* what, bool ok) { if (!ok) { printf("  [FAIL] %s\n", what); ++bad; } }

template <int N, class V> static uint64_t read_bits(const V& v) {
    uint64_t r = 0; for (int i = 0; i < N; ++i) r |= (uint64_t)(v.w[i] & 1) << i; return r;
}

int main() {
    ClearContext cx;
    const uint32_t A = 0xDEADBEEF, B = 0x12345678;

    // ---- UInt32 arithmetic / compare vs host ----
    {
        auto a = UInt<ClearContext, 32>::constant(cx, A);
        auto b = UInt<ClearContext, 32>::constant(cx, B);
        chk("uint +",  read_bits<32>(a + b) == (uint32_t)(A + B));
        chk("uint -",  read_bits<32>(a - b) == (uint32_t)(A - B));
        chk("uint &",  read_bits<32>(a & b) == (A & B));
        chk("uint ^",  read_bits<32>(a ^ b) == (A ^ B));
        chk("uint |",  read_bits<32>(a | b) == (A | B));
        chk("uint ~",  read_bits<32>(~a) == (uint32_t)(~A));
        chk("uint ==", ((a == a).w & 1) == 1 && ((a == b).w & 1) == 0);
        chk("uint <",  ((b < a).w & 1) == (B < A) && ((a < b).w & 1) == (A < B));
        chk("uint sel", read_bits<32>(a.select(Bit<ClearContext>::constant(cx, true), b)) == B);
        chk("uint *",  read_bits<32>(a * b) == (uint32_t)(A * B));
        chk("uint /",  read_bits<32>(a / b) == (uint32_t)(A / B));
        chk("uint %",  read_bits<32>(a % b) == (uint32_t)(A % B));
    }

    // ---- SignedInt32: +, -, unary-, signed < ----
    {
        const int32_t X = -1000000, Y = 2000000;
        auto x = Int<ClearContext, 32>::constant(cx, X);
        auto y = Int<ClearContext, 32>::constant(cx, Y);
        chk("int +", (int32_t)read_bits<32>(x + y) == (int32_t)(X + Y));
        chk("int -", (int32_t)read_bits<32>(x - y) == (int32_t)(X - Y));
        chk("int neg", (int32_t)read_bits<32>(-x) == -X);
        chk("int < (neg<pos)", ((x < y).w & 1) == 1);
        chk("int < (pos<neg)", ((y < x).w & 1) == 0);
    }

    // ---- Float32 arithmetic via IR replay through the context ----
    {
        auto bits = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return (uint64_t)u; };
        auto tofl = [](uint64_t u32) { float f; uint32_t u = (uint32_t)u32; std::memcpy(&f, &u, 4); return f; };
        auto fa = Float<ClearContext, 32>::from_bits(cx, bits(1.5f));
        auto fb = Float<ClearContext, 32>::from_bits(cx, bits(2.25f));
        auto f2 = Float<ClearContext, 32>::from_bits(cx, bits(2.0f));
        auto f4 = Float<ClearContext, 32>::from_bits(cx, bits(4.0f));
        auto fz = Float<ClearContext, 32>::from_bits(cx, bits(0.0f));
        auto fn = Float<ClearContext, 32>::from_bits(cx, bits(-1.5f));
        chk("float +", tofl(read_bits<32>(fa + fb)) == 3.75f);
        chk("float *", tofl(read_bits<32>(fa * fb)) == 3.375f);
        chk("float -", tofl(read_bits<32>(fb - fa)) == 0.75f);
        chk("float /", tofl(read_bits<32>(fb / fa)) == 1.5f);
        chk("float min", tofl(read_bits<32>(fa.min(fb))) == 1.5f);
        chk("float max", tofl(read_bits<32>(fa.max(fb))) == 2.25f);
        chk("float sqr", tofl(read_bits<32>(fa.sqr())) == 2.25f);
        chk("float sqrt", tofl(read_bits<32>(f4.sqrt())) == 2.0f);
        chk("float recip", tofl(read_bits<32>(f2.recip())) == 0.5f);
        chk("float rsqrt", tofl(read_bits<32>(f4.rsqrt())) == 0.5f);
        chk("float fma", tofl(read_bits<32>(fa.fma(fb, f4))) == 1.5f * 2.25f + 4.0f);
        // comparisons -> bit
        chk("float lt", ((fa.less_than(fb)).w & 1) == 1 && ((fb.less_than(fa)).w & 1) == 0);
        chk("float le", ((fa.less_equal(fa)).w & 1) == 1);
        chk("float gt", ((fb.greater_than(fa)).w & 1) == 1);
        chk("float ge", ((fa.greater_equal(fa)).w & 1) == 1);
        chk("float eq", ((fa.equal(fa)).w & 1) == 1 && ((fa.equal(fb)).w & 1) == 0);
        chk("float ne", ((fa.not_equal(fb)).w & 1) == 1);
        // classifiers
        chk("float iszero", ((fz.is_zero()).w & 1) == 1 && ((fa.is_zero()).w & 1) == 0);
        chk("float isinf", ((fa.is_inf()).w & 1) == 0);
        chk("float isnan", ((fa.is_nan()).w & 1) == 0);
        // sign-bit ops + select
        chk("float abs", tofl(read_bits<32>(fn.abs())) == 1.5f);
        chk("float neg", tofl(read_bits<32>(-fa)) == -1.5f);
        chk("float copysign", tofl(read_bits<32>(fa.copysign(fn))) == -1.5f);
        chk("float select T", tofl(read_bits<32>(fa.select(Bit<ClearContext>::constant(cx, true), fb))) == 2.25f);
        chk("float select F", tofl(read_bits<32>(fa.select(Bit<ClearContext>::constant(cx, false), fb))) == 1.5f);

        // host clear codec (FloatTraits) + constant()
        std::vector<bool> e = Float<ClearContext, 32>::encode(3.75f);
        bool eb[32]; for (int i = 0; i < 32; ++i) eb[i] = e[i];
        chk("float codec roundtrip", Float<ClearContext, 32>::decode(eb) == 3.75f);
        auto ca = Float<ClearContext, 32>::constant(cx, 1.5f);
        auto cb = Float<ClearContext, 32>::constant(cx, 2.25f);
        auto cc = ca + cb;
        bool cbits[32]; for (int i = 0; i < 32; ++i) cbits[i] = cc.w[i] & 1;
        chk("float constant + host decode", Float<ClearContext, 32>::decode(cbits) == 3.75f);
    }

    // ---- record a typed UInt circuit, replay it on real inputs ----
    {
        RecordContext rc;
        uint32_t base = rc.external_input(64);
        uint32_t aw[32], bw[32];
        for (int i = 0; i < 32; ++i) { aw[i] = base + i; bw[i] = base + 32 + i; }
        auto ra = UInt<RecordContext, 32>::from_wires(rc, aw);
        auto rb = UInt<RecordContext, 32>::from_wires(rc, bw);
        auto rsum = ra + rb;
        uint32_t outw[32]; rsum.pack_wires(outw);
        ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(outw, 32));

        std::array<uint8_t, 64> in{};
        for (int i = 0; i < 32; ++i) { in[i] = (A >> i) & 1; in[32 + i] = (B >> i) & 1; }
        ClearContext cx2;
        std::vector<uint8_t> out = execute_program(cx2, prog, std::span<const uint8_t>(in.data(), 64));
        uint32_t res = 0; for (int i = 0; i < 32; ++i) res |= (uint32_t)(out[i] & 1) << i;
        chk("record/replay uint +", res == (uint32_t)(A + B));
    }

    printf("test_typed: %s\n", bad ? "FAILED" : "typed values over Ctx — PASS");
    return bad ? 1 : 0;
}
