# Floating-Point Circuit Assets

This document records how the shipped IEEE floating-point circuit assets are
represented, generated, and verified. The runtime assets live in:

```text
emp-tool/ir/files/fp<width>_<op>.empbc
```

The `.empbc` files are the canonical stored format for nontrivial generated
circuits. emp-tool loads them through `emp::circuit::float_circuit()`, which
uses the shared circuit asset resolver: `EMP_CIRCUIT_DIR`, then build-tree
assets, then install-tree assets beside the installed headers.

## Scope

The public floating-point suite has 69 operations: 3 IEEE binary widths times 23
operations. Of those, 60 are shipped as `.empbc` assets. The three structural
operations in each width (`abs`, `neg`, and `copysign`) are implemented directly
as EMP bit operations.

Widths:

```text
fp16  IEEE binary16  1 sign,  5 exponent, 10 fraction bits
fp32  IEEE binary32  1 sign,  8 exponent, 23 fraction bits
fp64  IEEE binary64  1 sign, 11 exponent, 52 fraction bits
```

Circuit-backed operations:

```text
add div eq fma ge gt isinf isnan iszero le lt max min
mul ne recip rsqrt sqrt square sub
```

Direct structural operations:

```text
abs neg copysign
```

Notes:

- Predicate/classifier circuits currently output 8 bits; bit 0 is the result.
- `fma` is intentionally unfused: `add(mul(a, b), c)` with two roundings.
- `rsqrt` is `1 / sqrt(x)`, also not a fused primitive.
- Transcendentals (`sin`, `cos`, `tan`, `exp`, `log`, etc.) are not shipped.
- IEEE exception flags, signaling-NaN behavior, exact NaN payload propagation,
  and non-default rounding modes are not modeled.

## Regeneration Contract

The intent of this document is that a future maintainer has enough clues to
regenerate a float circuit or add another externally generated circuit without
reverse-engineering the runtime.

The contract for any generated circuit is:

- Store the checked-in runtime artifact as `.empbc`.
- Model the circuit as one `emp::circuit::BooleanProgram`.
- Use dense wire IDs: inputs are `[0, num_inputs)`, and every non-input wire in
  `[num_inputs, num_wires)` is defined exactly once.
- Keep gates in topological order.
- Use only `And`, `Xor`, `Not`, `Const0`, and `Const1`.
- Normalize unused operands to 0: `Not` uses only `in0`; constants use neither
  operand.
- Preserve LSB-first bit order for integer/float payloads.
- Validate by loading through `load_empbc_file()` before replacing any shipped
  asset.

For floating-point assets specifically:

- File names are `fp<width>_<op>.empbc`, for example `fp64_add.empbc`.
  Structural `abs`, `neg`, and `copysign` do not have files.
- Put runtime assets in `emp-tool/ir/files/`; CMake installs the `.empbc`
  assets and the asset README beside the headers.
- Unary inputs are `A`, binary inputs are `A||B`, and ternary inputs are
  `A||B||C`, each operand LSB-first.
- Non-predicate outputs are one LSB-first float payload of the same width.
- Predicate/classifier outputs are currently 8 bits, with bit 0 holding the
  boolean result.

For non-floating-point generated circuits, prefer the same shape: choose a
stable input/output ABI, emit a dense `BooleanProgram`, save it with
`save_empbc_file()`, and load it through the shared circuit asset resolver
instead of adding another ad hoc search path.

## Generation Pipeline

The current generation path is:

1. Write a small C wrapper for one operation. The wrapper exposes a single
   `mpc_main` with fixed-width public C integer variables named `INPUT_*` and
   `OUTPUT_*`.
2. Compile that wrapper with CBMC-GC-2. Loops must be statically bounded, and
   source shape matters: a smaller C expression usually means a smaller circuit.
3. Convert the generated gate list into `.empbc`, compacting to emp-tool's dense
   `BooleanProgram` invariant and preserving LSB-first bit order.
4. Load the `.empbc` through `load_empbc_file()`, which validates bounds,
   topology, single-definition, canonical unused operands, and dense wire IDs.
5. Verify the `.empbc` output against host floating-point behavior or the native
   C reference for the same operation.

The core library does not parse any legacy text circuit format at runtime.
Conversion scripts are development/provenance tools only; `.empbc` is the stored
format consumed by emp-tool and downstream protocols.

The current tree is not yet a fully automated reproduction harness. In
particular, the CBMC-GC output-to-`BooleanProgram` handoff still wants a
first-class development tool. Bulky one-off C experiments and edited generator
source trees are intentionally not part of the runtime asset directory.

When adding the missing driver, keep it outside the runtime library and make it
emit `BooleanProgram` / `.empbc` directly. A small generic converter/helper may
live beside the assets if useful, but it should not drag in per-op C source
bundles. It should compact any generator-local wire numbering to emp-tool's
dense invariant, reject unsupported gate kinds, and run the same
`load_empbc_file()` validation path that production uses.

## Adding Or Replacing One Circuit

A practical workflow is:

1. Pick the closest width/op strategy from the notes below.
2. Write a wrapper with the `mpc_main` ABI: `INPUT_A_x`, `INPUT_B_x`,
   `INPUT_C_x` as needed, and one `OUTPUT_*` value.
3. Build the C source natively first and compare it against the host or
   SoftFloat oracle. Fix semantic mismatches before generating gates.
4. Generate the Boolean gate list with CBMC-GC-2. Avoid data-dependent loops,
   union type-punning, platform headers that CBMC-GC cannot parse, and
   shared-input shortcuts that change NaN behavior.
5. Convert the generated gates into a dense `.empbc` program following the
   contract above.
6. Verify the `.empbc` with a width-appropriate verifier and any edge-case
   verifier for the operation.
7. Compare AND counts against the table below. Any increase should have a clear
   correctness or maintainability reason.
8. Copy the final asset to `emp-tool/ir/files/` and run `test_float`.

## Generation Tricks

These are the tricks and gotchas that mattered for correctness or circuit size.

- Shape C for CBMC-GC, not for a normal optimizer. Smaller, flatter C usually
  means fewer gates. Data-dependent `while` loops can hang unwinding; use fixed
  trip-count loops.
- Keep wrappers boring. Use `mpc_main`, fixed-width integer inputs named
  `INPUT_A_x`, `INPUT_B_x`, `INPUT_C_x`, and one `OUTPUT_*`. Keep the bit-level
  float payload in an integer type.
- Avoid fragile platform headers. `<stdint.h>` worked for the SoftFloat path;
  macOS `<inttypes.h>` did not. The fp64 sources avoid system headers where
  possible.
- Remove union type-punning before symbolic execution. The SoftFloat-derived
  fp32/fp64 sources use no-union edits so CBMC-GC sees integer fields directly.
- Unity-build the edited SoftFloat core when needed. The wrapper includes only
  the required `.c` files plus a small `platform.h` shim, keeping the generated
  circuit focused on one operation.
- For fp64 multiplication, the Karatsuba-shaped 53-bit significand multiply was
  materially smaller than the straightforward product in the SoftFloat path.
- Do not assume tying a binary circuit's two inputs is safe. `mul(a, a)` exposed
  a CBMC-GC shared-input/NaN issue for fp16/fp64 square. The robust pattern is a
  branch-free dedicated square: compute the symmetric product unconditionally,
  then mux NaN/Inf/Zero at the end.
- Prefer exact digit-recurrence/long-division datapaths for sqrt/div/recip when
  they fit. They beat Newton-style approximations here and are bit-exact rather
  than merely close.
- Comparisons are cheapest when converted to an integer ordering problem. The
  compare/min/max sources use monotonic sign-aware keys and handle NaNs
  explicitly.
- Implement pure rewiring operations directly in EMP. `abs`, `neg`, and
  `copysign` do not need a gate generator or `.empbc` asset.
- Compose circuits for intentionally unfused operations. Current `fma` is
  `add(mul(a, b), c)` and `rsqrt` is `1 / sqrt(x)`, so composition is clearer
  than asking the generator for a larger fused-looking expression.
- For heavy ops, a low CBMC-GC minimization time limit, usually `1`, avoided SAT
  stalls and still produced the checked-in sizes.
- Verify semantics before celebrating gate counts. Native C checks catch source
  mistakes; `.empbc` replay catches conversion/bit-order mistakes; dedicated
  edge sweeps are needed for NaN, Inf, zero signs, subnormals, and shared-input
  cases.
- Keep any "keep smaller" automation conservative: only replace an asset when
  the candidate verifies and is smaller, or when a size increase is justified by
  a correctness fix.

## Per-Width Strategy

### FP16

FP16 was generated from hand-written source-shaped C:

```text
fp16.h
fp16_ops.h
fp16_arith.h
sqr.h
wrappers/
```

No SoftFloat source is needed for fp16. All arithmetic fits naturally in small
integer intermediates. `square` uses a branch-free dedicated squarer because the
shared-input `mul(a, a)` shape can produce wrong circuits after minimization.

Verification:

- Unary operations were checked exhaustively over all 2^16 inputs.
- Binary/ternary operations were checked by large randomized samples.
- `.empbc` files were reloaded and checked through the same operation semantics.

### FP32

FP32 used a mixed strategy:

- hand-written source-shaped C for add/sub, comparisons, classifiers, min/max,
  div, recip, sqrt, and rsqrt;
- a small edited SoftFloat32 bundle for `mul` and `square`;
- direct EMP bit operations for abs/neg/copysign;
- `fma` composed from verified `mul` and `add` circuits.

The SoftFloat32 copies used for generation were edited to remove union
type-punning so CBMC-GC sees raw integer fields. Keep the original license
notices with any regenerated source copies.

### FP64

FP64 also used a mixed strategy:

- source-shaped C for add/sub, comparisons, classifiers, and min/max;
- edited SoftFloat3-derived sources for mul, div, recip, sqrt, and rsqrt;
- a branch-free dedicated square to avoid the shared-input `mul(a, a)` issue;
- direct EMP bit operations for abs/neg/copysign;
- `fma` composed from verified `mul` and `add` circuits.

The SoftFloat64 generation path used no-union edits and a Karatsuba-shaped
53-bit significand multiply. The final checked-in assets are the `.empbc` files,
not the intermediate generator output.

## Current AND Counts

These counts are from the current `.empbc` assets. XOR/NOT are also stored in the
file, but AND gates are the dominant cost in modern garbling. `abs`, `neg`, and
`copysign` are direct bit operations and have no asset file.

| op | fp16 | fp32 | fp64 |
|---|---:|---:|---:|
| add | 579 | 1143 | 2840 |
| sub | 579 | 1141 | 2840 |
| mul | 717 | 2286 | 8575 |
| square | 478 | 1876 | 6578 |
| fma | 1296 | 3429 | 11415 |
| div | 2646 | 7845 | 18023 |
| recip | 2264 | 5863 | 15391 |
| sqrt | 270 | 5699 | 17880 |
| rsqrt | 4256 | 11179 | 33273 |
| eq/ne | 57 | 111 | 213 |
| lt/le/gt/ge | 58 | 112 | 214 |
| min | 101 | 200 | 395 |
| max | 90 | 176 | 342 |
| isnan/isinf/iszero | 14 | 30 | 62 |

## Replacement Checklist

Before replacing a checked-in `.empbc`, confirm all of the following:

1. The candidate loads successfully through `load_empbc_file()`.
2. The candidate has the expected input count, output count, and LSB-first bit
   order.
3. A width-appropriate verifier passes against host behavior or the native C
   reference.
4. Any special edge-case verifier for the operation passes.
5. AND counts have been compared with the table above, and any increase has a
   clear correctness or maintainability reason.
6. emp-tool's `test_float` passes.
7. The downstream protocol test that exercises float circuits passes, when that
   protocol supports the current float API.

## Third-Party Provenance

Some fp32/fp64 assets were generated from modified sources derived from Berkeley
SoftFloat 3e. The asset README in `emp-tool/ir/files/README.md` carries the
SoftFloat notice so it is installed beside the generated `.empbc` files.
