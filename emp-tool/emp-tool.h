// emp-tool's wire formats and most internal byte/bit packings assume
// little-endian. KDF (hash.h), block packing (block.h), and any IO that
// memcpy's an integer over the wire would silently produce mismatched
// output on a big-endian peer. Refuse to build there rather than ship a
// binary that subtly disagrees with little-endian counterparties.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "emp-tool requires a little-endian target");

#include <thread>
#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/net_io_channel.h"
#include "emp-tool/io/tls_io_channel.h"
#include "emp-tool/io/trace_io.h"

#include "emp-tool/core/block.h"
#include "emp-tool/core/block_vector.h"
#include "emp-tool/core/constants.h"
#include "emp-tool/core/test_mode.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/crypto/hash.h"
#include "emp-tool/crypto/prg.h"
#include "emp-tool/crypto/prp.h"
#include "emp-tool/crypto/ccrh.h"
#include "emp-tool/crypto/mitccrh.h"
#include "emp-tool/crypto/aes.h"
#include "emp-tool/crypto/session_id.h"
#include "emp-tool/crypto/f2k.h"
#include "emp-tool/crypto/ec.h"
#include "emp-tool/crypto/ro.h"
#include "emp-tool/third_party/ThreadPool.h"

#include "emp-tool/execution/backend.h"
#include "emp-tool/execution/clear_backend.h"
#include "emp-tool/execution/half_gate.h"
#include "emp-tool/execution/privacy_free.h"

// circuits/circuit.h aggregates all emp-tool/circuits/*.h headers as
// backend-independent `<Wire>` templates and the EMP_USE_CIRCUIT_TYPES binding
// macro. It bakes in NO concrete wire type and defines NO aliases: the wire is
// a property of the backend, so the friendly names (Bit, UInt32, …) are bound
// where the backend is chosen. At that site write one statement (the macros
// open namespace emp themselves):
//
//     EMP_USE_CIRCUIT_TYPES_ALL(block);                  // the whole set
//     EMP_USE_CIRCUIT_TYPES(block, Bit, UInt32);         // just these
//
// or, for the standard block batteries-included set plus the extern-template
// build speedup, `#include "emp-tool/circuits/circuit_block.h"`.
#include "emp-tool/circuits/circuit.h"
