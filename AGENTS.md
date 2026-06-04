# Agent guide for emp-tool

Entry point for AI coding agents working on this repository. Read this
file first, then load only the subdocs relevant to your task.

## Project at a glance

emp-tool is the foundation of the EMP toolkit: a header-mostly C++
library providing the circuit primitives (`Bit` / `BitVec` /
`UnsignedInt` / `SignedInt` / `Float`), backend execution (clear /
half-gate / privacy-free), IO channels (`NetIO`, `TLSIO`), and
crypto utilities (AES, hash, PRG, group ops) that the higher-level
emp-ot, emp-sh2pc, emp-ag2pc, emp-agmpc protocols build on.

Standard users include `emp-tool/emp-tool.h` for the substrate and opt
into `emp::block_types` when they want the `block`-typed aliases.
`emp-tool.h` intentionally defines no bare circuit aliases in `emp`, so
downstream protocol libraries can include it safely and bind their own
wire types.

## When to read what

Pick the smallest set of subdocs that covers your task. Each is
self-contained and assumes you've read this index.

| Task | Subdoc(s) |
|---|---|
| Modify a circuit primitive header (`Bit_T`, `BitVec_T`, `UnsignedInt_T`, `SignedInt_T`, `Float_T`) | [docs/circuits.md](docs/circuits.md) + [docs/numeric_semantics.md](docs/numeric_semantics.md) |
| Write or modify a `Backend` subclass (gate dispatch, garbling) | [docs/backend.md](docs/backend.md) |
| Run / compile / replay a pure circuit function through a backend (`frontend::run` / `compile`) | [docs/frontend.md](docs/frontend.md) |
| Write protocol code that uses NetIO (sends, recvs, multi-thread, flush) | [docs/io_channel.md](docs/io_channel.md) |
| Investigate a NetIO deadlock | [docs/io_channel.md](docs/io_channel.md) |
| Translate ordinary C++ / Python to an EMP secure circuit | [docs/EMP_TRANSLATION.md](docs/EMP_TRANSLATION.md) |
| Write or modify a `test/test_*.cpp` file | [docs/test_conventions.md](docs/test_conventions.md) |
| Write or modify a `bench/bench_*.cpp` file | [docs/benchmark_conventions.md](docs/benchmark_conventions.md) |
| Add or modify public API length/count parameters, or choose `random_data` vs `random_data_unaligned` | [docs/api_conventions.md](docs/api_conventions.md) |
| Verify wire-byte equivalence after a refactor / optimization (deterministic PRG, `TraceIO`) | [docs/test_mode.md](docs/test_mode.md) |
| Verify or debug a numeric corner case (wrap, division, shifts, resize) | [docs/numeric_semantics.md](docs/numeric_semantics.md) |
| Convert between byte buffers and `BitVec` / `Bit[]`, or debug an endianness mismatch | [docs/circuits.md § Bit / byte ordering](docs/circuits.md) |
| Add a new file-scope `block` / `__m128i` / non-`constexpr`-initialized global, or debug a "constant is silently zero in some binaries" bug | [docs/static_init.md](docs/static_init.md) |
