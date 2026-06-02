# Circuit class layer (`emp-tool/circuits/`)

Conventions for the user-facing circuit primitives — `Bit_T`,
`BitVec_T`, `UnsignedInt_T`, `SignedInt_T`, `Float_T` — and their
default-aliased forms `Bit` / `BitVec` / `UnsignedInt` / `SignedInt` /
`Float`. Read this when modifying any header under `emp-tool/circuits/`
or adding a new circuit primitive.

For numeric semantics (wrap, division, shifts, resize), read
[numeric_semantics.md](numeric_semantics.md). For protocol code that
*uses* these primitives, read [EMP_TRANSLATION.md](EMP_TRANSLATION.md)
instead.

## Templating and storage

- Circuit primitives — `Bit_T`, `BitVec_T`, `UnsignedInt_T`,
  `SignedInt_T`, `Float_T` — are class templates on `Wire`. Class
  definitions must not reference `block`. Wire storage is inline
  (`Wire bit;` in `Bit_T`, `vector<Bit_T<Wire>> bits;` in `BitVec_T`)
  — required for the gate-rate budget; do not introduce indirection.

- The `block`-typed default aliases (`Bit`, `BitVec`, `UnsignedInt`,
  `SignedInt`, `Float`, plus the AES/SHA3 calculator aliases) live
  only in `emp-tool/emp-tool.h`. Custom-wire users include the
  templated headers directly and spell out their own aliases.

## Inheritance and operator dispatch

- `UnsignedInt_T` and `SignedInt_T` inherit from `BitVec_T` for storage
  and bitwise/structural ops. The derived classes override `& | ^ ~`
  and the static-shamt shifts so they return the derived type — keeps
  `UnsignedInt ^ UnsignedInt → UnsignedInt` rather than slicing into
  `BitVec`. Conversion between signed and unsigned is an explicit
  `.as_signed()` / `.as_unsigned()` bit-cast (no gates).

- Sortable / If / sort dispatch goes through the CRTP mixin
  `Sortable<Wire, Self>`, where `Self` is the concrete derived type (e.g.
  `Sortable<Wire, UnsignedInt_T<Wire, N>>`).
  Derived classes supply `geq` / `equal` / `select`; the mixin provides
  `>=`, `<=`, `<`, `>`, `==`, `!=`, `If`. Don't add `operator==` on
  `BitVec_T` itself — it would collide with the mixin's operator==
  on classes that inherit both `BitVec_T` and `Sortable`.

## Shared kernels

- Shared bit-array kernels (`add_full`, `sub_full`, `mul_full`,
  `div_full`, `cond_neg`, `if_then_else`) live in
  `circuits/numeric_kernels.h`. Both `UnsignedInt_T` and `SignedInt_T`
  consume them. Sign semantics live one level up: signed division is
  unsigned div with pre/post `cond_neg`, signed comparison is
  sign-extended subtraction, etc.

## Circuit-value interface (`CircuitValue`)

Every circuit value (`Bit_T`, `BitVec_T`, `UnsignedInt_T`, `SignedInt_T`,
`Float_T`) inherits the empty marker base `CircuitValue`
([circuit_value.h](../emp-tool/circuits/circuit_value.h)) and provides a
small uniform interface that generic code (notably the
[frontend](frontend.md) record/replay layer) uses to flatten, rebuild,
and mux any value without per-type external traits:

- `using wire_type = Wire;` — `Sortable` supplies it for `Bit_T` and
  `Float_T`; `BitVec_T` has no `Sortable` base so it declares its own. The
  integer types inherit it from *both* `BitVec_T` and `Sortable`, so they
  re-declare `using wire_type = Wire;` to resolve the otherwise-ambiguous
  lookup.
- `template<class NW> using rebind = Self<NW…>;` — "same shape, different
  wire" (compile-time only; moves no data). Each type names its own
  shape (`UnsignedInt_T<NW,N>`, `Float_T<NW>`, …); `BitVec_T` provides
  the default and the integer types override it.
- `int pack_size() const` / `void pack(Wire*) const` /
  `void unpack(const Wire*, int n)` — flatten to / rebuild from a wire
  array. `BitVec_T` implements these over `bits`; the integer types
  inherit them.
- `Self select(const Bit_T<Wire>& cond, const Self& alt) const` —
  `cond ? alt : *this`. Already present on every type (also the basis of
  the `If/Then/Else` builder).
- Generic free helpers `wire_t<T>`, `rebind_t<T,NW>`, `pack_wires(v)`,
  `assemble<T>(w,n)` live alongside the base.

`CircuitValue` is **non-virtual on purpose**: a vtable would add a vptr to
every `Bit_T` (2–4× its size) and break `Bit_T<block>` trivial-copyability
and the `memcpy` fast paths — and we never dynamically dispatch (all use
is templated on a concrete type). Instead the base declares the methods as
**templated defaults whose bodies `static_assert`-fail** (deferred via a
dependent trait): a type that implements a method hides the default, while
a type that forgets one gets a clear "must implement …" error at the use
site. Adding a new circuit type = give it these members; no central trait
list to edit.

## Bit / byte ordering

The toolkit-wide convention is **LSB-first within a byte, byte
sequential in memory**. Two pieces:

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
- **`aes_128_ctr.h`'s `reverse_bytes(i) = 8*(15 - i/8) + (i%8)`** is
  *byte* (not bit) reordering, specific to the wire layout of the
  shipped `bristol_fashion/aes_128.txt` file (which expects byte
  15 of the input first). It is not a general convention — new
  code that builds AES from `aes_circuit.h` does not need it.
