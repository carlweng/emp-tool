#ifndef EMP_CIRCUIT_CONTEXT_H__
#define EMP_CIRCUIT_CONTEXT_H__

// The C++20 BooleanContext contract and the reusable contexts that realize it.
// A circuit kernel is `template<BooleanContext Ctx> auto k(Ctx&, wires...) ->
// wires`; gates are value-return over a cheap, copyable `Ctx::Wire` handle, so
// dispatch is static and inlineable. Heavy protocol state lives in the context,
// keyed by the handle. Large circuits are recorded once into a BooleanProgram
// (RecordCtx) and replayed through any context via execute_program(ctx,...).
//
// This is the canonical BooleanContext home. Requires C++20 (concepts,
// std::regular, std::span). It includes the C++17 IR headers, which compile fine
// in a C++20 TU.

#include "emp-tool/circuits/boolean_program.h"
#include "emp-tool/frontend/passes.h"     // schedule_pass for scheduled_execute_program
#include "emp-tool/core/utils.h"          // error()
#include <concepts>
#include <cstdint>
#include <span>
#include <vector>

namespace emp {

// ---------------------------------------------------------------------------
// The contract. Wire is a cheap, copyable, regular handle (no move-only: fan-out
// needs copies). Gates are value-return.
// ---------------------------------------------------------------------------
template <class Ctx>
concept BooleanContext =
    std::regular<typename Ctx::Wire> &&
    requires(Ctx& c, typename Ctx::Wire a, typename Ctx::Wire b, bool v) {
        { c.public_bit(v) } -> std::same_as<typename Ctx::Wire>;
        { c.and_gate(a, b) } -> std::same_as<typename Ctx::Wire>;
        { c.xor_gate(a, b) } -> std::same_as<typename Ctx::Wire>;
        { c.not_gate(a) }    -> std::same_as<typename Ctx::Wire>;
    };

// ---------------------------------------------------------------------------
// ClearCtx — plaintext evaluation. Wire = uint8_t bit (0/1), no folding, so
// gate counts match a recorded program exactly. The crypto-free static arm.
// ---------------------------------------------------------------------------
struct ClearCtx {
    using Wire = uint8_t;
    Wire public_bit(bool v)       { return v ? 1 : 0; }
    Wire and_gate(Wire a, Wire b) { return a & b; }
    Wire xor_gate(Wire a, Wire b) { return a ^ b; }
    Wire not_gate(Wire a)         { return a ^ 1; }
    // Bulk hook (a real backend would batch AES/OT here); default scalar loop.
    void and_many(Wire* o, const Wire* a, const Wire* b, size_t n) {
        for (size_t i = 0; i < n; ++i) o[i] = a[i] & b[i];
    }
};

// ===========================================================================
// Analysis contexts (CountCtx / DigestCtx). These are intentionally
// CORE: they are the non-crypto "alternative interpretations" of the same kernel
// (cost counting, determinism hashing) that make the "one source, many semantics"
// model concrete — peers of ClearCtx (eval) and RecordCtx (emit IR). The
// execution/frontend path does NOT depend on them (frontend/circuit_fn.h includes
// neither); they are analysis/verification tools, not an execution dependency.
// ===========================================================================

// ---------------------------------------------------------------------------
// CountCtx — count gate calls of a templated kernel (no value tracking).
// (For a stored BooleanProgram use count_pass instead.)
// ---------------------------------------------------------------------------
struct CountCtx {
    using Wire = uint8_t;   // value unused; only the call counts matter
    uint64_t ands = 0, xors = 0, nots = 0, consts = 0;
    bool c0_seen = false, c1_seen = false;   // dedup consts to match RecordCtx
    Wire public_bit(bool v) { bool& s = v ? c1_seen : c0_seen; if (!s) { s = true; ++consts; } return 0; }
    Wire and_gate(Wire, Wire)     { ++ands;   return 0; }
    Wire xor_gate(Wire, Wire)     { ++xors;   return 0; }
    Wire not_gate(Wire)           { ++nots;   return 0; }
    uint64_t total() const { return ands + xors + nots + consts; }
};

// ---------------------------------------------------------------------------
// DigestCtx — fold the gate stream (op + operands + output id) into a
// running hash. Two runs of a deterministic kernel/replay produce the same
// digest; a reordered or nondeterministic stream does not. Wire = wire id.
// ---------------------------------------------------------------------------
struct DigestCtx {
    using Wire = uint32_t;
    uint64_t digest = 1469598103934665603ull;   // FNV-1a offset basis
    uint32_t next_id = 0;
    int64_t c0 = -1, c1 = -1;                    // dedup consts (mirror RecordCtx)
    void mix_(uint64_t x) { digest = (digest ^ x) * 1099511628211ull; }
    // Reserve input wires [0, n) so gate outputs start at n — the SAME logical
    // numbering as RecordCtx / replay, which the digest must match to detect
    // nondeterministic replay. Mixes num_inputs (input reservation is part of the
    // digest). Call before any gate.
    Wire external_input(size_t n) {
        if (n == 0) error("DigestCtx::external_input: zero-width argument");
        if ((uint64_t)next_id + n > UINT32_MAX) error("DigestCtx::external_input: wire id overflow");
        Wire base = next_id; next_id += (uint32_t)n; mix_(0xE); mix_((uint64_t)n); return base;
    }
    Wire emit_(uint64_t op, uint64_t a, uint64_t b) {
        if ((uint64_t)next_id + 1 > UINT32_MAX) error("DigestCtx: wire id overflow");
        uint32_t o = next_id++;
        mix_(op); mix_(a); mix_(b); mix_(o);
        return o;
    }
    // Dedup const0/const1 and mix the gate only on first emission (mirrors
    // RecordCtx) so a streamed digest matches the recorded program's digest.
    Wire public_bit(bool v) {
        int64_t& c = v ? c1 : c0;
        if (c < 0) c = (int64_t)emit_(3 + (v ? 1 : 0), 0, 0);
        return (Wire)c;
    }
    Wire and_gate(Wire a, Wire b) { return emit_(0, a, b); }
    Wire xor_gate(Wire a, Wire b) { return emit_(1, a, b); }
    Wire not_gate(Wire a)         { return emit_(2, a, 0); }
};

// ---------------------------------------------------------------------------
// RecordCtx — the recorder for templated BooleanContext kernels. Runs a kernel
// and emits a validated BooleanProgram (the IDENTICAL circuit replayed by every
// other context). Call external_input(n) BEFORE any gate so inputs are wires
// [0, num_inputs); then finish(output_wires). (The Backend-shaped RecordBackend
// in frontend/record_backend.h is a separate, internal recorder for the legacy
// global-Backend kernels that produce the .empbc builtins.)
// ---------------------------------------------------------------------------
struct RecordCtx {
    using Wire = uint32_t;
    circuit::BooleanProgram prog;
    uint32_t next_id = 0, num_inputs = 0;
    int64_t c0 = -1, c1 = -1;   // dedup the two constant wires
    bool inputs_closed = false; // flips once any gate is emitted (see alloc_)

    uint32_t alloc_() { inputs_closed = true; if (next_id == UINT32_MAX) error("RecordCtx: wire id overflow"); return next_id++; }
    Wire external_input(size_t n) {
        // Contract: all inputs are reserved before any gate, so inputs occupy
        // wires [0, num_inputs). A late external_input would interleave input ids
        // with gate ids and corrupt replay. Always enforced (cheap, record-path
        // only) so corruption can't slip through a release build.
        if (inputs_closed) error("RecordCtx::external_input: called after a gate was emitted");
        if (n == 0) error("RecordCtx::external_input: zero-width argument");
        if ((uint64_t)next_id + n > UINT32_MAX) error("RecordCtx::external_input: wire id overflow");
        if ((uint64_t)num_inputs + n > UINT32_MAX) error("RecordCtx::external_input: num_inputs overflow");
        Wire base = next_id; next_id += (uint32_t)n; num_inputs += (uint32_t)n; return base;
    }

    Wire public_bit(bool v) {
        int64_t& c = v ? c1 : c0;
        if (c < 0) { c = alloc_(); prog.gates.push_back({0, 0, (uint32_t)c,
                       v ? circuit::Op::Const1 : circuit::Op::Const0}); }
        return (Wire)c;
    }
    Wire and_gate(Wire a, Wire b) { Wire o = alloc_(); prog.gates.push_back({a, b, o, circuit::Op::And}); return o; }
    Wire xor_gate(Wire a, Wire b) { Wire o = alloc_(); prog.gates.push_back({a, b, o, circuit::Op::Xor}); return o; }
    Wire not_gate(Wire a)         { Wire o = alloc_(); prog.gates.push_back({a, 0, o, circuit::Op::Not}); return o; }

    // Set outputs, finalize dimensions, and validate the IR invariants.
    circuit::BooleanProgram& finish(std::span<const Wire> outputs) {
        prog.num_wires  = next_id;
        prog.num_inputs = num_inputs;
        prog.outputs.assign(outputs.begin(), outputs.end());
        circuit::validate_program(prog);
        return prog;
    }
};

static_assert(BooleanContext<ClearCtx>);
static_assert(BooleanContext<CountCtx>);
static_assert(BooleanContext<DigestCtx>);
static_assert(BooleanContext<RecordCtx>);

// ---------------------------------------------------------------------------
// Canonical replay digest. digest_program(p) hashes num_inputs + the gate stream
// exactly as a DigestCtx replay of the same source does, so a streamed
// digest (digest_source) can be compared against a recorded/stored program — the
// unambiguous target for replay-determinism checks.
// ---------------------------------------------------------------------------
inline uint64_t digest_program(const circuit::BooleanProgram& p) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(0xE); mix((uint64_t)p.num_inputs);                 // matches external_input(num_inputs)
    for (const circuit::Gate& g : p.gates) {
        uint64_t op = g.op == circuit::Op::And ? 0 : g.op == circuit::Op::Xor ? 1
                    : g.op == circuit::Op::Not ? 2 : (g.op == circuit::Op::Const1 ? 4 : 3);
        mix(op); mix(g.in0); mix(g.in1); mix(g.out);
    }
    return h;
}

// Digest a pure circuit body by replaying it through a DigestCtx. `body` is
// callable as body(DigestCtx&, const std::vector<uint32_t>& input_wires) and
// runs gates on the context. Equals digest_program() of the program that body
// produces under RecordCtx.
template <class Body>
inline uint64_t digest_source(uint32_t num_inputs, Body&& body) {
    DigestCtx d;
    uint32_t base = d.external_input((size_t)num_inputs);
    std::vector<uint32_t> in(num_inputs);
    for (uint32_t i = 0; i < num_inputs; ++i) in[i] = base + i;
    body(d, in);
    return d.digest;
}

// ---------------------------------------------------------------------------
// Reusable replay workspace — avoids per-call allocation once protocols depend
// on replay. `tmp_inputs` is for callers that assemble an input vector before
// replay (e.g. Float::binop_ concatenates 2*W operand wires).
// ---------------------------------------------------------------------------
template <class Wire>
struct ProgramWorkspace {
    circuit::CircuitScratch<Wire> scratch;   // wire slots
    std::vector<Wire> out;                     // output wires
    std::vector<Wire> tmp_inputs;              // caller-assembled inputs
    std::vector<Wire> ba, bb, bo;              // scheduled AND-batch buffers
    std::vector<uint32_t> bouts;
};

// Adapter: drive the in-place IR primitive from a value-return BooleanContext.
template <class Ctx>
struct CtxReplayAdapter {
    using W = typename Ctx::Wire;
    Ctx& c;
    void and_gate(W& o, const W& a, const W& b) { o = c.and_gate(a, b); }
    void xor_gate(W& o, const W& a, const W& b) { o = c.xor_gate(a, b); }
    void not_gate(W& o, const W& a)             { o = c.not_gate(a); }
    void const_gate(W& o, bool v)               { o = c.public_bit(v); }
};

// Value-return replay bridge (workspace form): writes outputs into ws.out and
// returns a reference to it (no allocation when ws is reused). This is how
// stored builtins run through any context.
template <BooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
    using W = typename Ctx::Wire;
    ws.out.resize(p.outputs.size());
    CtxReplayAdapter<Ctx> adapter{ctx};
    circuit::execute_program<W>(p, inputs.data(), inputs.size(),
                                ws.out.data(), ws.out.size(), ws.scratch, adapter);
    return ws.out;
}

// Convenience: allocate a one-shot workspace and return the outputs by value.
template <BooleanContext Ctx>
inline std::vector<typename Ctx::Wire> execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs) {
    ProgramWorkspace<typename Ctx::Wire> ws;
    return execute_program(ctx, p, inputs, ws);
}

// ---------------------------------------------------------------------------
// A BulkBooleanContext can evaluate a whole AND layer at once (batched AES/OT
// in a real backend). The default-loop and_many on a scalar context still works;
// the win comes when a backend overrides it.
// ---------------------------------------------------------------------------
template <class Ctx>
concept BulkBooleanContext =
    BooleanContext<Ctx> &&
    requires(Ctx& c, typename Ctx::Wire* o, const typename Ctx::Wire* a, size_t n) {
        c.and_many(o, a, a, n);
    };

// Scheduled bulk replay: execute linear (const/xor/not) gates in topological
// order, but batch every *ready* AND layer (grouped by AND-depth via
// schedule_pass) into one and_many call. Correctness identical to the scalar
// execute_program; the layering is what lets a bulk backend amortize crypto.
// Per AND-depth level L: run the level's ANDs first (operands are all at depth
// < L, hence ready), then the level's linear gates in emission order (they may
// read this level's ANDs or earlier same-level linears).
// A precomputed schedule: gate indices grouped per AND-depth level. Build once
// with make_scheduled_plan() and reuse across many executions of the same
// program (e.g. an AG2PC/AG-MPC builtin replayed every call) so the schedule
// pass and bucket allocation are not repeated per run.
struct ScheduledPlan {
    std::vector<std::vector<uint32_t>> bucket;   // gate indices per level (emission order)
    int depth = 0;
    // Identity of the program this plan was built for. A plan reused against a
    // DIFFERENT program would silently skip/misorder gates, so execution checks
    // these: the dimensions are an O(1) always-on guard; the digest is a stronger
    // debug-only guard (same shape, different gate stream).
    uint32_t num_inputs = 0;
    uint32_t num_wires  = 0;
    size_t   num_gates  = 0;
    uint64_t digest     = 0;
};

inline ScheduledPlan make_scheduled_plan(const circuit::BooleanProgram& p) {
    frontend::ScheduleStats sched = frontend::schedule_pass(p);
    ScheduledPlan plan;
    plan.depth = sched.levels.depth;
    plan.bucket.assign((size_t)plan.depth + 1, {});
    for (uint32_t gi = 0; gi < p.gates.size(); ++gi)
        plan.bucket[sched.wire_level[p.gates[gi].out]].push_back(gi);
    plan.num_inputs = p.num_inputs;
    plan.num_wires  = p.num_wires;
    plan.num_gates  = p.gates.size();
    plan.digest     = digest_program(p);
    return plan;
}

// Replay using a precomputed plan + reusable workspace (no schedule pass, no
// allocation per call). Writes outputs into ws.out and returns a reference.
template <BulkBooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p, const ScheduledPlan& plan,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
    using W = typename Ctx::Wire;
    if (inputs.size() != p.num_inputs)
        error("scheduled_execute_program: input count != program num_inputs");
    if (plan.num_inputs != p.num_inputs || plan.num_wires != p.num_wires ||
        plan.num_gates != p.gates.size())
        error("scheduled_execute_program: plan does not match program (stale plan?)");
#ifndef NDEBUG
    if (plan.digest != digest_program(p))
        error("scheduled_execute_program: plan digest mismatch (stale plan for a different program)");
#endif

    ws.scratch.ensure(p.num_wires);
    W* w = ws.scratch.wires.data();
    for (uint32_t i = 0; i < p.num_inputs; ++i) w[i] = inputs[i];

    for (int lv = 0; lv <= plan.depth; ++lv) {
        const std::vector<uint32_t>& bucket = plan.bucket[lv];
        ws.ba.clear(); ws.bb.clear(); ws.bouts.clear();
        for (uint32_t gi : bucket) {
            const circuit::Gate& g = p.gates[gi];
            if (g.op == circuit::Op::And) { ws.ba.push_back(w[g.in0]); ws.bb.push_back(w[g.in1]); ws.bouts.push_back(g.out); }
        }
        if (!ws.ba.empty()) {
            ws.bo.assign(ws.ba.size(), W{});
            ctx.and_many(ws.bo.data(), ws.ba.data(), ws.bb.data(), ws.ba.size());
            for (size_t k = 0; k < ws.bouts.size(); ++k) w[ws.bouts[k]] = ws.bo[k];
        }
        for (uint32_t gi : bucket) {
            const circuit::Gate& g = p.gates[gi];
            switch (g.op) {
                case circuit::Op::And:    break;                                  // done above
                case circuit::Op::Xor:    w[g.out] = ctx.xor_gate(w[g.in0], w[g.in1]); break;
                case circuit::Op::Not:    w[g.out] = ctx.not_gate(w[g.in0]); break;
                case circuit::Op::Const0: w[g.out] = ctx.public_bit(false); break;
                case circuit::Op::Const1: w[g.out] = ctx.public_bit(true); break;
            }
        }
    }
    ws.out.resize(p.outputs.size());
    for (size_t i = 0; i < p.outputs.size(); ++i) ws.out[i] = w[p.outputs[i]];
    return ws.out;
}

// Convenience: precomputed plan, allocate outputs.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p, const ScheduledPlan& plan,
    std::span<const typename Ctx::Wire> inputs) {
    ProgramWorkspace<typename Ctx::Wire> ws;
    return scheduled_execute_program(ctx, p, plan, inputs, ws);
}

// Convenience: build a one-shot plan and replay. For repeated execution of the
// same program, cache make_scheduled_plan(p) and use the plan overloads above.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs) {
    return scheduled_execute_program(ctx, p, make_scheduled_plan(p), inputs);
}

}  // namespace emp
#endif  // EMP_CIRCUIT_CONTEXT_H__
