#ifndef EMP_SESSION_CLEAR_SESSION_H__
#define EMP_SESSION_CLEAR_SESSION_H__

// ClearSession — the plaintext-evaluation session. A session owns the I/O boundary
// (input / reveal) and a direct gate context. Protocol sessions (SH2PCSession,
// AG2PCSession) own the same surface plus IO, party id, OT / preprocessing, and
// batching / checkpoint state; ClearSession is the trivial case — it owns a pure
// ClearCtx, and since there are no secrets in the clear, input is just public wires
// and reveal reads the wire values back.
//
// User shape (identical across clear / sh2pc / ag2pc except the constructor):
//   ClearSession sess;
//   using Ctx = ClearSession::DirectCtx;     // the gate context values are built over
//   using UInt32 = UInt_T<Ctx, 32>;
//   auto a = sess.input<UInt32>(ALICE, x);   // feed inputs through the session
//   auto b = sess.input<UInt32>(BOB,   y);
//   auto c = a + b;                          // pure circuit math on the values
//   uint32_t r = sess.reveal(c, PUBLIC).value();  // results leave through the session
//
// A session does NOT name circuit value families — values are context-bound
// (UInt_T<DirectCtx,N> etc.), so adding a value family needs no session edit. reveal
// returns std::optional<clear_t> (the session contract): the value is present on a
// party that learns it and std::nullopt otherwise. ClearSession is plaintext — it
// always knows the value — so its optional is always populated. Public constants
// stay value/context-level: UInt32::constant(sess.direct_ctx(), 7).

#include "emp-tool/ir/context/clear.h"        // ClearCtx
#include "emp-tool/ir/session/session.h"      // Session / DirectSession
#include "emp-tool/ir/wire_value.h"           // WireValue
#include "emp-tool/runtime/core/constants.h"  // PUBLIC
#include "emp-tool/runtime/core/utils.h"      // error()
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace emp {

class ClearSession {
public:
    using DirectCtx = ClearCtx;
    // reveal returns std::optional<clear_t> (the session contract). ClearSession is
    // plaintext and always knows the value, so its optional is always populated —
    // for every recipient, since there is no other party to withhold it from.
    template <class V> using reveal_t = std::optional<typename V::clear_t>;

    // Plaintext has no private party; PUBLIC is the universal-knowledge convention.
    int party() const { return PUBLIC; }

    // The direct gate context, for value construction that is not I/O — e.g. public
    // constants UInt_T<DirectCtx,32>::constant(sess.direct_ctx(), 7) or crypto kernels.
    DirectCtx& direct_ctx() { return ctx_; }

    // Feed a clear value as `owner`'s input, returning a fixed-width circuit value V
    // over this session's direct context. In the clear there are no secrets, so the
    // bits become public wires (the owner only has to be a valid party id, which is
    // then ignored — protocol sessions route by it).
    template <WireValue V>
    V input(int owner, const typename V::clear_t& value) {
        static_assert(std::same_as<typename V::context_type, DirectCtx>,
            "ClearSession::input<V>: V must be a value over this session's DirectCtx");
        if (owner != ALICE && owner != BOB && owner != PUBLIC)
            error("ClearSession::input: owner must be ALICE, BOB, or PUBLIC");
        const int n = V::width();
        std::vector<bool> bits = V::encode(value);
        // Always-on: a codec that emits the wrong bit count would silently
        // out-of-bounds the wire fill below — that is a codec bug, not a
        // debug-only condition. (SH2PC / AG2PC sessions enforce the same.)
        if ((int)bits.size() != n) error("ClearSession::input: V::encode produced a bit count != V::width()");
        std::vector<typename DirectCtx::Wire> wires((std::size_t)n);
        for (int i = 0; i < n; ++i) wires[i] = ctx_.public_bit(bits[i]);
        return V::from_wires(ctx_, wires.data());
    }

    // Reveal a circuit value to `recipient`, returning std::optional<clear_t>. In
    // the clear everyone learns it (the recipient only has to be a valid party id,
    // which is then ignored), so the optional is always populated. reveal<T>(...)
    // casts the clear value to T for readability.
    template <WireValue V>
    reveal_t<V> reveal(const V& value, int recipient) {
        static_assert(std::same_as<typename V::context_type, DirectCtx>,
            "ClearSession::reveal<V>: V must be a value over this session's DirectCtx");
        if (recipient != ALICE && recipient != BOB && recipient != PUBLIC)
            error("ClearSession::reveal: recipient must be ALICE, BOB, or PUBLIC");
        const int n = V::width();
        std::vector<typename DirectCtx::Wire> wires((std::size_t)n);
        value.pack_wires(wires.data());
        std::unique_ptr<bool[]> buf(new bool[(std::size_t)n]);
        for (int i = 0; i < n; ++i) buf[i] = (wires[i] & 1) != 0;
        return std::optional<typename V::clear_t>(V::decode(buf.get()));
    }
    template <class T, WireValue V>
    std::optional<T> reveal(const V& value, int recipient) {
        reveal_t<V> r = reveal(value, recipient);
        if (!r) return std::nullopt;
        return std::optional<T>(static_cast<T>(*r));
    }

private:
    DirectCtx ctx_;
};

static_assert(Session<ClearSession>);
static_assert(DirectSession<ClearSession>);
// SessionIO<ClearSession, V> for a concrete value type is asserted in the emp-tool
// tests — naming a value family here would make ir depend on circuits/.

}  // namespace emp
#endif  // EMP_SESSION_CLEAR_SESSION_H__
