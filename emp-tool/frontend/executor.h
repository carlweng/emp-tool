#ifndef EMP_FRONTEND_EXECUTOR_H__
#define EMP_FRONTEND_EXECUTOR_H__

// Run a PURE circuit function through the installed Backend. A circuit takes
// EMP inputs as arguments and returns an EMP output; it does no I/O of its own
// (no secret feed, no reveal — those are the caller's job, in direct mode,
// around the circuit). Two ways to run it:
//
//   live:     frontend::run(body, args...)        — call the body directly.
//   compiled: auto c = frontend::compile<...>(body);   // record once (+ stats)
//             frontend::run(c, args...)           — replay c with fresh inputs.
//
// Replaying a compiled circuit issues plain backend->public_label/and/xor/not
// calls through whatever Backend is installed, so the layer is protocol-neutral
// (ClearBackend, AG2PC, …) and the result is a live EMP object you reveal/chain
// OUTSIDE the call. (Direct, no-frontend code — write
// `UInt32 x(...); x.reveal()` — remains the way to do I/O; the frontend only
// transforms inputs to outputs.)

#include "emp-tool/execution/backend.h"
#include "emp-tool/circuits/bit.h"          // pulls circuit_value.h (wire_t / rebind_t / …)
#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/record_backend.h"
#include "emp-tool/frontend/passes.h"
#include "emp-tool/frontend/circuit.h"
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp {
namespace frontend {

// wire_t / rebind_t / pack_wires / assemble come from circuits/circuit_value.h
// (the generic accessors over any circuit value's pack/unpack interface).

// ---- The pure-circuit calling contract (one place, C++17, no concepts) ------
//
// A frontend body must be CALLABLE WITH PRVALUE CIRCUIT-VALUE ARGUMENTS AND
// RETURN A CIRCUIT VALUE BY VALUE. circuit_fn_traits<F, Args...> evaluates that
// contract for a body F invoked with the (caller-declared) argument types
// Args...; `value_return` is the decayed result type, and the individual flags
// drive precise diagnostics at every entry point (run / compile / compile-with-
// samples). Reused by emp-ag2pc (via frontend::run) so the contract is defined
// exactly once.

template <typename T>
inline constexpr bool is_circuit_value_v =
    std::is_base_of<emp::CircuitValue, std::decay_t<T>>::value;

template <typename F, typename... Args>
class circuit_fn_traits {
	template <typename A> using val = std::decay_t<A>;   // the prvalue arg type

	// Raw return type, but only formed when the body is actually callable with
	// the prvalue args (so a non-callable body doesn't hard-error here).
	template <typename G, bool = std::is_invocable_v<G &, val<Args>...>>
	struct ret_ { using type = void; };
	template <typename G>
	struct ret_<G, true> {
		using type = decltype(std::declval<G &>()(std::declval<val<Args>>()...));
	};

public:
	using raw_return   = typename ret_<F>::type;
	using value_return = std::decay_t<raw_return>;

	// Every argument is a circuit value (empty pack ⇒ true).
	static constexpr bool args_are_circuit = (true && ... && is_circuit_value_v<Args>);
	// Callable as F& with prvalue args — a non-const lvalue-ref parameter (which
	// a prvalue cannot bind to) makes this false, rejecting mutating bodies.
	static constexpr bool callable        = std::is_invocable_v<F &, val<Args>...>;
	static constexpr bool returns_ref     = std::is_reference<raw_return>::value;
	static constexpr bool returns_void    = std::is_void<raw_return>::value;
	static constexpr bool returns_circuit = !returns_void && is_circuit_value_v<raw_return>;

	static constexpr bool ok = args_are_circuit && callable && !returns_ref &&
	                           !returns_void && returns_circuit;
};

// circuit_contract<Tr> is the contract's single diagnostic site. The entry
// points instantiate it IN THEIR BODY (`(void)sizeof(circuit_contract<Tr>)`) so a
// violating body emits exactly the precise message(s) below, once. It is
// deliberately NOT named in the signature: a static_assert failure during
// return-type substitution would also make the overload non-viable, adding a
// spurious "no matching function". The entry points pair this with
// `if constexpr (Tr::ok)` so nothing downstream (record_typed_ / std::apply /
// pack_wires) instantiates after the contract has spoken. The trait emits the
// diagnostic; the `if constexpr` prevents the noise. (`::type` is provided on
// both specializations only so the type stays usable; the entry points read
// `Tr::value_return` directly for their return type.)
template <typename Tr, bool Ok = Tr::ok>
struct circuit_contract {                       // Ok: contract holds
	using type = typename Tr::value_return;
};
template <typename Tr>
struct circuit_contract<Tr, false> {            // the only place asserts live
	static_assert(Tr::args_are_circuit,
	    "frontend circuit: every argument must be a circuit value "
	    "(Bit / UnsignedInt / SignedInt / Float / ...)");
	static_assert(Tr::callable,
	    "frontend circuit: body is not callable with prvalue circuit-value "
	    "arguments (a non-const lvalue-reference parameter is rejected — take "
	    "arguments by value)");
	static_assert(!Tr::returns_ref,
	    "frontend circuit must RETURN BY VALUE: the output is a fresh circuit "
	    "value, not a reference (a returned reference dangles)");
	static_assert(!Tr::returns_void,
	    "frontend circuit must return a circuit value, not void");
	static_assert(Tr::returns_circuit,
	    "frontend circuit must return a circuit value (Bit / UnsignedInt / "
	    "SignedInt / Float / ...), not a plain value");
	using type = typename Tr::value_return;     // safe: value_return is void when
	                                            // the body isn't callable, never ill-formed
};

// A compiled circuit (defined just below). Forward-declared so live run() can
// route run(circuit, …) to the replay overload by TYPE (is_typed_circuit) rather
// than by callability — that way a contract-violating body still selects live
// run() and reaches its contract diagnostic instead of a bare "no matching
// function".
template <typename RetRec> struct TypedCircuit;
template <typename T> struct is_typed_circuit : std::false_type {};
template <typename R> struct is_typed_circuit<TypedCircuit<R>> : std::true_type {};
template <typename T>
inline constexpr bool is_typed_circuit_v = is_typed_circuit<std::decay_t<T>>::value;

// Complete stand-in for live run()'s return type when the contract fails (the
// real return would be the body's value_return, which is void for a non-callable
// body — `auto z = run(bad_body, …)` would then add a spurious "incomplete type
// void"). Returning this tag keeps the contract static_assert the only error.
struct invalid_circuit_fn {};

// LIVE run: invoke a generic, wire-typed body against the installed backend and
// return its typed result. Inputs/outputs are ordinary EMP objects, so circuits
// chain (feed one run's result into the next) and you reveal outside the call.
//
//   auto z = frontend::run([](auto a, auto b){ return a + b; }, x, y);
//   auto w = frontend::run([](auto a){ return a + a; }, z);   // chains on z
//
// Contract (circuit_fn_traits): arguments are circuit values passed BY VALUE
// (prvalue copies — the body cannot mutate them); the body returns a circuit
// value BY VALUE. A body taking a non-const lvalue reference, returning a
// reference, returning void, or returning a non-circuit value is rejected with
// one precise message (via circuit_contract, the return type). The overload is
// selected whenever the first argument is NOT a compiled circuit, so those bad
// bodies reach the diagnostic rather than SFINAE'ing to "no matching function".
template <typename F, typename... Args,
          typename Tr = circuit_fn_traits<F, Args...>,
          typename = std::enable_if_t<!is_typed_circuit_v<F>>>
std::conditional_t<Tr::ok, typename Tr::value_return, invalid_circuit_fn>
run(F &&body, Args &&...args) {
	// Instantiate the contract trait HERE (in the body, not the signature) so a
	// violation emits exactly one diagnostic — naming it in the return type would
	// also make the overload non-viable, adding a spurious "no matching function".
	(void)sizeof(circuit_contract<Tr>);   // instantiate the contract: emits its diagnostic
	if constexpr (Tr::ok)
		return body(static_cast<std::decay_t<Args>>(args)...);
	else
		return {};   // invalid_circuit_fn{} — unreachable; contract already asserted
}

// Build a Circuit (program + stats) from a finished recording.
inline Circuit make_circuit(BooleanProgram &&prog) {
	Circuit c;
	c.prog     = std::move(prog);
	c.count    = count_pass(c.prog);
	c.liveness = liveness_pass(c.prog);
	c.schedule = schedule_pass(c.prog);
	c.layout   = layout_pass(c.prog, c.liveness);
	return c;
}

// A compiled circuit that remembers its return type (RetRec, in RecWire form)
// so a replay can hand back a reconstructed EMP object rather than flat bits.
template <typename RetRec>
struct TypedCircuit {
	Circuit circuit;
};

// Allocate an input-argument port of `n` wires and build the RecWire-typed
// stand-in the body operates on while recording. Call while recording.
template <typename RecT>
inline RecT make_external(int n) {
	if (n <= 0)
		error("frontend::compile: a circuit input has zero width — use a "
		      "fixed-width type (UInt32, …) with compile<...>, or compile(body, sample) "
		      "with a sized sample for runtime-width types (UnsignedInt/BitVec)");
	RecordBackend *rb = static_cast<RecordBackend *>(backend);
	int base = rb->external_input(n);
	std::vector<RecWire> w(n);
	for (int i = 0; i < n; ++i) w[i] = RecWire(base + i);
	return assemble<RecT>(w.data(), n);
}

// Shared record-and-capture: install a RecordBackend, build the argument ports
// (via `make_inputs`, called while recording), run the body, capture the
// returned value's wires as the output, and package the Circuit.
template <typename Ret, typename F, typename MakeInputs>
inline TypedCircuit<Ret> record_typed_(F &&body, MakeInputs make_inputs) {
	// Contract (circuit_fn_traits: Ret is a circuit value, args circuit-valued,
	// returned by value) is enforced at the compile() entry points, so this
	// internal helper trusts Ret. The `if constexpr` below keeps a stray
	// non-circuit Ret from emitting a second cryptic pack_wires diagnostic.
	Backend *saved = backend;
	RecordBackend rec;
	backend = &rec;

	auto rec_inputs = make_inputs();
	// Pass the recorded inputs to the body as rvalues (value semantics): a
	// circuit's arguments are values, not mutable references.
	Ret rec_result =
	    std::apply([&](auto &&...ins) { return body(std::move(ins)...); }, rec_inputs);

	// The circuit's output IS the returned value's wires.
	if constexpr (std::is_base_of<emp::CircuitValue, Ret>::value) {
		auto ow = pack_wires(rec_result);   // std::vector<RecWire>
		rec.prog.outputs.clear();
		rec.prog.outputs.reserve(ow.size());
		for (const auto &x : ow) rec.prog.outputs.push_back(x.id);
	}

	rec.finalize();
	backend = saved;
	return TypedCircuit<Ret>{make_circuit(std::move(rec.prog))};
}

// Compile a pure circuit function (EMP args in, EMP value out). Two ways to fix
// the input shapes:
//   compile<UInt32, UInt32>(body)   — input types as template args (no values);
//                                     width comes from the type (fixed-width
//                                     types / Bit / Float).
//   compile(body, a, b)             — sample values; only their shape is read
//                                     (handy when you already hold inputs, or
//                                     for runtime-width integer values).
// The body must not feed secret inputs or reveal inside (RecordBackend rejects
// both); pass secrets as arguments and reveal the returned value outside.

// Both compile paths record the body once against RecWire-shaped arguments and
// enforce the SAME contract as live run(), against those recorded argument shapes.

// rebind_t<T, W> for a circuit value; a harmless circuit-value placeholder
// (Bit_T<W>) otherwise. Used so circuit_fn_traits stays well-formed even when an
// input type/value is NOT a circuit value — letting the precise "input must be a
// circuit value" assertion fire instead of a `rebind_t<int, …>` substitution
// failure ("int has no member rebind").
template <typename T, typename W, bool = is_circuit_value_v<T>>
struct rebind_safe { using type = rebind_t<T, W>; };
template <typename T, typename W>
struct rebind_safe<T, W, false> { using type = Bit_T<W>; };
template <typename T, typename W> using rebind_safe_t = typename rebind_safe<T, W>::type;

// Input types as template arguments. The body is checked against
// rebind_t<Ins, RecWire> argument shapes.
template <typename... Ins, typename F,
          typename Tr = circuit_fn_traits<F, rebind_safe_t<Ins, RecWire>...>,
          typename = std::enable_if_t<(sizeof...(Ins) > 0)>>
TypedCircuit<typename Tr::value_return> compile(F &&body) {
	// Validate the input TYPES up front (before anything depends on a real
	// rebind_t<Ins, RecWire>), so a non-circuit Ins gets this message, not a
	// substitution failure. Then instantiate the contract trait in the body to
	// emit the body's diagnostics as a single error.
	static_assert((true && ... && is_circuit_value_v<Ins>),
	              "frontend::compile: each input TYPE must be a circuit value "
	              "(UInt32 / Bit / Float / ...)");
	(void)sizeof(circuit_contract<Tr>);   // instantiate the contract: emits its diagnostic
	if constexpr (Tr::ok && (true && ... && is_circuit_value_v<Ins>)) {
		return record_typed_<typename Tr::value_return>(std::forward<F>(body), [] {
			return std::tuple<rebind_t<Ins, RecWire>...>{
			    make_external<rebind_t<Ins, RecWire>>(rebind_t<Ins, RecWire>{}.pack_size())...};
		});
	} else {
		return {};   // unreachable: a static_assert above already failed
	}
}

// Sample values fix the shapes. The body is checked against
// rebind_t<Samples, RecWire> argument shapes.
template <typename F, typename... Samples,
          typename Tr = circuit_fn_traits<F, rebind_safe_t<Samples, RecWire>...>>
TypedCircuit<typename Tr::value_return> compile(F &&body, const Samples &...samples) {
	static_assert(sizeof...(Samples) > 0,
	              "frontend::compile: a circuit needs at least one input argument "
	              "(construct constants directly rather than compiling a zero-input circuit)");
	static_assert((true && ... && is_circuit_value_v<Samples>),
	              "frontend::compile: each input SAMPLE must be a circuit value "
	              "(UInt32 / Bit / Float / ...)");
	(void)sizeof(circuit_contract<Tr>);   // instantiate the contract: emits its diagnostic
	if constexpr (Tr::ok && (true && ... && is_circuit_value_v<Samples>)) {
		return record_typed_<typename Tr::value_return>(std::forward<F>(body), [&] {
			return std::tuple<rebind_t<Samples, RecWire>...>{
			    make_external<rebind_t<Samples, RecWire>>(samples.pack_size())...};
		});
	} else {
		return {};   // unreachable: a static_assert above already failed
	}
}

// Replay a compiled circuit with live EMP inputs, returning the typed result (an
// EMP object over the live wire type) to reveal or chain outside the call. The
// live wire type is inferred from the first argument; each argument's wires are
// bound to a circuit input port, in order; the return value is reconstructed
// from the replayed output wires.
template <typename RetRec, typename Arg0, typename... Args>
auto run(const TypedCircuit<RetRec> &tc, const Arg0 &a0, const Args &...rest)
    -> rebind_t<RetRec, wire_t<Arg0>> {
	using Wire    = wire_t<Arg0>;
	using RetLive = rebind_t<RetRec, Wire>;
	const BooleanProgram &p = tc.circuit.prog;

	// Real checks (fire under NDEBUG too): a wrong backend or mismatched
	// argument count/width would otherwise corrupt wire slots.
	if (backend == nullptr || backend->wire_bytes() != sizeof(Wire))
		error("frontend::run(circuit): wrong/absent backend for this wire type");
	if (p.inputs.size() != 1 + sizeof...(Args))
		error("frontend::run(circuit): argument count != circuit input count");

	std::vector<Wire> buf(p.num_wire);

	// Bind each argument's live wires into its input port, in order.
	int ai = 0;
	auto bind = [&](const auto &arg) {
		const InputPort &port = p.inputs[ai++];   // ai in range by the count check
		std::vector<Wire> w = pack_wires(arg);
		if ((int)w.size() != port.n)
			error("frontend::run(circuit): argument width != circuit input width");
		for (int k = 0; k < port.n; ++k) buf[port.base + k] = w[k];
	};
	bind(a0);
	(void)std::initializer_list<int>{(bind(rest), 0)...};

	// Replay the gates through the installed backend.
	for (const Gate &g : p.gates) {
		switch (g.op) {
			case Op::CONST0: backend->public_label(&buf[g.out], false); break;
			case Op::CONST1: backend->public_label(&buf[g.out], true);  break;
			case Op::AND: backend->and_gate(&buf[g.out], &buf[g.in0], &buf[g.in1]); break;
			case Op::XOR: backend->xor_gate(&buf[g.out], &buf[g.in0], &buf[g.in1]); break;
			case Op::NOT: backend->not_gate(&buf[g.out], &buf[g.in0]); break;
		}
	}

	// Reconstruct the typed return value from the output wires.
	std::vector<Wire> ow;
	ow.reserve(p.outputs.size());
	for (int w : p.outputs) ow.push_back(buf[w]);
	return assemble<RetLive>(ow.data(), (int)ow.size());
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_EXECUTOR_H__
