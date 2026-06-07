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
//   context:     context() -> Ctx*, context_type, rebind<C2> (its family over C2)
//   wire layout: width(), pack_wires(out), from_wires(ctx, in)
//   clear codec: encode(clear) -> bits (LSB-first), decode(bits) -> clear
//                (the canonical codec lives inline on each value; value_traits.h
//                 exposes it uniformly)
// so a session can do input<T>(owner, clear) / reveal<T>(val, recipient) and the
// frontend can compile/replay generically. C++20.

#include "emp-tool/circuits/context.h"
#include "emp-tool/circuits/float_traits.h"   // FloatTraits<W> host codec + circuit::float_circuit (no legacy)
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace emp {

// Float_T replays the fp<W>_<op>.empbc builtins via circuit::float_circuit
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
    // A null context means a default-constructed (uninitialized) value reached an
    // operator — catch it clearly instead of dereferencing null. (Two defaults
    // would otherwise both be null and "match".)
    if (!l.context() || !r.context()) error("typed value: operand is uninitialized (default-constructed, no context)");
    if (l.context() != r.context()) error("typed value: operands belong to different contexts");
#else
    (void)l; (void)r;   // -DEMP_CONTEXT_CHECKS=0 fully disables the check (even in debug)
#endif
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

// ================================ Bit_T<Ctx> ===============================
template <BooleanContext Ctx>
class Bit_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = bool;
    template <BooleanContext C2> using rebind = Bit_T<C2>;
    Wire w{};

    // Default ctor: UNINITIALIZED — null context, no usable wire — so that
    // `Bit_T<Ctx> arr[N];` (or std::array) works; assign / from_wires before any
    // op. Operating on a default-constructed value is UB, like a moved-from one.
    // (Same contract for UInt_T / Int_T / Float_T / Bits_T below.)
    Bit_T() = default;
    Bit_T(Ctx& c, Wire wire) : ctx_(&c), w(wire) {}
    static Bit_T constant(Ctx& c, bool v) { return Bit_T(c, c.public_bit(v)); }

    Ctx* context() const { return ctx_; }
    Bit_T  constant(bool v) const { return constant(*ctx_, v); }   // same-context sugar

    Bit_T operator&(const Bit_T& o) const { check_same_context(*this, o); return Bit_T(*ctx_, ctx_->and_gate(w, o.w)); }
    Bit_T operator^(const Bit_T& o) const { check_same_context(*this, o); return Bit_T(*ctx_, ctx_->xor_gate(w, o.w)); }
    Bit_T operator|(const Bit_T& o) const { return *this ^ o ^ (*this & o); }
    Bit_T operator!() const                { return Bit_T(*ctx_, ctx_->not_gate(w)); }
    Bit_T operator==(const Bit_T& o) const { return !(*this ^ o); }   // XNOR
    Bit_T operator!=(const Bit_T& o) const { return *this ^ o; }      // XOR
    // sel ? t : *this
    Bit_T select(const Bit_T& sel, const Bit_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        return Bit_T(*ctx_, kernel::mux(*ctx_, sel.w, t.w, w));
    }

    static constexpr int width() { return 1; }
    void pack_wires(Wire* out) const { out[0] = w; }
    static Bit_T from_wires(Ctx& c, const Wire* in) { return Bit_T(c, in[0]); }
    static std::vector<bool> encode(bool v) { return std::vector<bool>{v}; }
    static bool decode(const bool* b)       { return b[0]; }

private:
    Ctx* ctx_ = nullptr;
};

// =============================== UInt_T<Ctx,N> ============================
template <BooleanContext Ctx, int N>
class UInt_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = uint64_t;
    template <BooleanContext C2> using rebind = UInt_T<C2, N>;
    std::array<Wire, N> w{};

    UInt_T() = default;
    explicit UInt_T(Ctx& c) : ctx_(&c) {}

    static UInt_T constant(Ctx& c, uint64_t v) {
        static_assert(N <= 64, "UInt_T::constant uint64 carrier limited to N <= 64 (TODO: limb type)");
        UInt_T r(c);
        for (int i = 0; i < N; ++i) r.w[i] = c.public_bit((v >> i) & 1);
        return r;
    }
    static UInt_T from_wires(Ctx& c, const Wire* in) {
        UInt_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    UInt_T constant(uint64_t v) const { return constant(*ctx_, v); }   // same-context sugar

    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    UInt_T operator+(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::ripple_add<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    UInt_T operator-(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::ripple_sub<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    UInt_T operator&(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    UInt_T operator^(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    UInt_T operator|(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = (*this)[i].operator|(o[i]).w; return r; }
    UInt_T operator~() const                { UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }

    // mul/div/mod budget note (templated kernel vs IR replay): multiply is an
    // O(N^2) shift-add and stays a templated kernel comfortably. Division/mod is
    // restoring division — the largest kernel here and a borderline case; it is
    // kept templated (not an .empbc replay) deliberately, because the kernel is
    // width-generic over N whereas a stored circuit would be per-width, and there
    // are no integer builtins recorded. Revisit (record uintN_div) if a fixed
    // width dominates. Float keeps the opposite policy: all nontrivial ops replay.
    // Division by zero saturates (see kernel::div_full).
    UInt_T operator*(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::mul_full<Ctx, N>(*ctx_, r.w.data(), w.data(), o.w.data()); return r; }
    UInt_T operator/(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::div_full<Ctx, N>(*ctx_, r.w.data(), nullptr, w.data(), o.w.data()); return r; }
    UInt_T operator%(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::div_full<Ctx, N>(*ctx_, nullptr, r.w.data(), w.data(), o.w.data()); return r; }

    Bit_T<Ctx> operator==(const UInt_T& o) const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx, N>(*ctx_, w.data(), o.w.data())); }
    Bit_T<Ctx> operator!=(const UInt_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<(const UInt_T& o)  const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::less_than<Ctx, N>(*ctx_, w.data(), o.w.data())); }
    Bit_T<Ctx> operator>(const UInt_T& o)  const { return o < *this; }
    Bit_T<Ctx> operator<=(const UInt_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const UInt_T& o) const { return !(*this < o); }

    UInt_T select(const Bit_T<Ctx>& sel, const UInt_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }

    // --- shifts / rotates by a PUBLIC constant amount (pure wiring, no gates).
    //     Shift amount must be >= 0; logical (zero-fill) for both directions. ---
    UInt_T operator<<(int s) const {
        if (s < 0) error("UInt_T::operator<<: shift amount must be >= 0");
        UInt_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[i] = (i >= s) ? w[i - s] : z;
        return r;
    }
    UInt_T operator>>(int s) const {
        if (s < 0) error("UInt_T::operator>>: shift amount must be >= 0");
        UInt_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[i] = (i + s < N) ? w[i + s] : z;
        return r;
    }
    UInt_T rotl(int s) const { UInt_T r(*ctx_); s = ((s % N) + N) % N; for (int i = 0; i < N; ++i) r.w[i] = w[((i - s) % N + N) % N]; return r; }
    UInt_T rotr(int s) const { UInt_T r(*ctx_); s = ((s % N) + N) % N; for (int i = 0; i < N; ++i) r.w[i] = w[(i + s) % N]; return r; }

    // --- width-changing views (pure wiring): slice [Lo,Hi), concat (this is the
    //     LOW half), zero-extend, truncate. Widths are compile-time for type
    //     safety. ---
    template <int Lo, int Hi> UInt_T<Ctx, Hi - Lo> slice() const {
        static_assert(0 <= Lo && Lo <= Hi && Hi <= N, "UInt_T::slice<Lo,Hi>: out of range");
        UInt_T<Ctx, Hi - Lo> r(*ctx_); for (int i = 0; i < Hi - Lo; ++i) r.w[i] = w[Lo + i]; return r;
    }
    template <int Base, int Width> UInt_T<Ctx, Width> extract() const { return slice<Base, Base + Width>(); }
    template <int M> UInt_T<Ctx, N + M> concat(const UInt_T<Ctx, M>& hi) const {
        check_same_context(*this, hi);
        UInt_T<Ctx, N + M> r(*ctx_);
        for (int i = 0; i < N; ++i) r.w[i] = w[i];
        for (int i = 0; i < M; ++i) r.w[N + i] = hi.w[i];
        return r;
    }
    template <int M> UInt_T<Ctx, M> zext() const {
        static_assert(M >= N, "UInt_T::zext<M>: M must be >= width");
        UInt_T<Ctx, M> r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < M; ++i) r.w[i] = (i < N) ? w[i] : z;
        return r;
    }
    template <int M> UInt_T<Ctx, M> trunc() const {
        static_assert(M <= N, "UInt_T::trunc<M>: M must be <= width");
        UInt_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = w[i]; return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::vector<bool> encode(uint64_t v) {
        static_assert(N <= 64, "UInt_T clear codec limited to N<=64 (TODO: limb type)");
        std::vector<bool> b(N); for (int i = 0; i < N; ++i) b[i] = (v >> i) & 1; return b;
    }
    static uint64_t decode(const bool* bits) {
        static_assert(N <= 64, "UInt_T clear codec limited to N<=64 (TODO: limb type)");
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }

private:
    Ctx* ctx_ = nullptr;
};

// ============================== Int_T<Ctx,N> =======================
// Two's complement: +,- share UInt_T's ripple kernels; comparison is signed.
template <BooleanContext Ctx, int N>
class Int_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = int64_t;
    template <BooleanContext C2> using rebind = Int_T<C2, N>;
    std::array<Wire, N> w{};

    Int_T() = default;
    explicit Int_T(Ctx& c) : ctx_(&c) {}
    static Int_T constant(Ctx& c, int64_t v) {
        static_assert(N <= 64, "Int_T::constant int64 carrier limited to N <= 64 (TODO: limb type)");
        // shift the bit pattern as unsigned (signed >> is implementation-defined)
        uint64_t u = (uint64_t)v;
        Int_T r(c); for (int i = 0; i < N; ++i) r.w[i] = c.public_bit((u >> i) & 1); return r;
    }
    static Int_T from_wires(Ctx& c, const Wire* in) {
        Int_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    Int_T  constant(int64_t v) const { return constant(*ctx_, v); }   // same-context sugar

    Int_T operator+(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::ripple_add<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    Int_T operator-(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::ripple_sub<Ctx, N>(*ctx_, w.data(), o.w.data(), r.w.data()); return r; }
    Int_T operator-() const { return Int_T::constant(*ctx_, 0) - *this; }

    // signed a < b: lt = (a[N-1] != b[N-1]) ? a[N-1] : borrow(a-b).
    Bit_T<Ctx> operator<(const Int_T& o) const {
        check_same_context(*this, o);
        using W = typename Ctx::Wire;
        W ub = kernel::less_than<Ctx, N>(*ctx_, w.data(), o.w.data());   // unsigned a<b borrow
        W sa = w[N - 1], sb = o.w[N - 1];
        W diff = ctx_->xor_gate(sa, sb);
        // signed_lt = diff ? sa : ub
        return Bit_T<Ctx>(*ctx_, kernel::mux(*ctx_, diff, sa, ub));
    }
    Bit_T<Ctx> operator>(const Int_T& o) const { return o < *this; }
    // equality is sign-agnostic (bitwise), same kernel as UInt_T.
    Bit_T<Ctx> operator==(const Int_T& o) const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx, N>(*ctx_, w.data(), o.w.data())); }
    Bit_T<Ctx> operator!=(const Int_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<=(const Int_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const Int_T& o) const { return !(*this < o); }

    // Low N bits of the two's-complement product equal the unsigned product, so
    // multiply reuses UInt_T's kernel; bitwise ops are per-bit.
    Int_T operator*(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::mul_full<Ctx, N>(*ctx_, r.w.data(), w.data(), o.w.data()); return r; }
    Int_T operator&(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    Int_T operator^(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    Int_T operator|(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ((*this)[i] | o[i]).w; return r; }
    Int_T operator~() const               { Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }
    Bit_T<Ctx> operator[](int i) const    { return Bit_T<Ctx>(*ctx_, w[i]); }

    // Signed division / remainder (truncate toward zero; remainder takes the
    // dividend's sign): divide the magnitudes, then fix signs. (Division by zero
    // saturates via the unsigned kernel; the most-negative operand is a UB
    // precondition, as in the legacy signed int.)
    Int_T operator/(const Int_T& o) const {
        check_same_context(*this, o);
        Bit_T<Ctx> sa = (*this)[N - 1], sb = o[N - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);   // magnitudes
        Int_T uq(*ctx_); kernel::div_full<Ctx, N>(*ctx_, uq.w.data(), nullptr, ua.w.data(), ub.w.data());
        return uq.select(sa != sb, -uq);
    }
    Int_T operator%(const Int_T& o) const {
        check_same_context(*this, o);
        Bit_T<Ctx> sa = (*this)[N - 1], sb = o[N - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        Int_T ur(*ctx_); kernel::div_full<Ctx, N>(*ctx_, nullptr, ur.w.data(), ua.w.data(), ub.w.data());
        return ur.select(sa, -ur);
    }

    // sel ? t : *this
    Int_T select(const Bit_T<Ctx>& sel, const Int_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }

    // --- shifts by a PUBLIC constant amount (s >= 0): logical left, ARITHMETIC
    //     right (sign-extending), and width-changing sign-extend / truncate. ---
    Int_T operator<<(int s) const {
        if (s < 0) error("Int_T::operator<<: shift amount must be >= 0");
        Int_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[i] = (i >= s) ? w[i - s] : z;
        return r;
    }
    Int_T operator>>(int s) const {                 // arithmetic: fill with sign bit
        if (s < 0) error("Int_T::operator>>: shift amount must be >= 0");
        Int_T r(*ctx_); Wire sgn = w[N - 1];
        for (int i = 0; i < N; ++i) r.w[i] = (i + s < N) ? w[i + s] : sgn;
        return r;
    }
    template <int M> Int_T<Ctx, M> sext() const {
        static_assert(M >= N, "Int_T::sext<M>: M must be >= width");
        Int_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = (i < N) ? w[i] : w[N - 1]; return r;
    }
    template <int M> Int_T<Ctx, M> trunc() const {
        static_assert(M <= N, "Int_T::trunc<M>: M must be <= width");
        Int_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = w[i]; return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::vector<bool> encode(int64_t v) {
        static_assert(N <= 64, "Int_T clear codec limited to N<=64 (TODO: limb type)");
        uint64_t u = (uint64_t)v;
        std::vector<bool> b(N); for (int i = 0; i < N; ++i) b[i] = (u >> i) & 1; return b;
    }
    static int64_t decode(const bool* bits) {
        static_assert(N <= 64, "Int_T clear codec limited to N<=64 (TODO: limb type)");
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        if (N < 64 && ((v >> (N - 1)) & 1)) v |= ~((uint64_t(1) << N) - 1);
        return (int64_t)v;
    }

private:
    Ctx* ctx_ = nullptr;
};

// ================================ Float_T<Ctx,W> =========================
// IEEE binary{16,32,64}. Arithmetic replays the recorded fp<W>_<op>.empbc
// through the context (the "big circuits -> IR replay" rule). Clear codec uses
// the host scalar (float for fp16/fp32, double for fp64) via FloatTraits<W>
// (memcpy/bit_cast; software RNE for fp16); raw-bit helpers are also provided.
template <BooleanContext Ctx, int W>
class Float_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using host_t       = typename FloatTraits<W>::host_t;
    using clear_t      = host_t;
    template <BooleanContext C2> using rebind = Float_T<C2, W>;
    std::array<Wire, W> w{};

    Float_T() = default;
    explicit Float_T(Ctx& c) : ctx_(&c) {}
    static Float_T from_bits(Ctx& c, uint64_t bits) {
        Float_T r(c); for (int i = 0; i < W; ++i) r.w[i] = c.public_bit((bits >> i) & 1); return r;
    }
    static Float_T from_wires(Ctx& c, const Wire* in) {
        Float_T r(c); for (int i = 0; i < W; ++i) r.w[i] = in[i]; return r;
    }

    // Construct from a host value (e.g. 1.5f) via the per-width bit pattern.
    static Float_T constant(Ctx& c, host_t v) { return from_bits(c, FloatTraits<W>::to_bits(v)); }

    Ctx*    context() const { return ctx_; }
    Float_T constant(host_t v) const { return constant(*ctx_, v); }   // same-context sugar

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
    const std::vector<Wire>& replay_(const char* op, const std::array<const Float_T*, K>& ops) const {
        static thread_local ProgramWorkspace<Wire> ws;
        ws.tmp_inputs.resize((size_t)K * W);
        for (int k = 0; k < K; ++k)
            for (int i = 0; i < W; ++i) ws.tmp_inputs[(size_t)k * W + i] = ops[k]->w[i];
        return execute_program(*ctx_, circuit::float_circuit(W, op),
                               std::span<const Wire>(ws.tmp_inputs.data(), (size_t)K * W), ws);
    }
    Float_T unary_(const char* op) const {
        const auto& out = replay_<1>(op, {this});
        Float_T r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Float_T binop_(const char* op, const Float_T& o) const {
        check_same_context(*this, o);
        const auto& out = replay_<2>(op, {this, &o});
        Float_T r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Float_T ternary_(const char* op, const Float_T& b, const Float_T& c) const {
        check_same_context(*this, b); check_same_context(*this, c);
        const auto& out = replay_<3>(op, {this, &b, &c});
        Float_T r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = out[i]; return r;
    }
    Bit_T<Ctx> cmp_(const char* op, const Float_T& o) const {
        check_same_context(*this, o);
        return Bit_T<Ctx>(*ctx_, replay_<2>(op, {this, &o})[0]);
    }
    Bit_T<Ctx> classify_(const char* op) const {
        return Bit_T<Ctx>(*ctx_, replay_<1>(op, {this})[0]);
    }

    // ---- arithmetic ----
    Float_T operator+(const Float_T& o) const { return binop_("add", o); }
    Float_T operator-(const Float_T& o) const { return binop_("sub", o); }
    Float_T operator*(const Float_T& o) const { return binop_("mul", o); }
    Float_T operator/(const Float_T& o) const { return binop_("div", o); }
    Float_T min(const Float_T& o) const { return binop_("min", o); }
    Float_T max(const Float_T& o) const { return binop_("max", o); }
    Float_T sqr()   const { return unary_("square"); }
    Float_T sqrt()  const { return unary_("sqrt"); }
    Float_T recip() const { return unary_("recip"); }
    Float_T rsqrt() const { return unary_("rsqrt"); }
    Float_T fma(const Float_T& b, const Float_T& c) const { return ternary_("fma", b, c); }

    // ---- comparisons / classifiers -> Bit_T ----
    Bit_T<Ctx> equal(const Float_T& o)         const { return cmp_("eq", o); }
    Bit_T<Ctx> not_equal(const Float_T& o)     const { return cmp_("ne", o); }
    Bit_T<Ctx> less_than(const Float_T& o)     const { return cmp_("lt", o); }
    Bit_T<Ctx> less_equal(const Float_T& o)    const { return cmp_("le", o); }
    Bit_T<Ctx> greater_than(const Float_T& o)  const { return cmp_("gt", o); }
    Bit_T<Ctx> greater_equal(const Float_T& o) const { return cmp_("ge", o); }
    // Operator spellings (NaN-aware, via the fp<W>_* circuits).
    Bit_T<Ctx> operator==(const Float_T& o) const { return equal(o); }
    Bit_T<Ctx> operator!=(const Float_T& o) const { return not_equal(o); }
    Bit_T<Ctx> operator<(const Float_T& o)  const { return less_than(o); }
    Bit_T<Ctx> operator<=(const Float_T& o) const { return less_equal(o); }
    Bit_T<Ctx> operator>(const Float_T& o)  const { return greater_than(o); }
    Bit_T<Ctx> operator>=(const Float_T& o) const { return greater_equal(o); }
    Bit_T<Ctx> is_nan()  const { return classify_("isnan"); }
    Bit_T<Ctx> is_inf()  const { return classify_("isinf"); }
    Bit_T<Ctx> is_zero() const { return classify_("iszero"); }

    // ---- sign-bit ops + select: pure wiring, realized locally (MSB = bit W-1) ----
    Float_T abs() const        { Float_T r(*this); r.w[W - 1] = ctx_->public_bit(false); return r; }
    Float_T operator-() const  { Float_T r(*this); r.w[W - 1] = ctx_->not_gate(w[W - 1]); return r; }
    Float_T copysign(const Float_T& o) const { check_same_context(*this, o); Float_T r(*this); r.w[W - 1] = o.w[W - 1]; return r; }
    // sel ? o : *this, per bit.
    Float_T select(const Bit_T<Ctx>& sel, const Float_T& o) const {
        check_same_context(*this, sel); check_same_context(*this, o);
        Float_T r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, o.w[i], w[i]); return r;
    }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    static constexpr int width() { return W; }
    void pack_wires(Wire* out) const { for (int i = 0; i < W; ++i) out[i] = w[i]; }
    // Clear codec: host scalar <-> W bits (LSB-first), via FloatTraits<W>.
    static std::vector<bool> encode(host_t v) {
        uint64_t bits = FloatTraits<W>::to_bits(v);
        std::vector<bool> b(W); for (int i = 0; i < W; ++i) b[i] = (bits >> i) & 1; return b;
    }
    static host_t decode(const bool* bits) {
        uint64_t v = 0; for (int i = 0; i < W; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        return FloatTraits<W>::from_bits(v);
    }
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

// =============================== Bits_T<Ctx,N> ===========================
// A fixed-width bit vector: the natural value for crypto blocks (AES/SHA I/O) and
// for assembling / disassembling wider values bit by bit. clear_t is the N bools.
// It is interconvertible with UInt_T<Ctx,N> (same wires, zero gates).
template <BooleanContext Ctx, int N>
class Bits_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = std::array<bool, N>;
    template <BooleanContext C2> using rebind = Bits_T<C2, N>;
    std::array<Wire, N> w{};

    Bits_T() = default;                          // uninitialized (null ctx) until assigned/from_wires
    explicit Bits_T(Ctx& c) : ctx_(&c) {}
    static Bits_T constant(Ctx& c, const clear_t& v) {
        Bits_T r(c); for (int i = 0; i < N; ++i) r.w[i] = c.public_bit(v[i]); return r;
    }
    // Assemble from N Bit_T<Ctx> (e.g. a kernel's output bits).
    static Bits_T from_bit_values(Ctx& c, const Bit_T<Ctx>* b) {
        Bits_T r(c); for (int i = 0; i < N; ++i) r.w[i] = b[i].w; return r;
    }
    static Bits_T from_wires(Ctx& c, const Wire* in) { Bits_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r; }

    Ctx* context() const { return ctx_; }
    Bits_T constant(const clear_t& v) const { return constant(*ctx_, v); }   // same-context sugar
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    // Reinterpret the same wires as an unsigned integer (zero gates).
    UInt_T<Ctx, N> as_uint() const { return UInt_T<Ctx, N>::from_wires(*ctx_, w.data()); }

    template <int Lo, int Hi> Bits_T<Ctx, Hi - Lo> slice() const {
        static_assert(0 <= Lo && Lo <= Hi && Hi <= N, "Bits_T::slice<Lo,Hi>: out of range");
        Bits_T<Ctx, Hi - Lo> r(*ctx_); for (int i = 0; i < Hi - Lo; ++i) r.w[i] = w[Lo + i]; return r;
    }
    template <int M> Bits_T<Ctx, N + M> concat(const Bits_T<Ctx, M>& hi) const {  // this is the LOW half
        check_same_context(*this, hi);
        Bits_T<Ctx, N + M> r(*ctx_);
        for (int i = 0; i < N; ++i) r.w[i] = w[i];
        for (int i = 0; i < M; ++i) r.w[N + i] = hi.w[i];
        return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::vector<bool> encode(const clear_t& v) { return std::vector<bool>(v.begin(), v.end()); }
    static clear_t decode(const bool* b) { clear_t v{}; for (int i = 0; i < N; ++i) v[i] = b[i]; return v; }

private:
    Ctx* ctx_ = nullptr;
};

// Round-trip guards: rebinding a value to its own context is the identity.
static_assert(std::is_same_v<Bit_T<ClearCtx>::rebind<ClearCtx>,          Bit_T<ClearCtx>>);
static_assert(std::is_same_v<UInt_T<ClearCtx, 32>::rebind<ClearCtx>,     UInt_T<ClearCtx, 32>>);
static_assert(std::is_same_v<Int_T<ClearCtx, 32>::rebind<ClearCtx>,      Int_T<ClearCtx, 32>>);
static_assert(std::is_same_v<Float_T<ClearCtx, 32>::rebind<ClearCtx>,    Float_T<ClearCtx, 32>>);
static_assert(std::is_same_v<Bits_T<ClearCtx, 128>::rebind<ClearCtx>,    Bits_T<ClearCtx, 128>>);

}  // namespace emp
#endif  // EMP_CIRCUIT_TYPED_H__
