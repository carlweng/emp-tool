#ifndef EMP_SESSION_CONCEPT_H__
#define EMP_SESSION_CONCEPT_H__

// The session-layer contract — the peer of BooleanContext (context/concept.h).
// A Session is the I/O boundary a protocol exposes: it owns a gate context (Ctx,
// a BooleanContext) and feeds/reveals concrete values across it. Pure circuit
// bodies stay Ctx-templated values; only a Session does input/reveal. reveal is
// recipient-explicit: reveal_t<V> is std::optional<clear_t> on every session, the
// value present only on a party that learns it (every party for PUBLIC, the named
// recipient otherwise) and std::nullopt on a party that does not. ClearSession is
// plaintext, so its optional is always populated.

#include "emp-tool/context/concept.h"      // BooleanContext
#include "emp-tool/frontend/circuit_fn.h"  // frontend::CircuitValue
#include <concepts>
#include <optional>

namespace emp {

// The common user-facing session surface: a gate context + the value-type aliases
// that name circuit values over it. (Widths 32/128 are representative
// instantiation probes; the alias templates accept any valid width.)
template <class S>
concept CircuitSession =
    requires(S& s) {
        typename S::Ctx;
        requires BooleanContext<typename S::Ctx>;
        { s.ctx() } -> std::same_as<typename S::Ctx&>;
        typename S::Bit;
        typename S::template UInt<32>;
        typename S::template Int<32>;
        typename S::template BitVec<128>;
        typename S::template Float<32>;
    } &&
    frontend::CircuitValue<typename S::Bit> &&
    frontend::CircuitValue<typename S::template UInt<32>>;   // the aliases are real circuit values

// A session can input/reveal a value V over its context: input feeds a clear
// value and returns V; reveal returns reveal_t<V>, which is std::optional<clear_t>
// (value present only on a party that learns it, std::nullopt otherwise). (The
// `s.template input<V>` disambiguator is mandatory — V is dependent.)
template <class S, class V>
concept SessionIO =
    CircuitSession<S> &&
    std::same_as<typename V::context_type, typename S::Ctx> &&
    requires(S& s, typename V::clear_t x, const V& v, int party) {
        typename S::template reveal_t<V>;
        requires std::same_as<typename S::template reveal_t<V>,
                              std::optional<typename V::clear_t>>;
        { s.template input<V>(party, x) } -> std::same_as<V>;
        { s.reveal(v, party) } -> std::same_as<typename S::template reveal_t<V>>;
    };

// A session whose values carry materialized cross-call state and so support a
// checkpoint barrier (not universal — a stateless session has none). The zero-arg
// probe only proves checkpoint exists; the keep-list form checkpoint(v...) is a
// per-session capability exercised by tests, not pinned here.
template <class S>
concept CheckpointingSession =
    CircuitSession<S> &&
    requires(S& s) { s.checkpoint(); };

}  // namespace emp
#endif  // EMP_SESSION_CONCEPT_H__
