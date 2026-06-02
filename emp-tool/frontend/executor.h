#ifndef EMP_FRONTEND_EXECUTOR_H__
#define EMP_FRONTEND_EXECUTOR_H__

// Record-and-replay over the global Backend. The frontend never talks to a
// protocol directly: it records ordinary circuit code into a BooleanProgram,
// then *replays* that program through whatever Backend is currently installed
// (e.g. the AG2PCBackend from setup_ag2pc, or a ClearBackend). Replaying issues
// plain backend->feed/and_gate/xor_gate/not_gate/public_label/reveal calls, so:
//   - the layer is protocol-neutral (works for any Backend), and
//   - replayed outputs are live wires of the *installed* backend's wire type,
//     so you reveal (or chain) them OUTSIDE the run() call with ordinary
//     Bit/Integer ops — the backend's own machinery does the rest.
//
// Three modes:
//   direct:   write ordinary code against the installed backend (no run()).
//   function: Runner<Wire>::run(body)        — record fresh, then replay.
//   circuit:  Runner<Wire>::run(compile(body)) — record once (with stats),
//                                                replay many.

#include "emp-tool/execution/backend.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/record_backend.h"
#include "emp-tool/frontend/passes.h"
#include "emp-tool/frontend/circuit.h"
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace emp {
namespace frontend {

// A self-contained body of ordinary circuit code, written against RecWire
// (use the Bit_rec / UInt32_rec / … aliases). Feeds its own inputs; designates
// outputs with keep() (live, revealed outside) and/or reveal() (decoded inside).
using Body = std::function<void()>;

// Designate a recorded value as a live output handed back by run(). Call inside
// a body while the RecordBackend is installed. Overloads: a single Bit, or any
// vector-of-bits type (BitVec / UnsignedInt / SignedInt).
inline void keep(const Bit_T<RecWire> &b) {
	int id = b.bit.id;
	static_cast<RecordBackend *>(backend)->add_wire_output(&id, 1);
}
template <typename V, typename = decltype(std::declval<const V &>().bits)>
inline void keep(const V &v) {
	std::vector<int> ids;
	ids.reserve(v.bits.size());
	for (const auto &bt : v.bits) ids.push_back(bt.bit.id);
	static_cast<RecordBackend *>(backend)->add_wire_output(ids.data(), ids.size());
}

// Record a body once into a BooleanProgram and annotate it with the stat
// passes. Saves/restores the global backend, so it composes with whatever is
// installed. Wire-independent: the program is replayed against the live wire
// type later.
inline Circuit compile(const Body &body) {
	Backend *saved = backend;
	RecordBackend rec;
	backend = &rec;
	body();
	rec.finalize();
	backend = saved;

	Circuit c;
	c.prog     = std::move(rec.prog);
	c.count    = count_pass(c.prog);
	c.liveness = liveness_pass(c.prog);
	c.schedule = schedule_pass(c.prog);
	c.layout   = layout_pass(c.prog, c.liveness);
	return c;
}

// Replays compiled circuits through the installed backend, materializing wires
// of `Wire` (the installed backend's wire type). Stateless — all protocol state
// lives behind the global `backend`.
template <typename Wire>
class Runner {
public:
	// Replay a circuit. Returns the live kept-output wires (Wire-kind ports),
	// flattened in keep() order, as Bit_T<Wire> bound to the installed backend
	// — reveal or chain them outside this call. Revealed-kind ports (reveal()
	// inside the body) are executed during replay against the backend.
	std::vector<Bit_T<Wire>> run(const Circuit &c, const BoundInputs &in = {}) const {
		const BooleanProgram &p = c.prog;
		assert(backend != nullptr && "no backend installed (call setup_*_backend first)");
		assert(backend->wire_bytes() == sizeof(Wire) &&
		       "Runner<Wire>: installed backend's wire_bytes() != sizeof(Wire)");

		// Scratch wire array in the backend's wire type. Real Wire objects (not
		// a byte buffer) so wire carriers with copy/assign semantics — e.g.
		// AG2PCWire's refcount — travel correctly through backend ops.
		std::vector<Wire> buf(p.num_wire);

		// Inputs: feed this party's bits per port (BoundInputs overrides the
		// recorded fed_bits when present).
		for (size_t pi = 0; pi < p.inputs.size(); ++pi) {
			const InputPort &ip = p.inputs[pi];
			const std::vector<bool> &src =
			    (pi < in.bits.size() && !in.bits[pi].empty()) ? in.bits[pi]
			                                                  : ip.fed_bits;
			std::unique_ptr<bool[]> bits(new bool[ip.n]);
			for (int k = 0; k < ip.n; ++k)
				bits[k] = (k < (int)src.size()) ? src[k] : false;
			backend->feed(&buf[ip.base], ip.owner, bits.get(), ip.n);
		}

		// Gates in topological (recorded) order.
		for (const Gate &g : p.gates) {
			switch (g.op) {
				case Op::CONST0: backend->public_label(&buf[g.out], false); break;
				case Op::CONST1: backend->public_label(&buf[g.out], true);  break;
				case Op::AND: backend->and_gate(&buf[g.out], &buf[g.in0], &buf[g.in1]); break;
				case Op::XOR: backend->xor_gate(&buf[g.out], &buf[g.in0], &buf[g.in1]); break;
				case Op::NOT: backend->not_gate(&buf[g.out], &buf[g.in0]); break;
			}
		}

		// Outputs.
		std::vector<Bit_T<Wire>> outs;
		for (const OutputPort &op : p.outputs) {
			if (op.kind == OutputPort::Kind::Wire) {
				for (int w : op.wire_ids) outs.push_back(Bit_T<Wire>(buf[w]));
			} else {  // Revealed: execute the reveal now, against the backend.
				const size_t n = op.wire_ids.size();
				std::unique_ptr<Wire[]> wbuf(new Wire[n]);
				for (size_t i = 0; i < n; ++i) wbuf[i] = buf[op.wire_ids[i]];
				std::unique_ptr<bool[]> rev(new bool[n]);
				backend->reveal(rev.get(), op.to_party, wbuf.get(), n);
			}
		}
		return outs;
	}

	std::vector<Bit_T<Wire>> run(const Body &body, const BoundInputs &in = {}) const {
		return run(compile(body), in);
	}
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_EXECUTOR_H__
