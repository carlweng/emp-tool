// The single TU that pays the cost of instantiating the `block`-typed circuit
// classes. Every other TU sees the matching `extern template class …;`
// declarations from block_types.h and skips re-instantiation. The class set is
// the single list in circuit_types.h, shared with the extern declarations.
//
// Member function TEMPLATES (e.g. Bit_T::reveal<O>) are not covered here —
// they're implicitly instantiated per call site as before.

#include "emp-tool/core/block.h"
#include "emp-tool/circuits/circuit.h"

namespace emp {

EMP_CIRCUIT_CLASS_LIST(EMP_INSTANTIATE_TEMPLATE, block)

}  // namespace emp
