#ifndef EMP_IR_WIRE_VALUE_H__
#define EMP_IR_WIRE_VALUE_H__

// WireValue — the generic structural contract for a typed value object over a
// BooleanContext: a fixed-width bundle of wires with a clear-value codec, packable
// to / constructible from raw Ctx::Wire, rebindable to another context, and
// reporting its own context pointer. It is the context-free vocabulary the IR
// execution and session layers speak; it names no concrete value family
// (Bit/UInt/Int/Float/BitVec) and no protocol. A value type satisfies it through
// its own static members — `Wire`/`context_type`/`clear_t`/`width()`/`rebind`/
// `pack_wires`/`from_wires`/`encode`/`decode`/`context()` — read uniformly through
// value_traits<T> (circuits/value_traits.h) where a metadata accessor is wanted.
//
// FIXED-WIDTH ONLY: WireValue requires a static `width()`, so a runtime-width value
// (e.g. UInt_T<Ctx, runtime_width>) does NOT model WireValue. Session I/O and
// circuit compilation need a statically known width / signature; runtime-width
// values are in-circuit only.

#include "emp-tool/ir/context/concept.h"   // BooleanContext
#include <concepts>
#include <type_traits>
#include <vector>

namespace emp {

template <class V_>
concept WireValue =
    requires {
        typename std::decay_t<V_>::Wire;
        typename std::decay_t<V_>::context_type;
        typename std::decay_t<V_>::clear_t;
    } &&
    // The value lives over a real gate context, and its wire IS that context's wire.
    BooleanContext<typename std::decay_t<V_>::context_type> &&
    std::same_as<typename std::decay_t<V_>::Wire,
                 typename std::decay_t<V_>::context_type::Wire> &&
    // width() is a static, compile-time constant int — so a runtime-width value
    // (whose width() is `requires (N > 0)` and absent at N == 0) is not a WireValue.
    requires { typename std::integral_constant<int, std::decay_t<V_>::width()>; } &&
    std::same_as<typename std::decay_t<V_>::template rebind<typename std::decay_t<V_>::context_type>,
                 std::decay_t<V_>> &&
    requires(const std::decay_t<V_> v, typename std::decay_t<V_>::Wire* out) {
        { v.context() } -> std::convertible_to<typename std::decay_t<V_>::context_type*>;
        v.pack_wires(out);
    } &&
    requires(typename std::decay_t<V_>::context_type& c, const typename std::decay_t<V_>::Wire* in) {
        { std::decay_t<V_>::from_wires(c, in) } -> std::same_as<std::decay_t<V_>>;
    } &&
    requires(typename std::decay_t<V_>::clear_t cv, const bool* bits) {
        { std::decay_t<V_>::encode(cv) } -> std::convertible_to<std::vector<bool>>;
        { std::decay_t<V_>::decode(bits) } -> std::same_as<typename std::decay_t<V_>::clear_t>;
    };

template <class V> inline constexpr bool is_wire_value_v = WireValue<std::decay_t<V>>;

}  // namespace emp
#endif  // EMP_IR_WIRE_VALUE_H__
