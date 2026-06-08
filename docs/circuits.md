# Circuit value layer (`emp-tool/circuits/`)

Conventions for the circuit value types under `emp-tool/circuits/`.

The circuit value layer is the **context-bound typed values**: `Bit_T<Ctx>`,
`UInt_T<Ctx,N>`, `Int_T<Ctx,N>`, `Float_T<Ctx,W>`, and `BitVec_T<Ctx,N>`,
templated on a `BooleanContext` (`context/context.h`). Static dispatch, no global
backend; each value carries its own `Ctx*` and issues value-return gates on it.
This is the layer the [frontend](frontend.md) compiles and replays, and the layer
protocol contexts (e.g. emp-sh2pc's `SH2PCCtx`) feed through `input`/`reveal`.

Each value type lives in its own header — `circuits/{bit,bitvec,unsigned_int,
signed_int,float}.h` — over the shared arithmetic in `circuits/numeric_kernels.h`.
Two umbrellas gather them:

- `circuits/typed.h` — the value types (`#include` it to get all five).
- `circuits/circuit.h` — values + `value_traits.h` + sorting (`sort.h`) + the
  crypto primitives (`circuits/crypto/crypto.h` = aes128 / sha256 / keccak).

For numeric semantics (wrap, division, comparison), read
[numeric_semantics.md](numeric_semantics.md). For protocol code that *uses* these
primitives, read [EMP_TRANSLATION.md](EMP_TRANSLATION.md).

## Context-bound values

Each value is a small struct templated on a `BooleanContext` `Ctx`. It holds its
wires inline (`Wire w;` in `Bit_T`, `std::array<Wire,N> w;` in the rest — a
`std::vector<Wire>` for the runtime-width `UInt_T<Ctx,0>` / `Int_T<Ctx,0>`) plus a
private `Ctx*` (reached via `context()`); operators issue value-return gates on the
context. No inheritance, no marker base, no global backend.

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
  `Bit_T`, `uint64_t` for `UInt_T`, `int64_t` for `Int_T`, the host float type for
  `Float_T`, `std::array<bool,N>` for `BitVec_T`).

These static members are exposed uniformly through
[`emp::value_traits<T>`](../emp-tool/circuits/value_traits.h)
(`width()`/`encode`/`decode`/`rebind<Ctx>`) — the single metadata accessor over a
value's own static members. A type meeting all three contracts satisfies the
`CircuitValue` concept (`frontend/circuit_fn.h`) and can be `input`/`reveal`'d by a
context and `compile`/`run` by the frontend.

```cpp
#include "emp-tool/circuits/typed.h"
using namespace emp;

ClearCtx cx;
auto a = UInt_T<ClearCtx,32>::constant(cx, 7);
auto b = UInt_T<ClearCtx,32>::constant(cx, 5);
auto s = a + b;                         // value-return gates on cx
auto lt = a < b;                        // -> Bit_T<ClearCtx>
```

### Value surface

- `Bit_T` — `& | ^ ! == !=`, `select`.
- `UInt_T<N>` — `+ - * / %`, comparisons, `& | ^ ~`, public-amount shifts/rotates
  (`<<`/`>>`/`rotl`/`rotr` by `int`), secret-amount shifts (`<<`/`>>` by a
  `UInt_T` — a barrel shifter), `slice`/`extract`/`concat`/`zext`/`trunc`,
  `hamming_weight`/`popcount<R>`, `leading_zeros`, `mod_exp`, `as_signed`.
- `Int_T<N>` — two's-complement `+ - * / %` (truncating), `-`(negate), signed
  comparisons, `& | ^ ~`, logical-left / arithmetic-right shifts, `sext`/`trunc`,
  `as_unsigned`.

**Fixed vs runtime width.** `N > 0` is a fixed-width value and a `CircuitValue`
(compile-time width, clear codec, wire layout — the form the frontend and protocol
contexts feed through `input`/`reveal`). `N == 0` (the `runtime_width` sentinel) is
the *same* `UInt_T` / `Int_T` family with the width carried in the wire vector and
chosen at construction — `UInt_T<Ctx,0>(ctx, width)`, `UInt_T<Ctx,0>::constant(ctx,
width, v)`. It shares every operator above through the runtime-sized kernels; the
compile-time-width surface (`slice`/`extract`/`concat`/`zext`/`trunc`, secret-amount
barrel shifts, the clear codec, `popcount<R>`) is `requires (N > 0)` and so absent,
and it adds `resize(width)` plus fixed↔runtime conversion (`to_dynamic()` on a fixed
value, `to_fixed<M>()` on a runtime one). A runtime-width value is **not** a
`CircuitValue` — it is for data-driven in-circuit computation, not the frontend
`input`/`compile` boundary. (Width must be `>= 1`; wider-than-64 constants
zero-extend for `UInt_T` and sign-extend for `Int_T`.)
- `Float_T<W>` — `+ - * / min max sqrt recip rsqrt fma`, comparisons / `is_nan` /
  `is_inf` / `is_zero`, `abs`/negate/`copysign`/`select`. Arithmetic **replays**
  the recorded `fp<W>_<op>.empbc` builtins through the context.
- `BitVec_T<N>` — `& | ^ ~`, `== !=`, `select`, public-amount shifts,
  `slice`/`concat`, `as_uint`, indexing.

### Arithmetic kernels (`emp::kernel`)

The small, inlineable structured kernels (ripple add/sub, mux, comparators,
multiply, restoring division, `if_then_else`) live in namespace `emp::kernel` in
[numeric_kernels.h](../emp-tool/circuits/numeric_kernels.h), written against bare
`Ctx::Wire` (no per-bit `Ctx*`). They are LSB-first and size-optimal: one AND per
full adder (an N-bit add is N−1 ANDs); unsigned `<` is the borrow-out of a
subtract (one AND/bit). The value-type operators forward to them, passing the width
as a runtime argument, so the fixed-width and runtime-width (`N == 0`) integers
share one kernel set. `Float_T` is the opposite — every nontrivial op is an
`.empbc` replay.

### Sorting

[sort.h](../emp-tool/circuits/sort.h) provides data-oblivious `compare_swap(a, b)`
(ascending min/max via `<` + `select`) and `sort(values)` (a Batcher odd-even
mergesort network, any length) over any value with `operator<` and `select`
(`UInt_T` / `Int_T` / `Float_T`). The compare-swap schedule is fixed, so the gate
stream is identical for every input.

### Crypto primitives (`emp::circuit::crypto`)

BooleanContext-native crypto circuits over the value layer:

- [aes128.h](../emp-tool/circuits/crypto/aes128.h) — `aes_sbox`, `aes128_key_schedule`,
  `aes128_encrypt_block`, `aes128_encrypt`, plus `aes128_ctr` (CTR mode, NIST SP
  800-38A), all over bare `Ctx::Wire` arrays (the bulk state is wires, not `Bit_T`)
  and taking the `Ctx&` explicitly. Other modes (CBC, …) are caller-composed from
  the block primitive.
- [sha256.h](../emp-tool/circuits/crypto/sha256.h) — `sha256_compress` (the word-level
  compression function over `UInt_T<Ctx,32>`) + `sha256<N>` (full padded hash for a
  compile-time public N-bit message; the padded message buffer is `Ctx::Wire`, the
  256-bit I/O is `Bit_T<Ctx>`).
- [keccak.h](../emp-tool/circuits/crypto/keccak.h) — `keccak_f1600` (the lane-level
  permutation over `UInt_T<Ctx,64>[25]`) + `sha3_256<N>` (the 1600-bit sponge state
  is `Ctx::Wire`, the I/O is `Bit_T<Ctx>`).

These are the source of truth; the prebuilt `aes128` / `sha256_256` /
`sha3_256_256` `.empbc` assets (loaded by `ir/builtins.h`) are kept as replay
fixtures and are checked against the kernels by `test_builtin_circuits`.

### Context check

Mixing typed values from two different contexts silently corrupts (especially with
id-based wires). `check_same_context` guards binary operators: a trivial pointer
compare of the two `context()`s. It is DEBUG-ONLY by default (on when `NDEBUG` is
unset); `-DEMP_CONTEXT_CHECKS=1` makes it an always-on `error()`,
`-DEMP_CONTEXT_CHECKS=0` disables it entirely.

## Bit / byte ordering

The toolkit-wide convention is **LSB-first within a byte, byte sequential in
memory**. It is the layout the canonical clear codecs use (`encode`/`decode`
emit/consume bits LSB-first) and the layout the crypto kernels and `.empbc` byte
feeds use. Two pieces:

1. **Bit-within-byte: LSB at index 0.** `(byte >> k) & 1` with `k=0` gives the
   least-significant bit. This matches FIPS-197's `b_0` = "low order bit" and the C
   language convention.
2. **Bytes sequential: byte `i` of the buffer fills bits `8i..8i+7`.** No byte
   reordering.

So bit `8` (LSB of byte 1) sits just past bit `7` (MSB of byte 0): the buffer is
one large little-endian multi-byte integer. On a little-endian host, bit `i` of a
fixed-width value equals bit `i` of the corresponding scalar directly — no
transformation; on a big-endian host you'd byte-swap before feeding.

### Exceptions / things that flip

- **Some published cryptographic circuits use MSB-first per-byte notation.** The
  Boyar–Peralta AES S-box formulas in
  [aes128.h](../emp-tool/circuits/crypto/aes128.h) (`emp::circuit::crypto::aes_sbox`) are
  written with `U[0]` = MSB of the byte. The function does a one-time index flip
  (`U[i] = U_lsb[7-i]`) at entry and exit so callers see the LSB-first convention.
  The flip emits zero gates — it's pure renaming.
