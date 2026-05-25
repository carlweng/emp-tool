#ifndef EMP_CIRCUIT_H__
#define EMP_CIRCUIT_H__

// Backend-independent umbrella for emp-tool/circuits/*. Pulls in every circuit
// primitive as a `<Wire>` template — it bakes in NO concrete wire type. Choose
// a wire and bind the friendly aliases (Bit, UInt32, …) where you select the
// backend, via EMP_USE_CIRCUIT_TYPES from circuit_types.h. emp-tool's own
// block-wire binding lives in circuit_block.h (included by default through
// emp-tool.h unless EMP_NO_DEFAULT_WIRE is defined).

#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/float32.h"
#include "emp-tool/circuits/circuit_file.h"
#include "emp-tool/circuits/aes_circuit.h"
#include "emp-tool/circuits/aes_128_ctr.h"
#include "emp-tool/circuits/sha3_circuit.h"
#include "emp-tool/circuits/sha3_256.h"
#include "emp-tool/circuits/sha256_circuit.h"
#include "emp-tool/circuits/circuit_types.h"

#endif  // EMP_CIRCUIT_H__
