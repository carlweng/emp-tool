#ifndef EMP_FRONTEND_PASSES_H__
#define EMP_FRONTEND_PASSES_H__

// Protocol-neutral analysis passes over a recorded BooleanProgram. Each is a
// pure function computed once (at compile time) and cached on the Circuit. They
// describe graph facts every backend can use; a backend reads the ones it needs
// (ag2pc uses liveness; GMW will use the schedule) and ignores the rest.
//
// All passes operate in the recorder's wire-id space (the program as recorded).
// A protocol backend that compacts to its own dense id space recomputes what it
// needs there; these passes stay backend-independent.

#include "emp-tool/frontend/boolean_program.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace emp {
namespace frontend {

struct CountStats {
	int64_t num_and = 0, num_xor = 0, num_not = 0, num_const = 0;
	int num_input_bits = 0, num_output_bits = 0;
	int num_wire = 0, num_gate = 0;
};

inline CountStats count_pass(const BooleanProgram& p) {
	CountStats s;
	s.num_wire = p.num_wire;
	s.num_gate = p.num_gate();
	s.num_input_bits  = p.total_input_bits();
	s.num_output_bits = p.total_output_bits();
	for (const auto& g : p.gates) {
		switch (g.op) {
			case Op::AND:    ++s.num_and;   break;
			case Op::XOR:    ++s.num_xor;   break;
			case Op::NOT:    ++s.num_not;   break;
			case Op::CONST0:
			case Op::CONST1: ++s.num_const; break;
		}
	}
	return s;
}

struct LivenessStats {
	std::vector<int> last_use;   // [num_wire] last gate index reading w, -1 if never read
	std::vector<int> first_def;  // [num_wire] producing gate index, -1 if an input wire
};

inline LivenessStats liveness_pass(const BooleanProgram& p) {
	LivenessStats s;
	s.last_use.assign(p.num_wire, -1);
	s.first_def.assign(p.num_wire, -1);
	const int G = p.num_gate();
	for (int gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		if (g.out >= 0) s.first_def[g.out] = gi;
		if (g.is_const()) continue;                       // CONST reads nothing
		if (g.in0 >= 0) s.last_use[g.in0] = gi;
		if (!g.is_not() && g.in1 >= 0) s.last_use[g.in1] = gi;
	}
	return s;
}

struct ScheduleStats {
	std::vector<int> wire_level;   // [num_wire] AND-depth at which w becomes available
	LevelInfo        levels;       // AND gate indices grouped per level + max depth
};

inline ScheduleStats schedule_pass(const BooleanProgram& p) {
	ScheduleStats s;
	s.wire_level.assign(p.num_wire, 0);
	const int G = p.num_gate();
	int depth = 0;
	for (int gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		int lv = 0;
		switch (g.op) {
			case Op::CONST0:
			case Op::CONST1: lv = 0; break;
			case Op::NOT:    lv = s.wire_level[g.in0]; break;
			case Op::XOR:    lv = std::max(s.wire_level[g.in0], s.wire_level[g.in1]); break;
			case Op::AND:    lv = 1 + std::max(s.wire_level[g.in0], s.wire_level[g.in1]); break;
		}
		if (g.out >= 0) s.wire_level[g.out] = lv;
		depth = std::max(depth, lv);
	}
	s.levels.depth = depth;
	s.levels.and_gate_indices.assign(depth + 1, {});
	for (int gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		if (g.is_and()) s.levels.and_gate_indices[s.wire_level[g.out]].push_back(gi);
	}
	return s;
}

struct LayoutStats {
	// Forward-consumable liveness: frees[gi] lists wire ids whose last read is
	// gate gi (the inverse of LivenessStats::last_use), so a forward gate walk
	// can recycle slots inline without scanning the array. Outputs and never-
	// read wires are absent (they are roots / dead-on-arrival).
	std::vector<std::vector<int>> frees;   // [num_gate]
	// Output-rooted dead-code-elimination sizes (what a backend rooted on the
	// revealed/returned outputs would actually execute).
	int reachable_wire = 0;
	int reachable_and  = 0;
};

inline LayoutStats layout_pass(const BooleanProgram& p, const LivenessStats& live) {
	LayoutStats s;
	s.frees.assign(p.num_gate(), {});
	for (int w = 0; w < p.num_wire; ++w)
		if (live.last_use[w] >= 0) s.frees[live.last_use[w]].push_back(w);

	// Output-rooted DCE: mark wires reachable backward from output ports.
	std::vector<char> reach(p.num_wire, 0);
	std::vector<int> stack;
	for (const auto& op : p.outputs)
		for (int w : op.wire_ids)
			if (w >= 0 && !reach[w]) { reach[w] = 1; stack.push_back(w); }
	while (!stack.empty()) {
		int w = stack.back(); stack.pop_back();
		int pg = live.first_def[w];
		if (pg < 0) continue;                  // input wire: no producer to walk
		const Gate& g = p.gates[pg];
		auto visit = [&](int v) { if (v >= 0 && !reach[v]) { reach[v] = 1; stack.push_back(v); } };
		visit(g.in0);
		if (!g.is_not() && !g.is_const()) visit(g.in1);
	}
	for (int w = 0; w < p.num_wire; ++w) if (reach[w]) ++s.reachable_wire;
	for (const auto& g : p.gates)
		if (g.is_and() && g.out >= 0 && reach[g.out]) ++s.reachable_and;
	return s;
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_PASSES_H__
