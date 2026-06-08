#ifndef EMP_SESSION_CLEAR_SESSION_H__
#define EMP_SESSION_CLEAR_SESSION_H__

// ClearSession — the plaintext-evaluation session. A session owns the I/O
// boundary (input / reveal) and a circuit context. Protocol sessions
// (SH2PCSession, AG2PCSession) own the same surface plus IO, party id, OT /
// preprocessing, and batching / checkpoint state; ClearSession is the trivial
// case — it owns a pure ClearCtx, and since there are no secrets in the clear,
// input is just public wires and reveal reads the wire values back.
//
// User shape (identical across clear / sh2pc / ag2pc except the constructor):
//   ClearSession sess;
//   using UInt32 = ClearSession::UInt<32>;
//   auto a = sess.input<UInt32>(ALICE, x);   // feed inputs through the session
//   auto b = sess.input<UInt32>(BOB,   y);
//   auto c = a + b;                          // pure circuit math on the values
//   uint32_t r = sess.reveal(c, PUBLIC);     // results leave through the session
//
// Pure circuit bodies stay Ctx-templated values; the session is only the
// boundary. Public constants stay value/context-level: UInt32::constant(sess.ctx(), 7).

#include "emp-tool/context/clear.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/core/utils.h"   // error()
#include <cstddef>
#include <memory>
#include <vector>

namespace emp {

class ClearSession {
public:
    using Ctx = ClearCtx;
    template <int N> using UInt   = UInt_T<Ctx, N>;
    template <int N> using Int    = Int_T<Ctx, N>;
    template <int N> using BitVec = BitVec_T<Ctx, N>;
    template <int W> using Float  = Float_T<Ctx, W>;
    using Bit = Bit_T<Ctx>;

    // The circuit context, for value construction that is not I/O — e.g. public
    // constants UInt<32>::constant(sess.ctx(), 7) or crypto kernels.
    Ctx& ctx() { return ctx_; }

    // Feed a clear value as `owner`'s input, returning a fixed-width circuit value
    // V over this session's context. In the clear there are no secrets, so the
    // bits become public wires (owner is accepted and ignored). V must be a
    // fixed-width value (Bit/BitVec/UInt/Int/Float); runtime-width values are
    // in-circuit only and are made with ::constant, not fed as input.
    template <class V>
    V input(int /*owner*/, const typename V::clear_t& value) {
        const int n = V::width();
        std::vector<bool> bits = V::encode(value);
        // Always-on: a codec that emits the wrong bit count would silently
        // out-of-bounds the wire fill below — that is a codec bug, not a
        // debug-only condition. (SH2PC / AG2PC sessions enforce the same.)
        if ((int)bits.size() != n) error("ClearSession::input: V::encode produced a bit count != V::width()");
        std::vector<typename Ctx::Wire> wires((std::size_t)n);
        for (int i = 0; i < n; ++i) wires[i] = ctx_.public_bit(bits[i]);
        return V::from_wires(ctx_, wires.data());
    }

    // Reveal a circuit value to `recipient`, returning the clear value. In the
    // clear everyone learns it (recipient accepted and ignored). reveal<T>(...)
    // casts the clear value to T for readability.
    template <class V>
    typename V::clear_t reveal(const V& value, int /*recipient*/) {
        const int n = V::width();
        std::vector<typename Ctx::Wire> wires((std::size_t)n);
        value.pack_wires(wires.data());
        std::unique_ptr<bool[]> buf(new bool[(std::size_t)n]);
        for (int i = 0; i < n; ++i) buf[i] = (wires[i] & 1) != 0;
        return V::decode(buf.get());
    }
    template <class T, class V>
    T reveal(const V& value, int recipient) { return static_cast<T>(reveal(value, recipient)); }

private:
    Ctx ctx_;
};

}  // namespace emp
#endif  // EMP_SESSION_CLEAR_SESSION_H__
