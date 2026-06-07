#ifndef EMP_FRONTEND_RECORD_BACKEND_H__
#define EMP_FRONTEND_RECORD_BACKEND_H__

// Internal recorder for the legacy global-Backend circuit kernels (emp::legacy::
// Bit_T<Wire> + emp::Backend): it records a PURE circuit function into a
// BooleanProgram instead of evaluating it, the source that produces the .empbc
// builtins (AES / SHA). (Templated BooleanContext kernels record through RecordCtx
// in circuits/context.h instead; this Backend-shaped recorder exists only for the
// legacy kernels.) A circuit takes its inputs as arguments and returns its output
// — there is no I/O inside it: secret feed() and reveal() are rejected (do I/O in
// direct mode, around the circuit). Public-constant feeds are allowed (they fold
// to CONST gates). The gate shape is value-free with respect to the arguments, so
// all parties record an identical program — provided public feeds are literal /
// agreed constants (they bake in as CONST gates and must not be per-party runtime
// values).

#include "emp-tool/execution/backend.h"
#include "emp-tool/core/utils.h"            // error()
#include "emp-tool/frontend/boolean_program.h"
#include <cstddef>
#include <cstdint>

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
	// public literal appears. Unused operands are normalized to 0 (the IR's
	// convention; never read for const gates).
	void public_label(void* out, bool b) override {
		int& cache = b ? const1_id_ : const0_id_;
		if (cache < 0) {
			cache = alloc_();
			prog.gates.push_back(Gate{0, 0, (uint32_t)cache, b ? Op::Const1 : Op::Const0});
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
		prog.gates.push_back(Gate{(uint32_t)id_(l), (uint32_t)id_(r), (uint32_t)o, Op::And});
		static_cast<RecWire*>(out)->id = o;
		++and_count_;
	}
	void xor_gate(void* out, const void* l, const void* r) override {
		int o = alloc_();
		prog.gates.push_back(Gate{(uint32_t)id_(l), (uint32_t)id_(r), (uint32_t)o, Op::Xor});
		static_cast<RecWire*>(out)->id = o;
	}
	void not_gate(void* out, const void* in) override {
		int o = alloc_();
		prog.gates.push_back(Gate{(uint32_t)id_(in), 0, (uint32_t)o, Op::Not});
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

	// Reserve `n` input wires (the leading wires of the program); returns the base
	// wire id. The core IR has no input-port concept — inputs are simply wires
	// [0, num_inputs) — so we only accumulate the count; per-argument widths live
	// on the compiled Circuit's signature (CircuitSignature::arg_widths). Mirrors
	// RecordCtx::external_input: positive count, no overflow, and inputs only
	// before any gate (incl. a CONST gate); all always-on.
	int external_input(int n) {
		if (n <= 0)
			error("RecordBackend::external_input: width must be positive");
		if ((int64_t)next_id_ + n > INT32_MAX)
			error("RecordBackend::external_input: wire id overflow");
		if (!prog.gates.empty())
			error("RecordBackend::external_input: inputs must be reserved before any gate");
		int base = next_id_;
		for (int i = 0; i < n; ++i) alloc_();
		num_inputs_ += n;
		return base;
	}

	uint64_t num_and() override { return and_count_; }

	void finalize() override {
		prog.num_wires  = (uint32_t)next_id_;
		prog.num_inputs = (uint32_t)num_inputs_;
	}

private:
	int next_id_    = 0;
	int num_inputs_ = 0;
	uint64_t and_count_ = 0;
	int const0_id_ = -1, const1_id_ = -1;   // -1 = not yet materialized
	int alloc_() { return next_id_++; }
	static int id_(const void* p) { return static_cast<const RecWire*>(p)->id; }
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_RECORD_BACKEND_H__
