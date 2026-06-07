#ifndef EMP_CIRCUIT_SHAPE_H__
#define EMP_CIRCUIT_SHAPE_H__

// Context-free "shapes" — the signature elements of a compiled circuit. A shape
// is "a typed value minus its context": it knows the bit width, the host clear
// type + codec, and which value template it binds to for a given context. The
// frontend (frontend/circuit_fn.h) parameterizes Circuit<RetShape, ArgShapes...>
// by shapes so a compiled circuit's signature is context-free; bind<Ctx>
// re-attaches a context to recover the concrete value (UInt<Ctx,N> etc.).
//
// The clear codec lives canonically HERE; the value templates' static
// encode/decode forward to the shape. C++20.

#include "emp-tool/circuits/context.h"   // BooleanContext concept, ClearContext
#include "emp-tool/circuits/float.h"     // FloatTraits<W>
#include <cstdint>
#include <vector>

namespace emp {

// Forward declarations of the value templates (defined in typed.h). `bind<Ctx>`
// is an alias template, so declarations suffice — no include cycle with typed.h.
template <BooleanContext Ctx>            class Bit;
template <BooleanContext Ctx, int N>     class UInt;
template <BooleanContext Ctx, int N>     class Int;
template <BooleanContext Ctx, int W>     class Float;

struct BitShape {
    using clear_t = bool;
    static constexpr int width = 1;
    template <BooleanContext Ctx> using bind = Bit<Ctx>;
    static std::vector<bool> encode(bool v) { return {v}; }
    static bool              decode(const bool* b) { return b[0]; }
};

template <int N>
struct UIntShape {
    using clear_t = uint64_t;            // TODO: limb clear_t for N > 64
    static constexpr int width = N;
    template <BooleanContext Ctx> using bind = UInt<Ctx, N>;
    static std::vector<bool> encode(uint64_t v) {
        static_assert(N <= 64, "UIntShape clear codec limited to N <= 64 (TODO: limb clear_t)");
        std::vector<bool> b(N); for (int i = 0; i < N; ++i) b[i] = (v >> i) & 1; return b;
    }
    static uint64_t decode(const bool* bits) {
        static_assert(N <= 64, "UIntShape clear codec limited to N <= 64 (TODO: limb clear_t)");
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }
};

template <int N>
struct IntShape {
    using clear_t = int64_t;             // TODO: limb clear_t for N > 64
    static constexpr int width = N;
    template <BooleanContext Ctx> using bind = Int<Ctx, N>;
    static std::vector<bool> encode(int64_t v) {
        static_assert(N <= 64, "IntShape clear codec limited to N <= 64 (TODO: limb clear_t)");
        uint64_t u = (uint64_t)v;
        std::vector<bool> b(N); for (int i = 0; i < N; ++i) b[i] = (u >> i) & 1; return b;
    }
    static int64_t decode(const bool* bits) {
        static_assert(N <= 64, "IntShape clear codec limited to N <= 64 (TODO: limb clear_t)");
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        if (N < 64 && (v >> (N - 1)) & 1) v |= ~((uint64_t(1) << N) - 1);   // sign-extend
        return (int64_t)v;
    }
};

template <int W>
struct FloatShape {
    using clear_t = typename FloatTraits<W>::host_t;   // float (fp16/32) | double (fp64)
    static constexpr int width = W;
    template <BooleanContext Ctx> using bind = Float<Ctx, W>;
    static std::vector<bool> encode(clear_t v) {
        uint64_t bits = FloatTraits<W>::to_bits(v);
        std::vector<bool> b(W); for (int i = 0; i < W; ++i) b[i] = (bits >> i) & 1; return b;
    }
    static clear_t decode(const bool* bits) {
        uint64_t v = 0; for (int i = 0; i < W; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        return FloatTraits<W>::from_bits(v);
    }
};

// A Shape is the context-free signature element: a positive width, a clear codec
// (encode/decode over clear_t), and bind<Ctx> to the concrete typed value.
template <class S>
concept Shape =
    requires {
        { S::width } -> std::convertible_to<int>;
        requires S::width > 0;
        typename S::clear_t;
        typename S::template bind<ClearContext>;
    } &&
    requires(typename S::clear_t v, const bool* bits) {
        { S::encode(v) }    -> std::convertible_to<std::vector<bool>>;
        { S::decode(bits) } -> std::same_as<typename S::clear_t>;
    };

}  // namespace emp
#endif  // EMP_CIRCUIT_SHAPE_H__
