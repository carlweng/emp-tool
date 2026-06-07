#ifndef EMP_CIRCUIT_BACKEND_CTX_H__
#define EMP_CIRCUIT_BACKEND_CTX_H__

// BackendCtx<Wire> — the internal bridge that lets the value layer (Bit_T<Ctx> /
// UInt_T<Ctx,N> / ...) run on the global virtual emp::Backend (object-mode /
// IR-virtual execution). It is a compatibility shim for the global-backend path;
// the primary path is a native BooleanContext (ClearCtx, RecordCtx, or a protocol
// context like SH2PCCtx). `Wire` MUST match the backend's wire payload (e.g. a
// ClearBackend uses ClearWire, not block) — checked in the constructor.

#include "emp-tool/circuits/context.h"     // BooleanContext
#include "emp-tool/execution/backend.h"    // emp::Backend
#include "emp-tool/core/utils.h"           // error
#include <cstddef>

namespace emp {

template <class WireT>
struct BackendCtx {
    using Wire = WireT;
    Backend* b;
    explicit BackendCtx(Backend* be) : b(be) {
        if (!b) error("BackendCtx: null backend");
        if (b->wire_bytes() != sizeof(Wire))
            error("BackendCtx: Wire size does not match backend->wire_bytes()");
    }
    Wire public_bit(bool v)        { Wire o; b->public_label(&o, v); return o; }
    Wire and_gate(Wire a, Wire b_) { Wire o; b->and_gate(&o, &a, &b_); return o; }
    Wire xor_gate(Wire a, Wire b_) { Wire o; b->xor_gate(&o, &a, &b_); return o; }
    Wire not_gate(Wire a)          { Wire o; b->not_gate(&o, &a); return o; }
};

}  // namespace emp
#endif  // EMP_CIRCUIT_BACKEND_CTX_H__
