#ifndef EMP_CONTEXT_COUNT_H__
#define EMP_CONTEXT_COUNT_H__

// CountCtx — count gate calls of a templated kernel (no value tracking). One of
// the non-crypto "alternative interpretations" of the same kernel (cost counting,
// determinism hashing in context/digest.h) that make the "one source, many
// semantics" model concrete — a peer of ClearCtx (eval) and RecordCtx (emit IR).
// The execution/frontend path does not depend on it. (For a stored BooleanProgram
// use circuit::count_pass instead.)

#include "emp-tool/context/concept.h"
#include <cstdint>

namespace emp {

struct CountCtx {
    using Wire = uint8_t;   // value unused; only the call counts matter
    uint64_t ands = 0, xors = 0, nots = 0, consts = 0;
    bool c0_seen = false, c1_seen = false;   // dedup consts to match RecordCtx
    Wire public_bit(bool v) { bool& s = v ? c1_seen : c0_seen; if (!s) { s = true; ++consts; } return 0; }
    Wire and_gate(Wire, Wire)     { ++ands;   return 0; }
    Wire xor_gate(Wire, Wire)     { ++xors;   return 0; }
    Wire not_gate(Wire)           { ++nots;   return 0; }
    uint64_t total() const { return ands + xors + nots + consts; }
};

static_assert(BooleanContext<CountCtx>);

}  // namespace emp
#endif  // EMP_CONTEXT_COUNT_H__
