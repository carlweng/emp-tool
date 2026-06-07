#ifndef EMP_CIRCUIT_TYPED_H__
#define EMP_CIRCUIT_TYPED_H__

// Typed circuit values over a BooleanContext (the "nice layer"): each value
// carries one Ctx* (private; reached via context()) plus its wires and overloads
// operators that issue value-return gates on the context — static dispatch, no
// global backend. The small structured arithmetic (ripple add/sub, comparators,
// mux) is the keep-templated kernel set, written here against bare Ctx::Wire.
// Large ops (Float arithmetic) replay the recorded .empbc through the context.
//
// Every value provides three contracts:
//   context:     context() -> Ctx*, context_type, shape (its context-free family)
//   wire layout: width(), pack_wires(out), from_wires(ctx, in)
//   clear codec: encode(clear) -> bits (LSB-first), decode(bits) -> clear
//                (forwarded to the value's shape; see circuits/shape.h)
// so a session can do input<T>(owner, clear) / reveal<T>(val, recipient) and the
// frontend can compile/replay generically. C++20.

#include "emp-tool/circuits/context.h"
#include "emp-tool/circuits/float.h"   // FloatTraits<W> host codec + circuit::float_circuit
#include "emp-tool/circuits/shape.h"   // BitShape/UIntShape/IntShape/FloatShape (+ codec)
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace emp {

// Float replays the fp<W>_<op>.empbc builtins via circuit::float_circuit
// (declared in float.h, defined in float_circuit_files.cpp) and uses
// FloatTraits<W> (in float.h) for its host clear codec.

// Mixing typed values from two different contexts (sessions/executors) silently
// corrupts — especially with id-based wires. The check defaults to DEBUG-ONLY
// (on when NDEBUG is unset). Opt in to a hard, always-on error() in release with
// -DEMP_CONTEXT_CHECKS=1; force it off with -DEMP_CONTEXT_CHECKS=0. When enabled
// it raises error() (not just assert), so it survives NDEBUG once opted in.
// It is a trivial pointer compare over the context() the value already stores.
#ifndef EMP_CONTEXT_CHECKS
#  ifndef NDEBUG
#    define EMP_CONTEXT_CHECKS 1
#  else
#    define EMP_CONTEXT_CHECKS 0
#  endif
#endif
template <class A, class B>
inline void check_same_context(const A& l, const B& r) {
#if EMP_CONTEXT_CHECKS
    if (l.context() != r.context()) error("typed value: operands belong to different contexts");
#endif
    assert(l.context() == r.context());
}

// ============================ keep-templated kernels =======================
// Bare-wire structured kernels (no Ctx* per bit). These are the "stay
// templated" set: small, inlineable, compiler-fusible. LSB-first throughout.
namespace kernel {

// OR via the free-XOR identity: a | b = a ^ b ^ (a & b) (one AND).
template <BooleanContext Ctx>
inline typename Ctx::Wire or_gate(Ctx& c, typename Ctx::Wire a, typename Ctx::Wire b) {
    return c.xor_gate(c.xor_gate(a, b), c.and_gate(a, b));
}

// The variable-length carry/borrow arithmetic — the size-optimal 1-AND-per-full-
// adder primitives (ported from numeric_kernels.h). `dest` may alias op1: each
// step reads op1[i]/op2[i] before writing dest[i].

// dest <- op1 + op2 + carryIn (mod 2^size). If carryOut != nullptr it receives
// the carry-out and the top bit is not folded into dest; otherwise the final
// carry is dropped and the top bit takes no AND (so an N-bit add is N-1 ANDs).
template <BooleanContext Ctx>
inline void add_full(Ctx& c, typename Ctx::Wire* dest, typename Ctx::Wire* carryOut,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2,
                     const typename Ctx::Wire* carryIn, int size) {
    using W = typename Ctx::Wire;
    if (size == 0) { if (carryIn && carryOut) *carryOut = *carryIn; return; }
    W carry = carryIn ? *carryIn : c.public_bit(false);
    int skipLast = (carryOut == nullptr) ? 1 : 0;
    int i = 0;
    while (size-- > skipLast) {
        W axc = c.xor_gate(op1[i], carry);
        W bxc = c.xor_gate(op2[i], carry);
        dest[i] = c.xor_gate(op1[i], bxc);
        W t = c.and_gate(axc, bxc);
        carry = c.xor_gate(carry, t);
        ++i;
    }
    if (carryOut) *carryOut = carry;
    else dest[i] = c.xor_gate(c.xor_gate(carry, op2[i]), op1[i]);
}

// dest <- op1 - op2 - borrowIn (mod 2^size). borrowOut as in add_full.
template <BooleanContext Ctx>
inline void sub_full(Ctx& c, typename Ctx::Wire* dest, typename Ctx::Wire* borrowOut,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2,
                     const typename Ctx::Wire* borrowIn, int size) {
    using W = typename Ctx::Wire;
    if (size == 0) { if (borrowIn && borrowOut) *borrowOut = *borrowIn; return; }
    W borrow = borrowIn ? *borrowIn : c.public_bit(false);
    int skipLast = (borrowOut == nullptr) ? 1 : 0;
    int i = 0;
    while (size-- > skipLast) {
        W bxa = c.xor_gate(op1[i], op2[i]);
        W bxc = c.xor_gate(borrow, op2[i]);
        dest[i] = c.xor_gate(bxa, borrow);
        W t = c.and_gate(bxa, bxc);
        borrow = c.xor_gate(borrow, t);
        ++i;
    }
    if (borrowOut) *borrowOut = borrow;
    else dest[i] = c.xor_gate(c.xor_gate(op1[i], op2[i]), borrow);
}

// out <- a + b (mod 2^N). Size-optimal: 1 AND per full adder, the unused MSB
// carry-out dropped -> N-1 ANDs (31 for N=32).
template <BooleanContext Ctx, int N>
inline void ripple_add(Ctx& c, const typename Ctx::Wire a[N],
                       const typename Ctx::Wire b[N], typename Ctx::Wire out[N]) {
    add_full<Ctx>(c, out, nullptr, a, b, nullptr, N);
}

// out <- a - b (mod 2^N). Same 1-AND/bit, MSB borrow dropped -> N-1 ANDs.
template <BooleanContext Ctx, int N>
inline void ripple_sub(Ctx& c, const typename Ctx::Wire a[N],
                       const typename Ctx::Wire b[N], typename Ctx::Wire out[N]) {
    sub_full<Ctx>(c, out, nullptr, a, b, nullptr, N);
}

// sel ? t : f, per wire: f ^ (sel & (t ^ f)) — one AND per bit.
template <BooleanContext Ctx>
inline typename Ctx::Wire mux(Ctx& c, typename Ctx::Wire sel,
                              typename Ctx::Wire t, typename Ctx::Wire f) {
    return c.xor_gate(f, c.and_gate(sel, c.xor_gate(t, f)));
}

// unsigned a < b: borrow out of (a - b). Returns a single wire.
template <BooleanContext Ctx, int N>
inline typename Ctx::Wire less_than(Ctx& c, const typename Ctx::Wire a[N],
                                    const typename Ctx::Wire b[N]) {
    using W = typename Ctx::Wire;
    W borrow = c.public_bit(false);
    for (int i = 0; i < N; ++i) {
        // borrow' = (~a & b) | (~(a^b) & borrow)
        W na  = c.not_gate(a[i]);
        W axb = c.xor_gate(a[i], b[i]);
        W g   = c.and_gate(na, b[i]);          // generate borrow
        W p   = c.and_gate(c.not_gate(axb), borrow);
        borrow = c.xor_gate(g, c.and_gate(c.not_gate(g), p)); // g | p
    }
    return borrow;
}

// all bits equal: AND of XNORs.
template <BooleanContext Ctx, int N>
inline typename Ctx::Wire equal(Ctx& c, const typename Ctx::Wire a[N],
                                const typename Ctx::Wire b[N]) {
    using W = typename Ctx::Wire;
    W acc = c.public_bit(true);
    for (int i = 0; i < N; ++i)
        acc = c.and_gate(acc, c.not_gate(c.xor_gate(a[i], b[i])));   // acc & ~(a^b)
    return acc;
}

// dest[i] <- cond ? tsrc[i] : fsrc[i]. dest may alias tsrc or fsrc.
template <BooleanContext Ctx>
inline void if_then_else(Ctx& c, typename Ctx::Wire* dest, const typename Ctx::Wire* tsrc,
                         const typename Ctx::Wire* fsrc, int size, typename Ctx::Wire cond) {
    using W = typename Ctx::Wire;
    int i = 0;
    while (size-- > 0) {
        W x = c.xor_gate(tsrc[i], fsrc[i]);
        W a = c.and_gate(cond, x);
        dest[i] = c.xor_gate(a, fsrc[i]);
        ++i;
    }
}

// dest <- low N bits of op1 * op2 (unsigned/signed identical at the low N bits).
template <BooleanContext Ctx, int N>
inline void mul_full(Ctx& c, typename Ctx::Wire* dest,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2) {
    using W = typename Ctx::Wire;
    std::vector<W> sum(N, c.public_bit(false));
    std::vector<W> tmp(N);
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < N - i; ++k) tmp[k] = c.and_gate(op1[k], op2[i]);
        add_full<Ctx>(c, sum.data() + i, nullptr, sum.data() + i, tmp.data(), nullptr, N - i);
    }
    for (int i = 0; i < N; ++i) dest[i] = sum[i];
}

// vquot, vrem <- op1 / op2, op1 % op2 (both unsigned). Either out may be null.
// op2 == 0 saturates (mirrors the obliv-c restoring-division circuit, not C).
template <BooleanContext Ctx, int N>
inline void div_full(Ctx& c, typename Ctx::Wire* vquot, typename Ctx::Wire* vrem,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2) {
    using W = typename Ctx::Wire;
    std::vector<W> overflow(N), tmp(N), rem(N), quot(N);
    W b;
    for (int i = 0; i < N; ++i) rem[i] = op1[i];
    overflow[0] = c.public_bit(false);
    for (int i = 1; i < N; ++i) overflow[i] = or_gate(c, overflow[i - 1], op2[N - i]);
    for (int i = N - 1; i >= 0; --i) {
        sub_full<Ctx>(c, tmp.data(), &b, rem.data() + i, op2, nullptr, N - i);
        b = or_gate(c, b, overflow[i]);
        if_then_else<Ctx>(c, rem.data() + i, rem.data() + i, tmp.data(), N - i, b);
        quot[i] = c.not_gate(b);
    }
    if (vrem)  for (int i = 0; i < N; ++i) vrem[i]  = rem[i];
    if (vquot) for (int i = 0; i < N; ++i) vquot[i] = quot[i];
}

}  // namespace kernel

// ================================ Bit<Ctx> =================================
template <BooleanContext Ctx>
class Bit {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using shape        = BitShape;
    using clear_t      = shape::clear_t;
    Wire w{};

    Bit() = default;
    Bit(Ctx& c, Wire wire) : ctx_(&c), w(wire) {}
    static Bit constant(Ctx& c, bool v) { return Bit(c, c.public_bit(v)); }

    Ctx* context() const { return ctx_; }
    Bit  constant(bool v) const { return constant(*ctx_, v); }   // same-context sugar

    Bit operator&(const Bit& o) const { check_same_context(*this, o); return Bit(*ctx_, ctx_->and_gate(w, o.w)); }
    Bit operator^(const Bit& o) const { check_same_context(*this, o); return Bit(*ctx_, ctx_->xor_gate(w, o.w)); }
    Bit operator|(const Bit& o) const { return *this ^ o ^ (*this & o); }
    Bit operator!() const              { return Bit(*ctx_, ctx_->not_gate(w)); }
    // sel ? t : *this
    Bit select(const Bit& sel, const Bit& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        return Bit(*ctx_, kernel::mux(*ctx_, sel.w, t.w, w));
    }

    static constexpr int width() { return shape::width; }
    void pack_wires(Wire* out) const { out[0] = w; }
    static Bit from_wires(Ctx& c, const Wire* in) { return Bit(c, in[0]); }
    static std::vector<bool> encode(bool v) { return shape::encode(v); }
    static bool decode(const bool* bits)    { return shape::decode(bits); }

private:
    Ctx* ctx_ = nullptr;
};

// =============================== UInt<Ctx,N> ==============================
template <BooleanContext Ctx, int N>
class UInt {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using shape        = UIntShape<N>;
    using clear_t      = typename shape::clear_t;
    std::array<Wire, N> w{};

    UInt() = default;
    explicit UInt(Ctx& c) : ctx_(&c) {}

    static UInt constant(Ctx& c, uint64_t v) {
        static_assert(N <= 64, "UInt::constant uint64 carrier limited to N <= 64 (TODO: limb type)");
        UInt r(c);
        for (int i = 0; i < N; ++i) r.w[i] = c.public_bit((v >> i) & 1);
        return r;
    }
    static UInt from_wires(Ctx& c, const Wire* in) {
        UInt r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    UInt constant(uint64_t v) const { return constant(*ctx_, v); }   // same-context sugar

    Bit<Ctx> operator[](int i) const { return Bit<Ctx>(*ctx_, w[i]); }

    UInt operator+(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); kernel::ripple_add<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    UInt operator-(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); kernel::ripple_sub<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    UInt operator&(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    UInt operator^(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    UInt operator|(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = (*this)[i].operator|(o[i]).w; return r; }
    UInt operator~() const               { UInt r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }

    // mul/div/mod budget note (templated kernel vs IR replay): multiply is an
    // O(N^2) shift-add and stays a templated kernel comfortably. Division/mod is
    // restoring division — the largest kernel here and a borderline case; it is
    // kept templated (not an .empbc replay) deliberately, because the kernel is
    // width-generic over N whereas a stored circuit would be per-width, and there
    // are no integer builtins recorded. Revisit (record uintN_div) if a fixed
    // width dominates. Float keeps the opposite policy: all nontrivial ops replay.
    // Division by zero saturates (see kernel::div_full).
    UInt operator*(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); kernel::mul_full<Ctx, N>(*ctx_, r.w.data(), w.data(), o.w.data()); return r; }
    UInt operator/(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); kernel::div_full<Ctx, N>(*ctx_, r.w.data(), nullptr, w.data(), o.w.data()); return r; }
    UInt operator%(const UInt& o) const { check_same_context(*this, o); UInt r(*ctx_); kernel::div_full<Ctx, N>(*ctx_, nullptr, r.w.data(), w.data(), o.w.data()); return r; }

    Bit<Ctx> operator==(const UInt& o) const { check_same_context(*this, o); return Bit<Ctx>(*ctx_, kernel::equal<Ctx, N>(*ctx_, w.data(), o.w.data())); }
    Bit<Ctx> operator<(const UInt& o)  const { check_same_context(*this, o); return Bit<Ctx>(*ctx_, kernel::less_than<Ctx, N>(*ctx_, w.data(), o.w.data())); }
    Bit<Ctx> operator>(const UInt& o)  const { return o < *this; }

    UInt select(const Bit<Ctx>& sel, const UInt& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        UInt r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }

    static constexpr int width() { return shape::width; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::vector<bool> encode(uint64_t v) { return shape::encode(v); }
    static uint64_t decode(const bool* bits)    { return shape::decode(bits); }

private:
    Ctx* ctx_ = nullptr;
};

// ============================== Int<Ctx,N> =========================
// Two's complement: +,- share UInt's ripple kernels; comparison is signed.
template <BooleanContext Ctx, int N>
class Int {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using shape        = IntShape<N>;
    using clear_t      = typename shape::clear_t;
    std::array<Wire, N> w{};

    Int() = default;
    explicit Int(Ctx& c) : ctx_(&c) {}
    static Int constant(Ctx& c, int64_t v) {
        static_assert(N <= 64, "Int::constant int64 carrier limited to N <= 64 (TODO: limb type)");
        // shift the bit pattern as unsigned (signed >> is implementation-defined)
        uint64_t u = (uint64_t)v;
        Int r(c); for (int i = 0; i < N; ++i) r.w[i] = c.public_bit((u >> i) & 1); return r;
    }
    static Int from_wires(Ctx& c, const Wire* in) {
        Int r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    Int  constant(int64_t v) const { return constant(*ctx_, v); }   // same-context sugar

    Int operator+(const Int& o) const { check_same_context(*this, o); Int r(*ctx_); kernel::ripple_add<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    Int operator-(const Int& o) const { check_same_context(*this, o); Int r(*ctx_); kernel::ripple_sub<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    Int operator-() const { return Int::constant(*ctx_, 0) - *this; }

    // signed a < b: lt = (a[N-1] != b[N-1]) ? a[N-1] : borrow(a-b).
    Bit<Ctx> operator<(const Int& o) const {
        check_same_context(*this, o);
        using W = typename Ctx::Wire;
        W ub = kernel::less_than<Ctx, N>(*ctx_, w.data(), o.w.data());   // unsigned a<b borrow
        W sa = w[N - 1], sb = o.w[N - 1];
        W diff = ctx_->xor_gate(sa, sb);
        // signed_lt = diff ? sa : ub
        return Bit<Ctx>(*ctx_, kernel::mux(*ctx_, diff, sa, ub));
    }
    Bit<Ctx> operator>(const Int& o) const { return o < *this; }

    static constexpr int width() { return shape::width; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::vector<bool> encode(int64_t v) { return shape::encode(v); }
    static int64_t decode(const bool* bits)    { return shape::decode(bits); }

private:
    Ctx* ctx_ = nullptr;
};

// ================================ Float<Ctx,W> ===========================
// IEEE binary{16,32,64}. Arithmetic replays the recorded fp<W>_<op>.empbc
// through the context (the "big circuits -> IR replay" rule). Clear codec uses
// the host scalar (float for fp16/fp32, double for fp64) via FloatTraits<W>
// (memcpy/bit_cast; software RNE for fp16); raw-bit helpers are also provided.
template <BooleanContext Ctx, int W>
class Float {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using shape        = FloatShape<W>;
    using host_t       = typename FloatTraits<W>::host_t;
    using clear_t      = host_t;
    std::array<Wire, W> w{};

    Float() = default;
    explicit Float(Ctx& c) : ctx_(&c) {}
    static Float from_bits(Ctx& c, uint64_t bits) {
        Float r(c); for (int i = 0; i < W; ++i) r.w[i] = c.public_bit((bits >> i) & 1); return r;
    }
    static Float from_wires(Ctx& c, const Wire* in) {
        Float r(c); for (int i = 0; i < W; ++i) r.w[i] = in[i]; return r;
    }

    // Construct from a host value (e.g. 1.5f) via the per-width bit pattern.
    static Float constant(Ctx& c, host_t v) { return from_bits(c, FloatTraits<W>::to_bits(v)); }

    Ctx*  context() const { return ctx_; }
    Float constant(host_t v) const { return constant(*ctx_, v); }   // same-context sugar

    // ---- IR-replay helpers over the fp<W>_<op>.empbc builtins ----
    // unary/binary/ternary return W bits; compare/classify circuits emit 8 bits
    // (bit 0 is the result), matching the recorded suite.
    //
    // Replay reuses a thread_local ProgramWorkspace (per Ctx/W/operand-count), so
    // float-heavy circuits don't churn input/output/scratch allocations per op.
    // The returned reference aliases the workspace and is valid only until the
    // next replay_<K> call — every caller copies it into a Float/Bit immediately,
    // and the workspace is single-threaded, so sequential ops never overlap.
    template <int K>
    const std::vector<Wire>& replay_(const char* op, const std::array<const Float*, K>& ops) const {
        static thread_local ProgramWorkspace<Wire> ws;
        ws.tmp_inputs.resize((size_t)K * W);
        for (int k = 0; k < K; ++k)
            for (int i = 0; i < W; ++i) ws.tmp_inputs[(size_t)k * W + i] = ops[k]->w[i];
        return execute_program(*ctx_, circuit::float_circuit(W, op),
                               std::span<const Wire>(ws.tmp_inputs.data(), (size_t)K * W), ws);
    }
    Float unary_(const char* op) const {
        const auto& out = replay_<1>(op, {this});
        Float r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Float binop_(const char* op, const Float& o) const {
        check_same_context(*this, o);
        const auto& out = replay_<2>(op, {this, &o});
        Float r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Float ternary_(const char* op, const Float& b, const Float& c) const {
        check_same_context(*this, b); check_same_context(*this, c);
        const auto& out = replay_<3>(op, {this, &b, &c});
        Float r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Bit<Ctx> cmp_(const char* op, const Float& o) const {
        check_same_context(*this, o);
        return Bit<Ctx>(*ctx_, replay_<2>(op, {this, &o})[0]);
    }
    Bit<Ctx> classify_(const char* op) const {
        return Bit<Ctx>(*ctx_, replay_<1>(op, {this})[0]);
    }

    // ---- arithmetic ----
    Float operator+(const Float& o) const { return binop_("add", o); }
    Float operator-(const Float& o) const { return binop_("sub", o); }
    Float operator*(const Float& o) const { return binop_("mul", o); }
    Float operator/(const Float& o) const { return binop_("div", o); }
    Float min(const Float& o) const { return binop_("min", o); }
    Float max(const Float& o) const { return binop_("max", o); }
    Float sqr()   const { return unary_("square"); }
    Float sqrt()  const { return unary_("sqrt"); }
    Float recip() const { return unary_("recip"); }
    Float rsqrt() const { return unary_("rsqrt"); }
    Float fma(const Float& b, const Float& c) const { return ternary_("fma", b, c); }

    // ---- comparisons / classifiers -> Bit ----
    Bit<Ctx> equal(const Float& o)         const { return cmp_("eq", o); }
    Bit<Ctx> not_equal(const Float& o)     const { return cmp_("ne", o); }
    Bit<Ctx> less_than(const Float& o)     const { return cmp_("lt", o); }
    Bit<Ctx> less_equal(const Float& o)    const { return cmp_("le", o); }
    Bit<Ctx> greater_than(const Float& o)  const { return cmp_("gt", o); }
    Bit<Ctx> greater_equal(const Float& o) const { return cmp_("ge", o); }
    Bit<Ctx> is_nan()  const { return classify_("isnan"); }
    Bit<Ctx> is_inf()  const { return classify_("isinf"); }
    Bit<Ctx> is_zero() const { return classify_("iszero"); }

    // ---- sign-bit ops + select: pure wiring, realized locally (MSB = bit W-1) ----
    Float abs() const        { Float r(*this); r.w[W - 1] = ctx_->public_bit(false); return r; }
    Float operator-() const  { Float r(*this); r.w[W - 1] = ctx_->not_gate(w[W - 1]); return r; }
    Float copysign(const Float& o) const { check_same_context(*this, o); Float r(*this); r.w[W - 1] = o.w[W - 1]; return r; }
    // sel ? o : *this, per bit.
    Float select(const Bit<Ctx>& sel, const Float& o) const {
        check_same_context(*this, sel); check_same_context(*this, o);
        Float r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, o.w[i], w[i]); return r;
    }
    Bit<Ctx> operator[](int i) const { return Bit<Ctx>(*ctx_, w[i]); }

    static constexpr int width() { return shape::width; }
    void pack_wires(Wire* out) const { for (int i = 0; i < W; ++i) out[i] = w[i]; }
    // Clear codec: host scalar <-> W bits (LSB-first), via the shape.
    static std::vector<bool> encode(host_t v) { return shape::encode(v); }
    static host_t decode(const bool* bits)    { return shape::decode(bits); }
    // Raw-bit helpers (when you want the IEEE pattern directly).
    static std::vector<bool> encode_bits(uint64_t bits) {
        std::vector<bool> b(W); for (int i = 0; i < W; ++i) b[i] = (bits >> i) & 1; return b;
    }
    static uint64_t decode_bits(const bool* bits) {
        uint64_t v = 0; for (int i = 0; i < W; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }

private:
    Ctx* ctx_ = nullptr;
};

// Round-trip guards: a value names its shape, and the shape binds back to it.
static_assert(std::is_same_v<BitShape::bind<ClearContext>,        Bit<ClearContext>>);
static_assert(std::is_same_v<UIntShape<32>::bind<ClearContext>,   UInt<ClearContext, 32>>);
static_assert(std::is_same_v<IntShape<32>::bind<ClearContext>,    Int<ClearContext, 32>>);
static_assert(std::is_same_v<FloatShape<32>::bind<ClearContext>,  Float<ClearContext, 32>>);
static_assert(std::is_same_v<Bit<ClearContext>::shape,            BitShape>);
static_assert(std::is_same_v<UInt<ClearContext, 32>::shape,       UIntShape<32>>);
static_assert(std::is_same_v<Int<ClearContext, 32>::shape,        IntShape<32>>);
static_assert(std::is_same_v<Float<ClearContext, 32>::shape,      FloatShape<32>>);

}  // namespace emp
#endif  // EMP_CIRCUIT_TYPED_H__
