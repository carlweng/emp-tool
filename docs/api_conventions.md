# Public API conventions

Cross-cutting conventions for emp-tool public APIs. Read this before
adding or changing an exported function that takes a byte/block/bool
count, or before choosing a PRG buffer-fill API.

## Length and count parameters

Buffer-length and count parameters on emp-tool's public API use
`int64_t`, not `int` or `size_t`.

Why:

- `int` overflows at 2^31 elements.
- unsigned `size_t` underflows silently in `len -= batch` style loops
  that decrement to zero.
- `int64_t` avoids both and matches the existing IO / crypto APIs.

New emp-tool APIs that take a "number of bytes / blocks / bools /
points" parameter follow this convention. Internal counters and indices
inside those bodies should match:

```cpp
for (int64_t i = 0; i < len; ++i) {
    // ...
}
```

Template non-type parameters stay as `int` / `size_t` when they are
compile-time bounded sizes (`int N`, `int K`, `size_t Width`, etc.).

## PRG buffer fills

`PRG::random_data` requires a 16-byte-aligned destination and asserts
in debug builds. Use `PRG::random_data_unaligned` for stack integers,
small structs, byte buffers with arbitrary offset, and any other
destination that is not naturally `block`-aligned.

Use the aligned fast path only when the destination is known to be
16-byte aligned, such as a `block*` or a suitably aligned block buffer.
