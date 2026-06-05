#ifndef EMP_CLEAR_BACKEND_H__
#define EMP_CLEAR_BACKEND_H__

#include "emp-tool/core/block.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/execution/backend.h"
#include "emp-tool/circuits/boolean_program.h"
#include "emp-tool/circuits/empbc.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace emp {

// Plaintext / circuit-capture backend. A wire carries:
//   - gid:        circuit wire id (only meaningful for private wires)
//   - is_public:  1 = public constant, 0 = private share
//   - value:      0 or 1
// With an empty filename it just evaluates in plaintext. With a non-empty
// filename it ALSO captures the executed computation as a native .empbc
// BooleanProgram, written on finalize(). Unlike the frontend RecordBackend it
// captures impure flows (it sees feed/reveal), so it stays a separate path.
// Precondition for capture: all secret feeds occur before any gate, so input
// wires are the leading [0, num_inputs) — validate_program() rejects a capture
// that violates this rather than emitting a malformed file.
struct ClearWire {
	uint64_t gid;
	uint32_t is_public;
	uint32_t value;
};

class ClearBackend : public Backend {
public:
	int64_t gid = 0;
	uint64_t gates = 0, ands = 0;
	uint64_t n1 = 0, n2 = 0, n3 = 0;
	bool print = false;
	std::string filename;
	emp::circuit::BooleanProgram prog;   // captured circuit (when print)
	struct OutputRef {
		bool is_public;
		bool value;
		int64_t gate_id;
	};
	std::vector<OutputRef> output_indices;

	static constexpr ClearWire public_one  { /*gid=*/0, /*is_public=*/1, /*value=*/1 };
	static constexpr ClearWire public_zero { /*gid=*/0, /*is_public=*/1, /*value=*/0 };

	explicit ClearBackend(const std::string& filename_ = "")
	    : Backend(PUBLIC), filename(filename_) {
		print = !filename.empty();
	}

	~ClearBackend() override {}

	size_t wire_bytes() const override { return sizeof(ClearWire); }

	void public_label(void* out, bool b) override {
		*static_cast<ClearWire*>(out) = b ? public_one : public_zero;
	}

	void and_gate(void* out, const void* l_, const void* r_) override {
		const ClearWire& l = *static_cast<const ClearWire*>(l_);
		const ClearWire& r = *static_cast<const ClearWire*>(r_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		// Capture operand ids before writing o: an in-place op (a &= b) makes
		// `out` alias `l`, so o = {...} would clobber l.gid before we record it.
		const uint32_t lg = (uint32_t)l.gid, rg = (uint32_t)r.gid;
		if (l.is_public) { o = l.value ? r : public_zero; return; }
		if (r.is_public) { o = r.value ? l : public_zero; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, l.value & r.value };
		if (print)
			prog.gates.push_back({lg, rg, (uint32_t)gid, emp::circuit::Op::And});
		++gid; ++gates; ++ands;
	}

	void xor_gate(void* out, const void* l_, const void* r_) override {
		const ClearWire& l = *static_cast<const ClearWire*>(l_);
		const ClearWire& r = *static_cast<const ClearWire*>(r_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		const uint32_t lg = (uint32_t)l.gid, rg = (uint32_t)r.gid;  // before any overwrite of o
		if (l.is_public && l.value) { not_gate(out, r_); return; }
		if (r.is_public && r.value) { not_gate(out, l_); return; }
		if (l.is_public)            { o = r; return; }
		if (r.is_public)            { o = l; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, l.value ^ r.value };
		if (print)
			prog.gates.push_back({lg, rg, (uint32_t)gid, emp::circuit::Op::Xor});
		++gid; ++gates;
	}

	void not_gate(void* out, const void* in_) override {
		const ClearWire& in = *static_cast<const ClearWire*>(in_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		const uint32_t ing = (uint32_t)in.gid;   // before o (may alias in) is overwritten
		if (in.is_public) { o = in.value ? public_zero : public_one; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, in.value ^ 1u };
		if (print)
			prog.gates.push_back({ing, 0, (uint32_t)gid, emp::circuit::Op::Not});
		++gid; ++gates;
	}

	void feed(void* out, int from_party, const bool* in, size_t n) override {
		auto* lbls = static_cast<ClearWire*>(out);
		if (from_party == PUBLIC) {
			for (size_t i = 0; i < n; ++i)
				public_label(&lbls[i], in[i]);
			return;
		}
		for (size_t i = 0; i < n; ++i)
			lbls[i] = private_label(in[i]);
		if (from_party == ALICE) n1 += n;
		else                     n2 += n;
	}

	void reveal(bool* out, int /*to_party*/, const void* in_, size_t n) override {
		const ClearWire* lbls = static_cast<const ClearWire*>(in_);
		for (size_t i = 0; i < n; ++i) {
			const bool is_pub = (lbls[i].is_public != 0);
			const bool val    = (lbls[i].value != 0);
			output_indices.push_back({is_pub, val,
			                          is_pub ? int64_t{0}
			                                 : static_cast<int64_t>(lbls[i].gid)});
			out[i] = val;
		}
		n3 += n;
	}

	uint64_t num_and() override { return ands; }

	void finalize() override {
		if (!print) return;
		// Assemble the program's outputs from the revealed wires. A private
		// reveal maps to its producing wire id; a public-constant reveal
			// synthesizes a first-class Const0/Const1 gate (deduped). Inputs are
			// the leading n1+n2 wires; validate_program()
			// enforces that (and the whole structure) before we write the file.
		int c0 = -1, c1 = -1;
		std::vector<uint32_t> outputs;
		outputs.reserve(output_indices.size());
		for (auto& v : output_indices) {
			if (!v.is_public) { outputs.push_back((uint32_t)v.gate_id); continue; }
			int& cache = v.value ? c1 : c0;
			if (cache < 0) {
				cache = (int)gid++;
				prog.gates.push_back({0, 0, (uint32_t)cache,
				    v.value ? emp::circuit::Op::Const1 : emp::circuit::Op::Const0});
			}
			outputs.push_back((uint32_t)cache);
		}
		prog.num_inputs = (uint32_t)(n1 + n2);
		prog.num_wires  = (uint32_t)gid;
		prog.outputs    = std::move(outputs);
		// save_empbc validates the program before writing (rejecting e.g. a
		// capture where a secret feed followed a gate, breaking feeds-first).
		emp::circuit::save_empbc_file(filename.c_str(), prog);
	}

private:
	ClearWire private_label(bool v) {
		return ClearWire{ static_cast<uint64_t>(gid++), 0, v ? 1u : 0u };
	}
};

inline void setup_clear_backend(const std::string& filename = "") {
	backend = new ClearBackend(filename);
}

inline void finalize_clear_backend() {
	if (!backend) return;
	backend->finalize();
	delete backend;
	backend = nullptr;
}

}  // namespace emp
#endif
