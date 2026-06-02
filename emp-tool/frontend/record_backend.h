#ifndef EMP_FRONTEND_RECORD_BACKEND_H__
#define EMP_FRONTEND_RECORD_BACKEND_H__

// A protocol-neutral Backend that records ordinary emp-tool circuit code into a
// BooleanProgram instead of evaluating it. Install it as the global `backend`,
// run a body of Bit/Integer code once, and read out `prog`. Recording is
// value-free for shape: the body's feed values only seed input-port fed_bits;
// the gate graph is independent of them, so every party records an identical
// program. reveal() is intercepted as an output port and writes a placeholder
// (the real cleartext comes from the protocol backend at run time).

#include "emp-tool/execution/backend.h"
#include "emp-tool/frontend/boolean_program.h"
#include <cstddef>
#include <utility>

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

	void feed(void* out, int from_party, const bool* in, size_t n) override {
		if (from_party == PUBLIC) {                 // public feed == constants
			for (size_t i = 0; i < n; ++i)
				public_label(static_cast<RecWire*>(out) + i, in[i]);
			return;
		}
		InputPort p;
		p.kind  = InputPort::Kind::Fed;
		p.owner = from_party;
		p.base  = next_id_;
		p.n     = (int)n;
		p.fed_bits.assign(in, in + n);   // owner's real bits; dummies elsewhere
		for (size_t i = 0; i < n; ++i)
			(static_cast<RecWire*>(out) + i)->id = alloc_();
		prog.inputs.push_back(std::move(p));
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

	// reveal at record time: register an output port; write a deterministic
	// placeholder so the body keeps running. The body's return value is
	// therefore meaningless under recording — callers read the executor's
	// decoded Outputs instead.
	void reveal(bool* out, int to_party, const void* in, size_t n) override {
		OutputPort p;
		p.kind     = OutputPort::Kind::Revealed;
		p.to_party = to_party;
		p.wire_ids.resize(n);
		for (size_t i = 0; i < n; ++i) {
			p.wire_ids[i] = id_(static_cast<const RecWire*>(in) + i);
			out[i] = false;
		}
		prog.outputs.push_back(std::move(p));
	}

	// Mark wires as live outputs to hand back (revealed/chained outside the run
	// call) rather than decoded inside. Used by frontend::keep().
	void add_wire_output(const int *ids, size_t n) {
		OutputPort p;
		p.kind = OutputPort::Kind::Wire;
		p.wire_ids.assign(ids, ids + n);
		prog.outputs.push_back(std::move(p));
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
