#ifndef EMP_IR_SESSION_SESSION_IO_H__
#define EMP_IR_SESSION_SESSION_IO_H__

// SessionIO<S,V> — a DirectSession S can feed (input) and open (reveal) a value V
// across its direct context. Generic over any WireValue V; it names no concrete
// value family, so a new value family is usable through any session without editing
// the session. reveal is recipient-explicit: reveal_t<V> is std::optional<clear_t>,
// the value present only on a party that learns it (every party for PUBLIC, the
// named recipient otherwise) and std::nullopt on a party that does not.

#include "emp-tool/ir/session/session.h"   // Session / DirectSession / CheckpointingSession
#include "emp-tool/ir/wire_value.h"        // WireValue
#include <concepts>
#include <optional>

namespace emp {

template <class S, class V>
concept SessionIO =
    DirectSession<S> &&
    WireValue<V> &&
    std::same_as<typename V::context_type, typename S::DirectCtx> &&
    requires(S& s, typename V::clear_t x, const V& v, int party) {
        typename S::template reveal_t<V>;
        requires std::same_as<typename S::template reveal_t<V>,
                              std::optional<typename V::clear_t>>;
        { s.template input<V>(party, x) } -> std::same_as<V>;
        { s.reveal(v, party) } -> std::same_as<typename S::template reveal_t<V>>;
    };

}  // namespace emp
#endif  // EMP_IR_SESSION_SESSION_IO_H__
