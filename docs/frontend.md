# Circuit frontend (`emp-tool/frontend/`)

A small, optional layer for **running a pure circuit function through any
backend**. You write the function in ordinary `Bit` / `Integer` code; the
frontend lets you call it directly, or compile it once into a reusable
circuit object (with size/depth stats) and replay it — all through the same
global `Backend`, so the identical code drives `ClearBackend`, AG2PC, or any
future protocol with no frontend changes.

Everything lives in namespace **`emp::frontend`** (so the short names
`run` / `compile` don't pollute `emp`). Header-only; compiles against
emp-tool alone. Pull it in with `#include "emp-tool/frontend/frontend.h"`,
which also injects suffixed `*_rec` aliases (`Bit_rec`, `UInt32_rec`, …) used
only internally for recording.

For the circuit-value interface this layer relies on, read
[circuits.md § Circuit-value interface](circuits.md). For the backend seam it
drives, read [backend.md](backend.md).

## What a circuit is — a pure function

A frontend circuit takes EMP values as **arguments** and returns an EMP value;
it does **no I/O of its own**:

- **No secret `feed` inside** — pass secret/party inputs as arguments.
- **No `reveal` inside** — reveal the returned value *outside*, in direct mode.
- **Literal public constants inside are fine** — `Integer iv(32, 0x6a09e667,
  PUBLIC)` folds to constant gates. But a public feed *bakes its value* into the
  circuit, so only literals / values all parties agree on at compile time belong
  inside; any public value that may differ across parties or runs must be an
  **argument**, not fed inside.

`compile` **enforces** this: a non-public `feed` or any `reveal` in the body is
a hard error (`RecordBackend` rejects them).

Why: `compile` traces *one* execution of the body with placeholder values, so
any **host** control flow that depended on a runtime value would be frozen into
the trace (e.g. `if (x.reveal()) …` would always take the record-time branch).
The pure-function rule removes that footgun: a circuit's *shape* cannot depend
on input or revealed values. (Branching on a **public scalar** — a width, a loop
bound, `party` — is fine; it shapes the circuit identically every run.)

So I/O stays the caller's job, done in **direct mode** around the circuit:
build the input `Integer`s, run the circuit, reveal the returned `Integer`s.

## Running a circuit

```cpp
setup_clear_backend("");                            // or setup_ag2pc(...)
auto add = [](auto a, auto b){ return a + b; };

UInt32 x(32, 7, PUBLIC), y(32, 5, PUBLIC);          // inputs: direct mode
UInt32 z  = frontend::run(add, x, y);               // LIVE: call the body
auto   c  = frontend::compile<UInt32,UInt32>(add);  // COMPILE once (+ stats)
UInt32 z2 = frontend::run(c, x, y);                 // replay; reuse c, fresh inputs
bool   r  = z2[0].reveal<bool>(PUBLIC);             // reveal outside, direct mode
```

- **live** — `run(body, args…)` invokes the body and returns its typed result.
- **compiled** — `compile(...)` records once into a circuit; `run(circuit, …)`
  replays it. Both return live EMP values, so circuits **chain** (one result
  feeds the next) and you reveal at the end. (Plain `Integer x(...); x.reveal()`
  is *direct mode* — that's how you do I/O; it is not "the frontend".)

## Bodies must be wire-generic

`run`/`compile` instantiate the body on the recording wire (`RecWire`) *and* on
the live wire, so the body cannot pin a wire type. Three legal spellings:

- `auto` generic lambda: `[](auto a, auto b){ return a + b; }`
- templated functor (C++17): `struct F { template<class W>
  UInt32_T<W> operator()(UInt32_T<W>, UInt32_T<W>) const; };`
- C++20 template lambda: `[]<class W>(UInt32_T<W> a, UInt32_T<W> b){ … }`

A body pinned to a concrete wire (`[](UInt32 a, …)` with `UInt32 =
…<AG2PCWire,32>`) compiles for the live `run(body, …)` only and fails `compile`.

## `compile`: two ways to fix input shapes

- `compile<UInt32, UInt32>(body)` — input **types** as template args; the width
  comes from the type (fixed-width types / `Bit` / `Float`). No dummy values.
- `compile(body, a, b)` — sample **values**; only their shape is read (handy
  when you already hold inputs, or for runtime-width Integers).

Both capture the body's returned value as the circuit output. `compile` records
via an internal `RecordBackend`, restoring the previous global `backend` on the
normal path (it is not exception-safe — don't throw out of a body mid-record).

## The compiled circuit object

`compile` returns a `Circuit` (the typed `compile` wraps it in a
`TypedCircuit<Ret>` that also remembers the return type). It is plain,
backend-independent data — the recorded `BooleanProgram` (gates, argument input
ports, output wire ids) plus stats computed once:

- `circuit.count` — `num_and`, `num_xor`, `num_not`, `num_wire`, input/output
  bit counts.
- `circuit.schedule.levels` — AND-depth and the gates grouped per level.
- `circuit.liveness` / `circuit.layout` — last-use and per-gate frees, for slot
  recycling.

Because it's data, it's **reusable and inspectable**: build it once, read
`circ.circuit.count.num_and` / `circ.circuit.schedule.levels.depth`, and
`run(circ, …)` it as
many times as you like with fresh inputs — the body's C++ isn't re-traversed on
replay. The gate *shape* is value-free (it depends only on public structure, not
on argument values), so all parties build the same `Circuit` — provided the body
obeys the contract above (no branch on a revealed value; public feeds are
literals/agreed constants, not per-party runtime values).

## Typed I/O and `rebind`

A compiled body records on `RecWire` but replays on the live wire, so the
returned `Integer` must be rebuilt over the live wire. The IR/gates are
**wire-free** and reused as-is; only the input/output *wrappers* are wire-typed.
`rebind` is the compile-time map "same shape, different wire"
(`UnsignedInt_T<RecWire,32> → UnsignedInt_T<W,32>`); it moves no data — the wires
are carried by `pack`/`unpack`. It exists solely to hand back the exact
`Integer`/`Bit`/`Float` type rather than an anonymous wire bundle. See
[circuits.md § Circuit-value interface](circuits.md).

## What's inside (internals)

- `boolean_program.h` — the protocol-neutral IR: `Gate`
  (`AND`/`XOR`/`NOT`/`CONST0`/`CONST1`), `InputPort{base, n}` (one per
  argument), `outputs` (the return value's wire ids), `BooleanProgram`.
- `record_backend.h` — `RecWire` + `RecordBackend` (a `Backend` that records
  operations into a `BooleanProgram`; rejects secret `feed` and `reveal`).
- `passes.h` — analysis over the IR: `count`, `liveness`, `schedule`
  (AND-depth), `layout`.
- `circuit.h` — `Circuit` (program + stats).
- `executor.h` — `compile` (record + stats), `run` (live + compiled replay),
  and the `make_external` / `record_typed_` helpers.

## C++ standard

The frontend headers are C++17. **Template-lambda bodies** (`[]<class W>(…)`)
are C++20 — opt in per target only where used (e.g. the `add_test_case_cxx20`
test macro); the library stays C++17 (its `cxx_std_17` is a floor, so a single
C++20 target resolves cleanly).

## Adding a new backend

Implement the `emp::Backend` virtuals and give it a wire type; the frontend
(live `run`, `compile`, replay, every circuit type) then works with **no
frontend changes** — that's the payoff of routing through the backend seam.

The generic replay walks the gate list in recorded order issuing scalar
`backend->{public_label,and,xor,not}` calls — that delivers **correctness** on
any backend. It does **not** itself batch by round: a round-sensitive protocol
(e.g. GMW) gets efficiency by *consuming the `Circuit`* (reading
`schedule.levels`) through its own execution path, not via the generic replay.
That backend-specific `Circuit` consumer is future work; the stats are recorded
for it. See [backend.md](backend.md).

## Tests

- `test/test_frontend.cpp` (emp-tool) — live + compiled modes and mode-switching
  on `ClearBackend` (plaintext), the standalone backend-neutral proof.
- `emp-ag2pc/test/test_frontend*.cpp` — the same `compile`/`run` over a real
  protocol (compiled replay, chaining, template-lambda body).
