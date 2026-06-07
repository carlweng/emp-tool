# Circuit frontend (`emp-tool/frontend/`)

A small, optional layer for **writing a pure circuit once and running it on any
context** — plaintext, garbled 2PC, ZK, … . You write the circuit in ordinary
typed circuit-value code (`Bit<Ctx>`, `UInt<Ctx,N>`, `Int<Ctx,N>`,
`Float<Ctx,W>`); the frontend lets you call it live, or `compile` it once into a
reusable, **context-free** `Circuit` and `run` it on whatever context you hold —
with no global backend and no per-bit virtual dispatch.

Everything lives in namespace **`emp::frontend`** (so `run` / `compile` don't
pollute `emp`). Header-only over emp-tool; **C++20**. Pull it in with
`#include "emp-tool/frontend/frontend.h"` (or directly
`#include "emp-tool/frontend/circuit_fn.h"`).

This is the BooleanContext frontend. The legacy `Bit_T`/global-`Backend` frontend
(`executor.h`, `TypedCircuit`, `setup_clear_backend` + `run`/`compile` over a
global backend) is **retired**; `executor.h` is now a `#error` migration stub.

For the typed values it builds on, read [circuits.md](circuits.md); for the
context concept it targets, read the header of `circuits/context.h`.

## What a circuit is — a pure function over a context

A circuit body takes typed circuit values as **arguments** and returns a typed
value; it does **no I/O of its own** (no `input`/`reveal`/OT). This is enforced
structurally: a body is recorded through a `RecordContext`, which has no I/O.

- **No secret input inside** — pass secret/party inputs as arguments.
- **No `reveal` inside** — reveal the returned value *outside*, on the session.
- **Public constants inside are fine** — `a.constant(5)` (implicit form) or
  `UInt<Ctx,N>::constant(ctx, 5)` (explicit form) fold to constant gates. A value
  that may differ across parties or runs must be an **argument**.

I/O stays the caller's job, around the circuit:

```cpp
auto a = sess.input<UInt<SH2PCContext,32>>(ctx, ALICE, av);   // session feeds inputs
auto b = sess.input<UInt<SH2PCContext,32>>(ctx, BOB,   bv);
auto c = frontend::run(ctx, circuit, a, b);                   // pure replay
uint32_t r = sess.reveal(c, PUBLIC);                          // session reveals
```

## Body forms

A body comes in two forms; `compile`/`run` detect which is invocable. A body
callable in **both** is a contract error (disambiguate).

- **implicit context** (default) — the typed values carry their own context, so
  the body needs none; make a constant from an argument with `a.constant(v)`:

  ```cpp
  auto add = [](auto a, auto b) { return a + b; };
  ```

- **explicit context** (general) — the body takes `Ctx&` first. Required for
  **nullary** circuits and for making a constant with no argument to anchor on:

  ```cpp
  auto bias = [](auto& ctx) {
      using C = std::remove_reference_t<decltype(ctx)>;
      return UInt<C,16>::constant(ctx, 4242);
  };
  ```

### The calling contract

One diagnostic site (`circuit_fn_traits` / `circuit_contract` in `circuit_fn.h`):
a body must be **callable with prvalue circuit-value arguments** and **return a
circuit value by value**. Arguments are passed by value, so a body cannot mutate
them (a non-`const` lvalue-reference parameter is rejected). Returning a
**reference**, returning **void**, returning a **non-circuit** value, taking a
non-circuit argument, or being callable in **both** context forms are all
compile-time errors with a precise message — in `compile<Shapes...>` and live
`run` alike.

## Shapes — context-free signatures

A **shape** is "a typed value minus its context": its bit width, its host clear
type + codec, and which value it `bind`s to per context. Shapes are how a
compiled circuit's signature stays context-free.

| value (per context)  | shape           |
|----------------------|-----------------|
| `Bit<Ctx>`           | `BitShape`      |
| `UInt<Ctx,N>`        | `UIntShape<N>`  |
| `Int<Ctx,N>`         | `IntShape<N>`   |
| `Float<Ctx,W>`       | `FloatShape<W>` |

`Shape::bind<Ctx>` re-attaches a context (`UIntShape<32>::bind<Ctx> ==
UInt<Ctx,32>`); the clear codec lives on the shape and the value's
`encode`/`decode` forward to it. See `circuits/shape.h`.

## compile / run

```cpp
#include "emp-tool/frontend/circuit_fn.h"
namespace cf = emp::frontend;

auto add  = [](auto a, auto b) { return a + b; };
auto circ = cf::compile<UIntShape<32>, UIntShape<32>>(add);   // record ONCE

ClearContext cx;                                              // run on any context
auto x = UInt<ClearContext,32>::constant(cx, 7);
auto y = UInt<ClearContext,32>::constant(cx, 5);
auto z = cf::run(cx, circ, x, y);                            // replay -> UInt<ClearContext,32>
```

- **compiled** — `compile<ArgShapes...>(body)` records the body once through a
  `RecordContext` and returns a `Circuit<RetShape, ArgShapes...>`.
  `run(ctx, circ, args...)` replays it on the live `ctx` (args **by const-ref**;
  the context is **explicit** — no global backend). The same `Circuit` runs
  identically on `ClearContext`, `SH2PCContext`, etc. — user circuits are as
  portable as the built-in `.empbc` files.
- **live** — `run(body, args...)` invokes the body directly on already-live typed
  values (it recovers the context from the first argument). Same contract.

`compile` is host-side and deterministic: all parties compile the identical
program, then replay it in lockstep. (A `Float` body inlines its `fp<W>_*.empbc`
gates into the recording, so a recorded float circuit is semantically — not
necessarily gate-for-gate — equal to the standalone builtin.)

## The compiled circuit object

`compile` returns a `Circuit<RetShape, ArgShapes...>` wrapping a
`circuit::CircuitArtifact` (the flat `BooleanProgram` + a `CircuitSignature` of
argument widths and the return width). The artifact is **private and immutable**;
the constructor validates the program structurally *and* that the signature
matches the declared shapes, so a stale or mis-typed loaded artifact is rejected
at construction rather than silently mis-running. Accessors: `circ.program()`,
`circ.signature()`.

No analyses are baked into the circuit — gate counts, liveness, and the AND-depth
schedule are free functions over the program (`frontend/passes.h`,
`circuits/context.h`), computed when wanted.

## What's inside (internals)

- `circuits/shape.h` — `Shape` concept + `BitShape`/`UIntShape<N>`/`IntShape<N>`/
  `FloatShape<W>` (width, clear codec, `bind<Ctx>`).
- `circuits/typed.h` — the typed values `Bit`/`UInt`/`Int`/`Float<Ctx>`.
- `circuits/context.h` — `RecordContext` (records a `BooleanProgram`),
  `execute_program(ctx, prog, inputs, ws)` (value-return replay over any
  `BooleanContext`), `ProgramWorkspace`.
- `circuits/circuit_artifact.h` — `CircuitArtifact` (program + signature) +
  `validate_artifact`.
- `frontend/circuit_fn.h` — the `CircuitValue` concept, `circuit_fn_traits` /
  `circuit_contract`, `Circuit<RetShape,ArgShapes...>`, `compile`, `run`.
- `frontend/passes.h` — analyses over the IR (`count`, `liveness`, `schedule`,
  `layout`) as free functions.

## Adding a new backend

Define a type satisfying the `BooleanContext` concept (a `std::regular` `Wire`
plus value-return `public_bit`/`and_gate`/`xor_gate`/`not_gate`). Every compiled
circuit then replays on it via `run(ctx, circ, …)` with no frontend changes — the
generic replay walks the gate list issuing the context's gate ops. A
round-sensitive protocol (e.g. GMW) gets efficiency by consuming the program's
AND-depth schedule (a `BulkBooleanContext` + `scheduled_execute_program`), not the
scalar replay.

## Tests

- `emp-tool/test/test_circuit_fn.cpp` — compile-once / run-on-any-context on
  `ClearContext` (incl. both body forms, a nullary circuit, `.constant()`, fp32),
  plus the size-optimal 31-AND adder and deterministic recording.
- `emp-tool/test/circuit_fn_contract_probes.cpp` — the contract's positive case +
  the negative cases that must fail to compile with the expected message.
- `emp-sh2pc/test/test_circuit_fn_sh2pc.cpp` — the same compiled `Circuit` run
  two-party over the garbled `SH2PCContext` (uint32 + fp32).
