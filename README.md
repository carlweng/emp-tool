# emp-tool

![build](https://github.com/emp-toolkit/emp-tool/workflows/build/badge.svg)
[![CodeQL](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml/badge.svg)](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml)

<img src="https://raw.githubusercontent.com/emp-toolkit/emp-readme/master/art/logo-full.jpg" width=300px/>

> **Which version do I want?**
>
> - **Existing projects pinned to a published release: stay on `0.3.0`** —
>   tag [`0.3.0`](https://github.com/emp-toolkit/emp-tool/releases/tag/0.3.0)
>   or branch [`v0.3.x`](https://github.com/emp-toolkit/emp-tool/tree/v0.3.x).
>   Bug fixes and security patches will be backported to `v0.3.x`.
> - **New projects, or willing to migrate: track the development branch**
>   (this branch). Its CMake package metadata is already `1.0.0` for
>   local development, but the API is still pre-alpha and not frozen.
>   Faster, cleaner APIs (test-mode determinism, trace I/O, refactored
>   AES/PRG/group), but headers and names may still move before the
>   first tagged alpha/release.

Foundational primitives for the emp-toolkit family: SIMD `block` types,
fast AES / PRG / PRP / hash / GF(2^128) kernels, OpenSSL-backed elliptic
curve ops, IO channels, a templated boolean-circuit frontend
(`Bit_T<Wire>` / `BitVec_T<Wire>` / `UnsignedInt_T<Wire>` /
`SignedInt_T<Wire>` / `Float_T<Wire>`), and pluggable execution backends
(plaintext circuit printer, half-gate garbling, privacy-free garbling).

## Requirements

- CMake ≥ 3.25
- A C++17 compiler (Clang ≥ 12, GCC ≥ 9, AppleClang 14+)
- OpenSSL ≥ 3.0
- pthreads
- x86_64 with AES-NI + PCLMULQDQ + SSE4.2, **or** arm64 with `armv8-a+crypto+crc`. The default build uses `-march=native` and pulls in VAES, VPCLMULQDQ, AVX-512 etc. wherever the host CPU has them; pass `-DEMP_TOOL_NATIVE_ARCH=OFF` for a portable binary tied only to the baseline above.

## Build and install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build           # respects CMAKE_INSTALL_PREFIX
```

The default build is tuned for performance: `Release`, `-O3
-funroll-loops`, and `-march=native` so VAES / VPCLMULQDQ / AVX-512 etc.
are used wherever the host CPU supports them. **Binaries built this way
are tied to the build machine's CPU** — they will SIGILL on a CPU
missing any instruction the build host had. To produce a portable
binary that runs on any AES-NI + PCLMUL + SSE4.2 (x86_64) or
`armv8-a+crypto+crc` (arm64) machine, pass
`-DEMP_TOOL_NATIVE_ARCH=OFF`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `EMP_TOOL_NATIVE_ARCH` | `ON` | Build with `-march=native`. Best performance, host-CPU-locked binary. Set `OFF` for portable binaries. |
| `EMP_TOOL_BUILD_TESTS` | `ON` when top-level | Build the test suite under `test/`. |
| `EMP_TOOL_BUILD_BENCHMARKS` | `OFF` | Build throughput benchmarks under `bench/`; not registered with `ctest`. |
| `EMP_TOOL_INSTALL` | `ON` when top-level | Generate install + export rules. |
| `EMP_TOOL_THREADING` | `OFF` | Make the global `Backend* backend` pointer thread-local. Required if multiple threads run circuits concurrently against different backends. |

## Consuming from another CMake project

After `cmake --install build`:

```cmake
find_package(emp-tool CONFIG REQUIRED)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

Without installing, the build tree exports its own targets file:

```cmake
find_package(emp-tool CONFIG REQUIRED PATHS /path/to/emp-tool/build)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

Or as a subdirectory:

```cmake
add_subdirectory(third_party/emp-tool)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

A single header pulls in the substrate (core / crypto / IO / backends /
wire-templated circuit classes):

```cpp
#include <emp-tool/emp-tool.h>
using namespace emp;
```

Circuit aliases are explicit because downstream protocol libraries bind their
own wire types. `emp-tool.h` makes the standard `block` wire aliases available
under `emp::block_types`; applications opt into that nested namespace:

```cpp
using namespace emp::block_types;  // in .cpp files
```

## Layout

```
emp-tool/
├── core/         block, constants, utils
├── crypto/       PRG, PRP, AES, Hash, CCRH, MITCCRH, f2k, ec (Scalar/Point/ECGroup)
├── io/           IOChannel, NetIO, TLSIO, TraceIO
├── execution/    Backend interface, ClearBackend, HalfGate*, PrivacyFree*
├── circuits/     Bit, BitVec, UnsignedInt, SignedInt, Float (all templated on Wire); BooleanProgram IR + .empbc format
├── frontend/     run / compile / replay pure circuit functions through any backend (emp::frontend)
└── third_party/  ThreadPool, sse2neon
```

`circuits/` is templated on the wire type; `emp::block_types` provides the
standard aliases (`Bit`, `BitVec`, `UnsignedInt`, `SignedInt`, `Float`)
all over `block`, so most application code only needs one explicit opt-in.

The numeric layer makes signedness explicit: `UnsignedInt` wraps mod
2^N matching `uint{N}_t`, `SignedInt` is two's-complement matching
`int{N}_t` on hardware (C signed-overflow UB is sidestepped — emp-tool
wraps deterministically), and `BitVec` carries no arithmetic at all
(just bitwise / shifts / slice / concat). `UnsignedInt` and `SignedInt`
inherit from `BitVec` so they pick up the structural ops; conversion
between signed and unsigned is an explicit `.as_signed()` /
`.as_unsigned()` bit-cast (no gates).

## Usage

### PRG

```cpp
PRG prg;                                         // secure random seed
block rand_block[3];
int rand_int;

prg.random_block(rand_block, 3);                 // 3 × 128 random bits
prg.random_data_unaligned(&rand_int, 4);         // arbitrary-aligned dest

prg.reseed(&rand_block[1]);                      // reset seed + counter
```

`random_data` (16B-aligned) is the fast path; use `random_data_unaligned`
for any destination that isn't naturally 16-byte aligned (stack ints,
small structs, etc.) — the aligned variant asserts in debug.

### PRP / CCRH

`PRP` is the bare AES wrapper; the hash variants sit on top of it:

| Class | Models |
|---|---|
| `CCRH`    | circular correlation-robust hash |
| `MITCCRH` | multi-instance tweakable CCRH |

```cpp
block key;
PRG().random_block(&key, 1);

PRP prp(key);
block buf[64];
prp.permute_block(buf, 64);                      // in-place AES of 64 blocks

CCRH ccrh;
block out[8];
ccrh.H<8>(out, buf);                             // compile-time batch
block one = ccrh.H(buf[0]);                      // single-block form
```

CCRH has three call shapes: a scalar `H(block)` returning one block, a
templated batched `H<n>(out, in)` that the compiler unrolls (best up to
n ≤ 16, beyond which register spills hurt throughput), and a runtime
`Hn(out, in, n)` for large batches. MITCCRH has a different shape — see
`crypto/mitccrh.h`. The plain non-circular CRH used to be a separate
class but has been removed; CCRH supersedes it (its `sigma` preprocessing
costs roughly half a cycle per block in bulk and rules out a footgun
class of misuse where `H(in)` and `H(in ⊕ Δ)` are correlated).

### Hash (SHA-256)

```cpp
Hash hash;
char data[1024];
char dig[Hash::DIGEST_SIZE];                     // 32 bytes

hash.put(data, sizeof(data));
hash.digest(dig);                                // resets after digesting
```

### GF(2^128) multiplication

`block` is a typedef for `__m128i`, so the f2k kernels accept it directly.

```cpp
block a, b, c;
PRG prg;
prg.random_block(&a, 1);
prg.random_block(&b, 1);
gfmul(a, b, &c);                                 // c = a · b in GF(2^128)
```

### Elliptic curves

`ECGroup` wraps an OpenSSL `EC_GROUP` + `BN_CTX`. Default curve is
P-256; pass any OpenSSL `NID_*` to the constructor to switch.
`Scalar` and `Point` are the corresponding handles.

```cpp
ECGroup G;                                       // P-256 by default
Scalar a = G.rand_scalar();                      // uniform in [0, order)
Point P = G.mul_gen(a);                          // P = a · G_generator

// Hash to curve, RFC 9380 §6 SSWU_RO_. Each protocol must pick its
// own domain-separation tag (DST); there's no default — sharing a
// DST across protocols defeats the point.
const char dst[] = "my-protocol:v1";
Point T = G.hash_to_point("my message", 10, dst, sizeof(dst) - 1);
```

### Network IO

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
io.send_data(buf, n);                            // buffered
io.flush();                                      // drain outbound
io.recv_data(buf, n);                            // blocks until n bytes arrive
```

### Plaintext circuit evaluation

The simplest backend evaluates `Bit` / `BitVec` / `UnsignedInt` /
`SignedInt` / `Float` in cleartext (and can optionally capture the
circuit it executed as a native `.empbc` file):

```cpp
using namespace emp::block_types;

setup_clear_backend();                           // installs ClearBackend

SignedInt a(32, 7,  PUBLIC);
SignedInt b(32, 35, PUBLIC);
SignedInt c = a * b + SignedInt(32, 1, PUBLIC);
std::cout << c.reveal<int32_t>() << "\n";        // 246

// Wrap on overflow is well-defined and matches int32_t / uint32_t hardware:
UnsignedInt big(32, UINT32_MAX, PUBLIC);
std::cout << (big + UnsignedInt(32, 1u, PUBLIC)).reveal<uint32_t>() << "\n"; // 0

finalize_clear_backend();
```

To capture the executed circuit as a native `.empbc` file, pass a filename
to `setup_clear_backend("circuit.empbc")`; it is written on
`finalize_clear_backend()`. (Capture requires all secret feeds before any
gate — inputs are the leading wires.)

### Circuit frontend: run and compile

Write a **pure circuit function** (inputs are arguments, the output is the
return value — no `feed`/`reveal` inside) and run it through the installed
backend: call it directly, or **compile it once into a reusable circuit**
(carrying size/depth stats) and replay it with fresh inputs. I/O stays in
direct mode, around the circuit. Add `#include <emp-tool/frontend/frontend.h>`
for the frontend API and opt into `emp::block_types` for the standard block
aliases.

```cpp
#include <emp-tool/frontend/frontend.h>
using namespace emp::block_types;

setup_clear_backend();                           // any backend (here: cleartext)
auto add = [](auto a, auto b){ return a + b; };

UInt32 x(32, 7, PUBLIC), y(32, 5, PUBLIC);       // inputs: direct mode
UInt32 z = frontend::run(add, x, y);             // run as a function -> 12

auto circ = frontend::compile<UInt32, UInt32>(add);  // pre-built once (+ stats)
UInt32 r1 = frontend::run(circ, x, y);                               // replay ...
UInt32 r2 = frontend::run(circ, UInt32(32,100,PUBLIC), UInt32(32,23,PUBLIC)); // ... reused
// inspect: circ.circuit.count.num_and, circ.circuit.schedule.levels.depth, ...
```

Outputs are live wires, so results chain into more circuit code and are
revealed whenever. The same `compile` / `run` drive any backend — the
cleartext one above, or a protocol such as AG2PC. See
[docs/frontend.md](docs/frontend.md).

### Native circuit files (`.empbc`)

Circuits load from the native binary `.empbc` format into one
`emp::circuit::BooleanProgram` (flat: inputs are wires `[0, num_inputs)`,
outputs are an explicit wire list) and run through the shared evaluator. The
loader validates structure (bounds, single-definition, topological order) and
rejects malformed files. No `.empbc` assets are shipped yet; generate or
capture one and load it through this API.

```cpp
using namespace emp::block_types;
using namespace emp::circuit;

BooleanProgram program = load_empbc_file("my_circuit.empbc");

// Dispatcher: realize each gate op on Bit slots through the active backend.
struct BitCompute {
    void and_gate(Bit& o, const Bit& a, const Bit& b) { o = a & b; }
    void xor_gate(Bit& o, const Bit& a, const Bit& b) { o = a ^ b; }
    void not_gate(Bit& o, const Bit& a)               { o = !a; }
    void const_gate(Bit& o, bool v)                   { emp::backend->public_label(&o.bit, v); }
};

std::vector<Bit> input(program.num_inputs), output(program.outputs.size());
// ... feed inputs via your protocol ...
CircuitScratch<Bit> sc;
execute_program<Bit>(program, input.data(), input.size(),
                     output.data(), output.size(), sc, BitCompute{});
```

### Custom wire type

If you implement your own backend with a non-`block` wire (e.g. an
arithmetic share, a long bytestring), instantiate the circuit templates
on it directly, or bind aliases in your own namespace. `emp-tool.h` defines
no bare aliases in `emp`, so protocol libraries can include it safely; just
don't opt into `emp::block_types` unless you explicitly want the block binding.

```cpp
#include <emp-tool/execution/backend.h>
#include <emp-tool/circuits/bit.h>
#include <emp-tool/circuits/bitvec.h>
#include <emp-tool/circuits/unsigned_int.h>
#include <emp-tool/circuits/signed_int.h>

struct MyWire { /* ... */ };
class MyBackend : public emp::Backend { /* override wire_bytes/and_gate/... */ };

emp::backend = new MyBackend(/* args */);

using MyBit         = emp::Bit_T<MyWire>;
using MyBitVec      = emp::BitVec_T<MyWire>;
using MyUnsignedInt = emp::UnsignedInt_T<MyWire>;
using MySignedInt   = emp::SignedInt_T<MyWire>;
```

The class definitions in `circuits/` carry no `block` of their own; the
standard `block` commitment lives only in `emp::block_types`.

## Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Each test file under `test/` doubles as a tutorial for the
corresponding header — see `docs/test_conventions.md` for the file conventions
(`example()` / `run_correctness()` per file).

### Benchmarks

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DEMP_TOOL_BUILD_BENCHMARKS=ON
cmake --build build -j
./build/bench/bench_aes 0.3
./run ./build/bench/bench_netio
```

Benchmarks are separate from `ctest` and live under `bench/`; see
`docs/benchmark_conventions.md`.

### Wire-byte equivalence (test mode)

Setting `EMP_TEST_MODE=1` swaps every randomness source in the
toolkit (`PRG()` default-construction, `ECGroup::rand_scalar`) for a
deterministic counter-derived stream so two runs of the same code
produce byte-identical wire output. Combined with `TraceIO` (an
`IOChannel` adapter that tees wire bytes to a file), this lets you
verify that an optimization or refactor doesn't change a protocol's
observable behavior:

```bash
EMP_TEST_MODE=1 ./run ./build/your_protocol_test before
# … apply your refactor …
EMP_TEST_MODE=1 ./run ./build/your_protocol_test after
diff before.alice.send after.alice.send   # must be empty
diff before.alice.recv after.alice.recv   # must be empty
```

See [docs/test_mode.md](docs/test_mode.md) for the full design,
determinism contract, and limitations.

## [Acknowledgement, Reference, and Questions](https://github.com/emp-toolkit/emp-readme/blob/master/README.md#citation)

## License

Licensed under the Apache License, Version 2.0 — see [LICENSE](LICENSE).
