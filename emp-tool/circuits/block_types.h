#ifndef EMP_BLOCK_TYPES_H__
#define EMP_BLOCK_TYPES_H__

// Standard opt-in binding of the circuit layer to emp::block.
//
// emp-tool/emp-tool.h intentionally does not define bare circuit aliases:
// downstream protocol libraries include it as a substrate and bind their own
// wire types. Applications/tests that want the ordinary block-wire circuit
// types include emp-tool.h and opt into the nested namespace:
//
//     #include "emp-tool/emp-tool.h"
//     using namespace emp::block_types;  // in .cpp files only
//
// Circuit-only users may include this header directly instead of emp-tool.h.
// Header/library code should prefer explicit names or narrow using-declarations
// instead of exporting a using-directive to its includers.

#include "emp-tool/core/block.h"
#include "emp-tool/circuits/circuit.h"

namespace emp {
namespace block_types {

EMP_CIRCUIT_TYPES_ALL(emp::block)

}  // namespace block_types

// Skip per-TU implicit instantiation of the block classes; the matching
// definitions are emitted once in circuit.cpp.
EMP_CIRCUIT_CLASS_LIST(EMP_EXTERN_TEMPLATE, block)

}  // namespace emp

#endif  // EMP_BLOCK_TYPES_H__
