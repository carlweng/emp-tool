#ifndef EMP_FRONTEND_CIRCUIT_FN_H__
#define EMP_FRONTEND_CIRCUIT_FN_H__

// The circuit-function frontend over the C++20 BooleanContext model: write a
// PURE circuit body once, compile() it into a context-free Circuit<Sig> (a
// recorded BooleanProgram + signature), and run() it on ANY context — plaintext,
// garbled 2PC, ZK, ... — exactly like the built-in .empbc circuits. No global
// backend, no Bit_T.
//
// A circuit is PURE: no input/reveal/OT inside a body (RecordContext has no I/O,
// so it is structurally impossible). The session does I/O AROUND run():
//   auto a = sess.input<UInt<Ctx,32>>(ctx, ALICE, av);
//   auto c = frontend::run(ctx, circuit, a, b);
//   sess.reveal(c, PUBLIC);
//
// A body comes in two forms; compile/run pick whichever is invocable:
//   [](auto a, auto b){ return a + b; }          // implicit context (default)
//   [](auto& ctx, auto a, auto b){ ... }         // explicit context (general:
//                                                //  the only form for nullary
//                                                //  circuits and for making a
//                                                //  constant with no anchor arg)
// A body callable in BOTH forms (e.g. a variadic lambda) is a contract error.
// In the implicit form, make a constant from an argument: a.constant(5). C++20.

#include "emp-tool/circuits/context.h"          // RecordContext, execute_program, ProgramWorkspace
#include "emp-tool/circuits/circuit_artifact.h" // CircuitArtifact, CircuitSignature, validate_artifact
#include "emp-tool/circuits/shape.h"            // Shape concept + *Shape
#include "emp-tool/circuits/typed.h"            // typed values (Bit/UInt/Int/Float)
#include "emp-tool/core/utils.h"                // error()
#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp {
namespace frontend {

// ---------------------------------------------------------------------------
// CircuitValue concept — a typed value over some BooleanContext (the new-layer
// analogue of the legacy is_base_of<CircuitValue>). Does NOT include
// circuit_value.h; uses context()/context_type/shape, never a public field.
// ---------------------------------------------------------------------------
template <class V_>
concept CircuitValue =
    requires {
        typename std::decay_t<V_>::Wire;
        typename std::decay_t<V_>::context_type;
        typename std::decay_t<V_>::shape;
        { std::decay_t<V_>::width() } -> std::convertible_to<int>;
    } &&
    Shape<typename std::decay_t<V_>::shape> &&
    std::same_as<typename std::decay_t<V_>::shape::template bind<typename std::decay_t<V_>::context_type>,
                 std::decay_t<V_>> &&
    requires(const std::decay_t<V_> v, typename std::decay_t<V_>::Wire* out) {
        { v.context() } -> std::convertible_to<typename std::decay_t<V_>::context_type*>;
        v.pack_wires(out);
    } &&
    requires(typename std::decay_t<V_>::context_type& c, const typename std::decay_t<V_>::Wire* in) {
        { std::decay_t<V_>::from_wires(c, in) } -> std::same_as<std::decay_t<V_>>;
    };

template <class V> inline constexpr bool is_circuit_value_v = CircuitValue<std::decay_t<V>>;

// ---------------------------------------------------------------------------
// circuit_fn_traits — introspect a body over typed args, in either form.
// ---------------------------------------------------------------------------
namespace detail {
template <class Ctx, class F, bool Impl, bool Expl, class... Args>
struct ret_ { using type = void; };
template <class Ctx, class F, class... Args>
struct ret_<Ctx, F, true, false, Args...> {
    using type = decltype(std::declval<F&>()(std::declval<Args>()...));
};
template <class Ctx, class F, class... Args>
struct ret_<Ctx, F, false, true, Args...> {
    using type = decltype(std::declval<F&>()(std::declval<Ctx&>(), std::declval<Args>()...));
};
}  // namespace detail

template <class Ctx, class F, class... Args>
struct circuit_fn_traits {
    static constexpr bool implicit_callable = std::is_invocable_v<F&, Args...>;
    static constexpr bool explicit_callable = std::is_invocable_v<F&, Ctx&, Args...>;
    static constexpr bool ambiguous = implicit_callable && explicit_callable;
    static constexpr bool callable  = implicit_callable || explicit_callable;
    static constexpr bool wants_ctx = explicit_callable && !implicit_callable;

    using raw_return = typename detail::ret_<Ctx, F, implicit_callable && !ambiguous,
                                             explicit_callable && !ambiguous, Args...>::type;
    using value_return = std::decay_t<raw_return>;

    static constexpr bool args_are_circuit = (true && ... && CircuitValue<Args>);
    static constexpr bool returns_ref      = std::is_reference_v<raw_return>;
    static constexpr bool returns_void     = std::is_void_v<raw_return>;
    static constexpr bool returns_circuit  = !returns_void && CircuitValue<value_return>;
    static constexpr bool ok = args_are_circuit && callable && !ambiguous &&
                               !returns_ref && !returns_void && returns_circuit;
};

// Single diagnostic site: instantiate `(void)sizeof(circuit_contract<Tr>)` in
// each entry point, paired with `if constexpr (Tr::ok)`. The guarded conditions
// make exactly the relevant message fire per failure mode.
template <class Tr, bool Ok = Tr::ok>
struct circuit_contract {};   // Ok == true: no diagnostics
template <class Tr>
struct circuit_contract<Tr, false> {
    static_assert(Tr::args_are_circuit,
        "frontend circuit: every argument must be a circuit value (Bit / UInt / Int / Float / ...)");
    static_assert(!Tr::ambiguous,
        "frontend circuit: body is callable in BOTH implicit and explicit context forms; "
        "disambiguate (take the context explicitly, or not at all)");
    static_assert(Tr::callable,
        "frontend circuit: body is not callable with prvalue circuit-value arguments — write "
        "[](auto a, auto b){...} or [](auto& ctx, auto a, auto b){...}, taking arguments by value");
    static_assert(!(Tr::callable && !Tr::ambiguous && Tr::returns_ref),
        "frontend circuit must RETURN BY VALUE: the output is a fresh circuit value, not a reference");
    static_assert(!(Tr::callable && !Tr::ambiguous && Tr::returns_void),
        "frontend circuit must return a circuit value, not void");
    static_assert(!(Tr::callable && !Tr::ambiguous && !Tr::returns_void) || Tr::returns_circuit,
        "frontend circuit must return a circuit value (Bit / UInt / Int / Float / ...), not a plain value");
};

// ---------------------------------------------------------------------------
// Circuit<RetShape, ArgShapes...> — a compiled, context-free circuit: a
// validated BooleanProgram + its signature (arg widths + return width). The
// shapes are template parameters so run() reconstructs the typed values.
// ---------------------------------------------------------------------------
template <Shape RetShape, Shape... ArgShapes>
class Circuit {
public:
    using ret_shape = RetShape;
    static constexpr std::size_t arity = sizeof...(ArgShapes);

    // Construct from a recorded (or loaded) artifact. Validates the program
    // structurally AND that the signature matches the declared shapes — so a
    // stale, hand-edited, or mis-typed loaded artifact is rejected here rather
    // than silently mis-running. The artifact is otherwise immutable (private).
    explicit Circuit(circuit::CircuitArtifact a) : artifact_(std::move(a)) {
        circuit::validate_artifact(artifact_);
        const circuit::CircuitSignature& s = artifact_.signature;
        const std::array<uint32_t, sizeof...(ArgShapes)> want{(uint32_t)ArgShapes::width...};
        if (s.arg_widths.size() != sizeof...(ArgShapes))
            error("Circuit: artifact signature arity != declared shapes");
        for (std::size_t i = 0; i < want.size(); ++i)
            if (s.arg_widths[i] != want[i])
                error("Circuit: artifact argument width != declared shape width");
        if (s.return_width != (uint32_t)RetShape::width)
            error("Circuit: artifact return width != declared shape width");
    }

    const circuit::BooleanProgram&   program()   const { return artifact_.program; }
    const circuit::CircuitSignature& signature() const { return artifact_.signature; }

private:
    circuit::CircuitArtifact artifact_;
};

struct invalid_circuit_fn {};   // sentinel for the contract-violating branch

template <class T> inline constexpr bool is_circuit_v = false;
template <Shape R, Shape... A> inline constexpr bool is_circuit_v<Circuit<R, A...>> = true;

// ---------------------------------------------------------------------------
// compile<ArgShapes...>(body): record the body once through a RecordContext.
// ---------------------------------------------------------------------------
namespace detail {
// non-shape -> BitShape so the traits stay well-formed and the static_assert
// below produces the clean "must be a shape" message first.
template <class S> using shape_or_default = std::conditional_t<Shape<S>, S, BitShape>;
template <class S> using rec_value_t = typename shape_or_default<S>::template bind<RecordContext>;

template <bool Ok, class Tr, class... ArgShapes>
struct compiled_ret { using type = invalid_circuit_fn; };
template <class Tr, class... ArgShapes>
struct compiled_ret<true, Tr, ArgShapes...> {
    using type = Circuit<typename Tr::value_return::shape, ArgShapes...>;
};
template <class Tr, class... ArgShapes>
using compiled_ret_t =
    typename compiled_ret<((Shape<ArgShapes> && ...) && Tr::ok), Tr, ArgShapes...>::type;

// Build one recording-context arg from a freshly reserved input window.
template <Shape S>
typename S::template bind<RecordContext> make_rec_arg(RecordContext& rc) {
    using V = typename S::template bind<RecordContext>;
    RecordContext::Wire base = rc.external_input((size_t)S::width);
    std::array<RecordContext::Wire, (std::size_t)S::width> win{};
    for (int i = 0; i < S::width; ++i) win[i] = base + (RecordContext::Wire)i;
    return V::from_wires(rc, win.data());
}
}  // namespace detail

template <class... ArgShapes, class F,
          class Tr = circuit_fn_traits<RecordContext, std::decay_t<F>,
                                       detail::rec_value_t<ArgShapes>...>>
detail::compiled_ret_t<Tr, ArgShapes...> compile(F&& body) {
    static_assert((Shape<ArgShapes> && ...),
        "frontend::compile: each input must be a SHAPE (BitShape / UIntShape<N> / IntShape<N> / FloatShape<W>)");
    (void)sizeof(circuit_contract<Tr>);
    if constexpr ((Shape<ArgShapes> && ...) && Tr::ok) {
        RecordContext rc;
        // Reserve inputs + build args, left-to-right (braced-init order), BEFORE
        // any gate (RecordContext requires all external_input calls up front).
        std::tuple<typename ArgShapes::template bind<RecordContext>...> args{
            detail::make_rec_arg<ArgShapes>(rc)...};
        auto ret = [&] {
            if constexpr (Tr::wants_ctx)
                return std::apply([&](auto&&... a) { return body(rc, std::move(a)...); }, args);
            else
                return std::apply([&](auto&&... a) { return body(std::move(a)...); }, args);
        }();
        using Ret = std::decay_t<decltype(ret)>;
        std::array<RecordContext::Wire, (std::size_t)Ret::width()> ow{};
        ret.pack_wires(ow.data());
        rc.finish(std::span<const RecordContext::Wire>(ow.data(), ow.size()));

        circuit::CircuitArtifact art;
        art.program   = std::move(rc.prog);
        art.signature = circuit::CircuitSignature{
            std::vector<uint32_t>{(uint32_t)ArgShapes::width...}, (uint32_t)Ret::width()};
        return detail::compiled_ret_t<Tr, ArgShapes...>(std::move(art));   // ctor validates
    } else {
        return {};   // invalid_circuit_fn; unreachable after the asserts
    }
}

// ---------------------------------------------------------------------------
// run(ctx, circuit, args...): replay a compiled circuit on a live context.
// Args by const-ref (replay only packs wires); ctx is explicit (no global
// backend).
// ---------------------------------------------------------------------------
namespace detail {
template <class Wire, class V>
inline void append_wires(std::vector<Wire>& out, const V& v) {
    std::size_t base = out.size();
    out.resize(base + (std::size_t)V::width());
    v.pack_wires(out.data() + base);
}
}  // namespace detail

template <BooleanContext Ctx, Shape RetShape, Shape... ArgShapes>
typename RetShape::template bind<Ctx>
run(Ctx& ctx, const Circuit<RetShape, ArgShapes...>& c,
    const typename ArgShapes::template bind<Ctx>&... args) {
    using Wire = typename Ctx::Wire;
    const circuit::BooleanProgram& p = c.program();
    if (c.signature().arg_widths.size() != sizeof...(ArgShapes))
        error("frontend::run: argument count != circuit arity (stale artifact?)");
    std::vector<Wire> inputs;
    inputs.reserve(p.num_inputs);
    (detail::append_wires(inputs, args), ...);
    if ((uint32_t)inputs.size() != p.num_inputs)
        error("frontend::run: total argument width != circuit input count");
    ProgramWorkspace<Wire> ws;
    const std::vector<Wire>& ow =
        execute_program(ctx, p, std::span<const Wire>(inputs.data(), inputs.size()), ws);
    return RetShape::template bind<Ctx>::from_wires(ctx, ow.data());
}

// ---------------------------------------------------------------------------
// Live run(body, args...): evaluate a body directly on already-live typed values
// (no compile step). Recovers the context from the first argument, so it serves
// both body forms. (Nullary/explicit-only bodies: call the body directly.)
// ---------------------------------------------------------------------------
template <class F, class Arg0, class... Args,
          class Ctx = typename std::decay_t<Arg0>::context_type,
          class Tr  = circuit_fn_traits<Ctx, std::decay_t<F>, std::decay_t<Arg0>, std::decay_t<Args>...>,
          std::enable_if_t<!is_circuit_v<std::decay_t<F>>, int> = 0>
std::conditional_t<Tr::ok, typename Tr::value_return, invalid_circuit_fn>
run(F&& body, Arg0&& a0, Args&&... rest) {
    (void)sizeof(circuit_contract<Tr>);
    if constexpr (Tr::ok) {
        Ctx& ctx = *a0.context();
        if constexpr (Tr::wants_ctx)
            return body(ctx, std::forward<Arg0>(a0), std::forward<Args>(rest)...);
        else
            return body(std::forward<Arg0>(a0), std::forward<Args>(rest)...);
    } else {
        return {};
    }
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_CIRCUIT_FN_H__
