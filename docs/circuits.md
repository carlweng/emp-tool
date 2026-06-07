# Circuit value layer (`emp-tool/circuits/`)

Conventions for the circuit value types under `emp-tool/circuits/`.

The circuit value layer is the **context-bound typed values**: `Bit_T<Ctx>`,
`UInt_T<Ctx,N>`, `Int_T<Ctx,N>`, `Float_T<Ctx,W>` in
[typed.h](../emp-tool/circuits/typed.h), templated on a `BooleanContext`
(`circuits/context.h`). Static dispatch, no global backend; each value carries
its own `Ctx*` and issues value-return gates on it. This is the layer the
[frontend](frontend.md) compiles and replays, and the layer protocol contexts
(e.g. emp-sh2pc's `SH2PCCtx`) feed through `input`/`reveal`.

A separate internal `emp::legacy` namespace holds the Wire-bound object API
(`Bit_T<Wire>`, `BitVec_T<Wire>`, the `*_Calculator_T<Wire>` kernels, …): the
global-`emp::backend` object API and the recording sources that produce the
prebuilt `.empbc` builtins. It is not a peer user-facing layer; see
[Internal: the Wire-bound layer](#internal-the-wire-bound-layer-emplegacy) at
the end.

For numeric semantics (wrap, division, comparison), read
[numeric_semantics.md](numeric_semantics.md). For protocol code that
*uses* these primitives, read [EMP_TRANSLATION.md](EMP_TRANSLATION.md)
instead.

## Context-bound values (`typed.h`)

Each value is a small struct templated on a `BooleanContext`
`Ctx`. It holds its wires inline (`Wire w;` in `Bit_T`,
`std::array<Wire,N> w;` in `UInt_T`/`Int_T`/`Float_T`) plus a private `Ctx*`
(reached via `context()`); operators issue value-return gates on the context. No
inheritance, no marker base, no global backend.

Every value provides three contracts that generic code (contexts, the frontend)
relies on:

- **context** — `context() -> Ctx*`, `using context_type = Ctx;`, and
  `template<BooleanContext C2> using rebind = X_T<C2,…>;` ("same value family,
  different context"; compile-time only, moves no data). E.g.
  `UInt_T<RecordCtx,32>::rebind<ClearCtx> == UInt_T<ClearCtx,32>`.
- **wire layout** — `static constexpr int width()`, `void pack_wires(Wire*) const`,
  `static X_T from_wires(Ctx&, const Wire*)`.
- **clear codec** — `static std::vector<bool> encode(clear_t)` (LSB-first) and
  `static clear_t decode(const bool*)`, with `using clear_t = …;` (`bool` for
  `Bit_T`, `uint64_t` for `UInt_T`, `int64_t` for `Int_T`, the host float type
  for `Float_T`).

These static members are exposed uniformly through
[`emp::value_traits<T>`](../emp-tool/circuits/value_traits.h)
(`width()`/`encode`/`decode`/`rebind<Ctx>`) — the single metadata accessor over a
value's own static members. A type meeting all three contracts satisfies the
`CircuitValue` concept (`frontend/circuit_fn.h`) and can be `input`/`reveal`'d by
a context and `compile`/`run` by the frontend.

```cpp
#include "emp-tool/circuits/typed.h"
using namespace emp;

ClearCtx cx;
auto a = UInt_T<ClearCtx,32>::constant(cx, 7);
auto b = UInt_T<ClearCtx,32>::constant(cx, 5);
auto s = a + b;                         // value-return gates on cx
auto lt = a < b;                        // -> Bit_T<ClearCtx>
```

### Arithmetic kernels (`emp::kernel`)

The small, inlineable, compiler-fusible structured kernels (ripple add/sub,
mux, comparators, multiply, restoring division, `if_then_else`) live in namespace
`emp::kernel` in `typed.h`, written against bare `Ctx::Wire` (no per-bit `Ctx*`).
They are LSB-first and size-optimal (one AND per full adder: an N-bit add is
N−1 ANDs). The value-type operators forward to them. Large ops are handled
differently: `Float_T` arithmetic **replays** the recorded `fp<W>_<op>.empbc`
builtins through the context (`circuit::float_circuit` + `execute_program`),
rather than carrying a templated kernel; integer multiply/div/mod stay templated
kernels because they are width-generic over `N`.

### Context check

Mixing typed values from two different contexts silently corrupts (especially
with id-based wires). `check_same_context` guards binary operators: a trivial
pointer compare of the two `context()`s. It is DEBUG-ONLY by default (on when
`NDEBUG` is unset); `-DEMP_CONTEXT_CHECKS=1` makes it an always-on `error()`,
`-DEMP_CONTEXT_CHECKS=0` disables it entirely.

## Bit / byte ordering

The toolkit-wide convention is **LSB-first within a byte, byte
sequential in memory**. It is the layout the canonical clear codecs use
(`encode`/`decode` emit/consume bits LSB-first) and the layout the internal
`BitVec_T` and the `.empbc` byte feeds use. Two pieces:

1. **Bit-within-byte: LSB at index 0.** `(byte >> k) & 1` with `k=0`
   gives the least-significant bit. This matches FIPS-197's `b_0` =
   "low order bit" and the C language convention.
2. **Bytes sequential: byte `i` of the buffer fills `bits[8i..8i+7]`.**
   No byte reordering inside `BitVec_T`.

Concretely, `BitVec(8*N, ptr, party)` lays out bits like:

```
buffer:    a[0]                a[1]                a[2]              ...
bit index: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15  16 ...
           ↑                       ↑                          ↑
           LSB of a[0]             LSB of a[1]                LSB of a[2]
```

So `bits[7]` (MSB of `a[0]`) and `bits[8]` (LSB of `a[1]`) are
adjacent — the BitVec treats the byte buffer as one large
little-endian multi-byte integer. On a little-endian host (modern
x86 / ARM in default config) this means
`BitVec(N, &uint_var, party).bits[i]` equals bit `i` of `uint_var`
directly — no transformation. On a big-endian host you'd need to
byte-swap before feeding `BitVec`.

`BitVec::reveal(void* out, party)` is the inverse: writes the same
LSB-first-within-byte, byte-sequential layout back to memory.

### Exceptions / things that flip

- **Some published cryptographic circuits use MSB-first per-byte
  notation.** The Boyar-Peralta AES SBox formulas in
  [aes_circuit.h](../emp-tool/circuits/aes_circuit.h) are written
  with `U[0]` = MSB of the byte. The function does a one-time
  index flip (`U[i] = U_lsb[7-i]`) at entry and exit so callers
  see the LSB-first convention. The flip emits zero gates — it's
  pure renaming.

## Internal: the Wire-bound layer (`emp::legacy`)

The `emp::legacy` namespace holds the Wire-bound object API: classes templated
directly on a `Wire` type that drive the global `emp::backend` (virtual
dispatch). It exists to serve two internal paths — the global-backend object API
and the **recording sources** that produce the prebuilt `.empbc` builtins (AES,
SHA, the `fp<W>_*` float suite). Application code uses the context-bound values
above; reach into `emp::legacy` only when working on those internal paths.

The set: `Bit_T<Wire>`, `BitVec_T<Wire>`, `UnsignedInt_T<Wire,N>`,
`SignedInt_T<Wire,N>`, `Float_T<Wire,W>`, plus the calculator kernels
`AES_Calculator_T<Wire>`, `AES_128_CTR_Calculator_T<Wire>`,
`Keccak_F_Calculator_T<Wire>`, `SHA3_256_Calculator_T<Wire>`,
`SHA256_Calculator_T<Wire>` (in
`circuits/{bit,bitvec,unsigned_int,signed_int,float,aes_circuit,aes_128_ctr,
sha3_circuit,sha3_256,sha256_circuit}.h`, gathered by `circuits/circuit.h`).
Wire storage is inline (`Wire bit;`, `std::vector<Bit_T<Wire>> bits;`) for the
gate-rate budget. The standard `block`-typed aliases (`Bit`, `BitVec`,
`UnsignedInt`, `SignedInt`, `Float`, …) are bound by
`EMP_CIRCUIT_TYPES_ALL(emp::block)` (`circuits/circuit_types.h`) under the
nested `emp::block_types` namespace; `emp-tool.h` defines no bare circuit aliases
in `emp` itself.

Implementation notes for anyone modifying this layer:

- `UnsignedInt_T` / `SignedInt_T` inherit from `BitVec_T` for storage and
  bitwise/structural ops, overriding `& | ^ ~` and the static-shamt shifts to
  return the derived type. Signed/unsigned conversion is an explicit
  `.as_signed()` / `.as_unsigned()` bit-cast (no gates).
- Sortable / `If` / `sort` dispatch goes through the CRTP mixin
  `Sortable<Wire, Self>` (`circuits/sortable.h`); derived classes supply
  `geq` / `equal` / `select`. Don't add `operator==` on `BitVec_T` itself — it
  would collide with the mixin's `operator==`.
- Shared bit-array kernels (`add_full`, `sub_full`, `mul_full`, `div_full`,
  `cond_neg`, `if_then_else`) live in `circuits/numeric_kernels.h`; sign
  semantics live one level up (signed division = unsigned div with pre/post
  `cond_neg`, etc.). These are the Wire-bound counterpart of the context-bound
  layer's `emp::kernel` set.
- Each value implements a small uniform interface (`wire_type`, `rebind<NW>`,
  `pack_size`/`pack`/`unpack`, `select`) via the **non-virtual** marker base
  `CircuitValue` ([circuit_value.h](../emp-tool/circuits/circuit_value.h)): a
  vtable would bloat `Bit_T` and break `Bit_T<block>` trivial-copyability, and
  dispatch is always templated on a concrete type. (This is independent of the
  context-bound layer's `CircuitValue` *concept* + `value_traits<T>`.)
