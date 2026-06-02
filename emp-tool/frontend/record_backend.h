#ifndef EMP_FRONTEND_RECORD_BACKEND_H__
#define EMP_FRONTEND_RECORD_BACKEND_H__

// A protocol-neutral Backend that records a PURE circuit function into a
// BooleanProgram instead of evaluating it. A circuit takes its inputs as
// arguments and returns its output — there is no I/O inside it: secret feed()
// and reveal() are rejected (do I/O in direct mode, around the circuit).
// Public-constant feeds are allowed (they fold to CONST gates). The gate shape
// is value-free with respect to the arguments, so all parties record an
// identical program — provided public feeds are literal / agreed constants
// (they bake in as CONST gates and must not be per-party runtime values).

#include "emp-tool/execution/backend.h"
#include "emp-tool/core/utils.h"            // error()
#include "emp-tool/frontend/boolean_program.h"
#include <cstddef>

namespace emp {
namespace frontend {

// 4-byte wire carrier holding a recorder wire id. Trivial except for an empty
// user dtor, which keeps Bit_T<RecWire> non-trivially-copyable so
// -Werror=class-memaccess still catches accidental memcpy of wire carriers
// (same trick as ag2pc's LambdaWire).
struct RecWire {
	int id = -1;
	RecWire() = default;
	explicit RecWire(int i) noexcept : id(i) {}
	~RecWire() {}
};

class RecordBackend : public Backend {
public:
	BooleanProgram prog;

	RecordBackend() : Backend(PUBLIC) {}

	size_t wire_bytes() const override { return sizeof(RecWire); }

	// Public constants fold to a single CONST0/CONST1 gate each (deduped), so
	// the IR carries at most two constant wires regardless of how many times a
	// public literal appears.
	void public_label(void* out, bool b) override {
		int& cache = b ? const1_id_ : const0_id_;
		if (cache < 0) {
			cache = alloc_();
			prog.gates.push_back(Gate{-1, -1, cache, b ? Op::CONST1 : Op::CONST0});
		}
		static_cast<RecWire*>(out)->id = cache;
	}

	// A circuit's inputs are its arguments, not values fed inside it. Only
	// public constants may be fed (they fold to CONST gates above). A secret /
	// party-owned feed inside the body would bake those bits into the circuit,
	// so it is rejected — pass such inputs as arguments instead.
	void feed(void* out, int from_party, const bool* in, size_t n) override {
		if (from_party != PUBLIC)
			error("frontend: a circuit takes secret inputs as ARGUMENTS, not via "
			      "feed() inside the body (do that in direct mode, around the circuit)");
		for (size_t i = 0; i < n; ++i)
			public_label(static_cast<RecWire*>(out) + i, in[i]);
	}

	void and_gate(void* out, const void* l, const void* r) override {
		int o = alloc_();
		prog.gates.push_back(Gate{id_(l), id_(r), o, Op::AND});
		static_cast<RecWire*>(out)->id = o;
		++prog.num_and;
	}
	void xor_gate(void* out, const void* l, const void* r) override {
		int o = alloc_();
		prog.gates.push_back(Gate{id_(l), id_(r), o, Op::XOR});
		static_cast<RecWire*>(out)->id = o;
	}
	void not_gate(void* out, const void* in) override {
		int o = alloc_();
		prog.gates.push_back(Gate{id_(in), -1, o, Op::NOT});
		static_cast<RecWire*>(out)->id = o;
	}

	// A circuit's output is its return value, not something revealed inside it.
	// Revealing inside would (a) bake a record-time placeholder into any host
	// branch on the result and (b) decode at a fixed party — so it is rejected.
	// Reveal the returned value in direct mode, outside the circuit.
	void reveal(bool* /*out*/, int /*to_party*/, const void* /*in*/, size_t /*n*/) override {
		error("frontend: a circuit RETURNS its output; reveal it in direct mode, "
		      "outside the circuit (reveal() inside the body is not allowed)");
	}

	// Allocate `n` wires as an input-argument port (bound to a live value at
	// run time). Returns the base wire id. Used by the compile() path.
	int external_input(int n) {
		int base = next_id_;
		for (int i = 0; i < n; ++i) alloc_();
		prog.inputs.push_back(InputPort{base, n});
		return base;
	}

	uint64_t num_and() override { return prog.num_and; }

	void finalize() override {
		prog.num_wire   = next_id_;
		prog.wire_bytes = sizeof(RecWire);
	}

private:
	int next_id_   = 0;
	int const0_id_ = -1, const1_id_ = -1;   // -1 = not yet materialized
	int alloc_() { return next_id_++; }
	static int id_(const void* p) { return static_cast<const RecWire*>(p)->id; }
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_RECORD_BACKEND_H__
