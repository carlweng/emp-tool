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
// OUTSIDE the call. (Direct, no-frontend code — write `Integer x(...); x.reveal()`
// — remains the way to do I/O; the frontend only transforms inputs to outputs.)

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

// LIVE run: invoke a generic, wire-typed body against the installed backend and
// return its typed result. Inputs/outputs are ordinary EMP objects, so circuits
// chain (feed one run's result into the next) and you reveal outside the call.
//
//   auto z = frontend::run([](auto a, auto b){ return a + b; }, x, y);
//   auto w = frontend::run([](auto a){ return a + a; }, z);   // chains on z
//
// Arguments are passed by VALUE: a circuit reads inputs and returns an output,
// it does not mutate inputs. This matches compiled replay and rejects bodies
// taking a non-const lvalue reference in both paths. The body must be generic
// over the wire (a generic / template lambda, or a templated functor) so the
// same code also records under RecWire in compile().
template <typename F, typename... Args,
          typename = std::enable_if_t<std::is_invocable_v<F &, std::decay_t<Args>...>>>
auto run(F &&body, Args &&...args)
    -> std::decay_t<decltype(std::declval<F &>()(std::declval<std::decay_t<Args>>()...))> {
	// Return type is decayed to a value: a circuit returns an EMP value, never a
	// reference (a by-reference return would dangle, pointing at the argument
	// copies that live only inside this call).
	return body(static_cast<std::decay_t<Args>>(args)...);
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
	static_assert(std::is_base_of<emp::CircuitValue, Ret>::value,
	              "frontend::compile: a circuit must RETURN an EMP circuit value "
	              "(Bit / Integer / Float / …), not a plain value");
	Backend *saved = backend;
	RecordBackend rec;
	backend = &rec;

	auto rec_inputs = make_inputs();
	// Pass the recorded inputs to the body as rvalues (value semantics): a
	// circuit's arguments are values, not mutable references. A body that takes
	// a non-const lvalue reference (and would mutate its input) fails to bind
	// here and is rejected at compile time — matching the SFINAE in compile().
	Ret rec_result =
	    std::apply([&](auto &&...ins) { return body(std::move(ins)...); }, rec_inputs);

	// The circuit's output IS the returned value's wires. Guard the capture with
	// `if constexpr` so a non-CircuitValue return surfaces ONLY the static_assert
	// above, not a second (cryptic) pack_wires error.
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
//                                     for runtime-width Integers).
// The body must not feed secret inputs or reveal inside (RecordBackend rejects
// both); pass secrets as arguments and reveal the returned value outside.

// The body is invoked with rvalue (value-semantics) arguments — see
// record_typed_ — so a body taking a non-const lvalue reference is rejected.

// Input types as template arguments.
template <typename... Ins, typename F,
          typename Ret = std::decay_t<decltype(std::declval<F &>()(
              std::declval<rebind_t<Ins, RecWire>>()...))>,
          typename = std::enable_if_t<(sizeof...(Ins) > 0) && !std::is_void<Ret>::value>>
TypedCircuit<Ret> compile(F &&body) {
	return record_typed_<Ret>(std::forward<F>(body), [] {
		return std::tuple<rebind_t<Ins, RecWire>...>{
		    make_external<rebind_t<Ins, RecWire>>(rebind_t<Ins, RecWire>{}.pack_size())...};
	});
}

// Sample values fix the shapes.
template <typename F, typename... Samples,
          typename Ret = std::decay_t<decltype(std::declval<F &>()(
              std::declval<rebind_t<Samples, RecWire>>()...))>,
          typename = std::enable_if_t<!std::is_void<Ret>::value>>
TypedCircuit<Ret> compile(F &&body, const Samples &...samples) {
	static_assert(sizeof...(Samples) > 0,
	              "frontend::compile: a circuit needs at least one input argument "
	              "(construct constants directly rather than compiling a zero-input circuit)");
	return record_typed_<Ret>(std::forward<F>(body), [&] {
		return std::tuple<rebind_t<Samples, RecWire>...>{
		    make_external<rebind_t<Samples, RecWire>>(samples.pack_size())...};
	});
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
