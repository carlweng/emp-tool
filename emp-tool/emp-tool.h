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

#include "emp-tool/execution/half_gate.h"
#include "emp-tool/execution/privacy_free.h"

// emp-tool.h is the substrate (IO, core, crypto, GC primitives). It defines no
// circuit value types: protocol libraries and applications include the circuit
// layer they need explicitly — circuits/typed.h for the BooleanContext values, or
// circuits/circuit.h for the values plus the crypto primitives.
