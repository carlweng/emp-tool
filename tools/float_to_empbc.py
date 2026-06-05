#!/usr/bin/env python3
# HISTORICAL one-time converter. NOT a regeneration path and NOT part of the
# build. It documents how the embedded float builtins
# (emp-tool/circuits/float_builtins.cpp) were produced.
#
# It reads the LEGACY generated float circuit headers (float_circuits/
# float32_*.hpp) — each embedding a flat (in0,in1,out,kind) gate table inside a
# templated method — and emits one C++ source with each circuit as an embedded
# .empbc blob plus a process-wide call_once cache (builtin_circuit()).
#
# Those .hpp inputs were DELETED once converted: float_builtins.cpp (the .empbc
# blobs) is now the canonical source of truth for float builtins.
# To re-run for reference, restore the inputs from git history first:
#     git checkout <pre-conversion-rev> -- emp-tool/circuits/float_circuits/
# The .empbc byte layout written here MUST match the reader in
# emp-tool/circuits/empbc.h (fixed little-endian).

import re, sys, struct, os

SRC = os.path.join(os.path.dirname(__file__), "..", "emp-tool", "circuits", "float_circuits")
OUT = os.path.join(os.path.dirname(__file__), "..", "emp-tool", "circuits", "float_builtins.cpp")

# Op codes — must match emp::circuit::Op in boolean_program.h.
AND, XOR, NOT, CONST0, CONST1 = 0, 1, 2, 3, 4
# Legacy float-header kind codes: 0=AND, 1=XOR, 2=NOT.
KIND_TO_OP = {0: AND, 1: XOR, 2: NOT}

# (file stem, BuiltinCircuit enumerator) in the SAME order as float_builtins.h.
CIRCUITS = [
    ("float32_add",  "Float32Add"),
    ("float32_sub",  "Float32Sub"),
    ("float32_mul",  "Float32Mul"),
    ("float32_div",  "Float32Div"),
    ("float32_sq",   "Float32Sq"),
    ("float32_sqrt", "Float32Sqrt"),
    ("float32_sin",  "Float32Sin"),
    ("float32_cos",  "Float32Cos"),
    ("float32_exp",  "Float32Exp"),
    ("float32_exp2", "Float32Exp2"),
    ("float32_ln",   "Float32Ln"),
    ("float32_log2", "Float32Log2"),
    ("float32_eq",   "Float32Eq"),
    ("float32_le",   "Float32Le"),
    ("float32_leq",  "Float32Leq"),
]


def parse(stem):
    txt = open(os.path.join(SRC, stem + ".hpp")).read()
    num_wires = int(re.search(r"std::vector<Bit_T<Wire>> B\((\d+)\)", txt).group(1))
    num_inputs = 64 if re.search(r"B\[i\+32\]\s*=\s*rhs", txt) else 32

    body = re.search(r"uint32_t gates\[\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    nums = [int(x) for x in re.findall(r"-?\d+", body)]
    assert len(nums) % 4 == 0, stem
    gates = []  # (in0, in1, out, op)
    for i in range(0, len(nums), 4):
        in0, in1, out, kind = nums[i:i + 4]
        op = KIND_TO_OP[kind]
        if op == NOT:
            in1 = 0  # normalize unused operand
        gates.append((in0, in1, out, op))

    # Outputs + optional synthesized const-0 sign bit.
    m_bit = re.search(r"Bit_T<Wire>\s+ret\s*=\s*B\[(\d+)\]", txt)
    if m_bit:                                   # eq / le / leq -> single Bit
        outputs = [int(m_bit.group(1))]
    elif re.search(r"res\[31\]\s*=\s*Bit_T<Wire>\(false", txt):
        # exp / exp2 / sq: result bits 0..30 from the last 31 wires, bit 31 is a
        # public 0 -> synthesize a Const0 gate and route it to output[31].
        base = num_wires - 31
        const_wire = num_wires
        gates.append((0, 0, const_wire, CONST0))
        num_wires += 1
        outputs = list(range(base, base + 31)) + [const_wire]
    else:                                        # 32-bit Float result
        base = num_wires - 32
        outputs = list(range(base, base + 32))

    # The legacy buffer size B(N) is sometimes over-allocated (a few trailing
    # wires are never written — e.g. leq reserves 516 but uses 514). The
    # canonical IR is dense, so derive num_wires from the wires actually defined
    # (inputs + gate outputs) instead of the buffer size.
    max_out = max((out for _, _, out, _ in gates), default=num_inputs - 1)
    dense_wires = max(num_inputs, max_out + 1, max(outputs) + 1)
    return dense_wires, num_inputs, gates, outputs


def to_empbc(num_wires, num_inputs, gates, outputs):
    iw = 2 if num_wires <= 0xFFFF else 4
    def idx(v):
        return struct.pack("<H", v) if iw == 2 else struct.pack("<I", v)
    b = bytearray()
    b += b"EMPB"
    b += struct.pack("<H", 1)            # version
    b += struct.pack("<B", iw)
    b += struct.pack("<B", 0)            # flags
    b += struct.pack("<I", num_wires)
    b += struct.pack("<I", num_inputs)
    b += struct.pack("<I", len(outputs))
    b += struct.pack("<I", len(gates))
    for in0, in1, out, op in gates:
        b += idx(in0) + idx(in1) + idx(out)
        b += struct.pack("<B", op)
        b += b"\x00" * (iw - 1)          # op + reserved padded to one index slot
    for o in outputs:
        b += idx(o)
    return bytes(b)


def emit_array(name, data):
    out = ["static const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 20):
        out.append("  " + ",".join(str(x) for x in data[i:i + 20]) + ",")
    out.append("};")
    return "\n".join(out)


def main():
    blobs = []
    for stem, enum in CIRCUITS:
        nw, ni, gates, outs = parse(stem)
        data = to_empbc(nw, ni, gates, outs)
        blobs.append((stem, enum, data))
        print("%-14s wires=%-6d inputs=%-3d gates=%-6d outputs=%-3d bytes=%d"
              % (stem, nw, ni, len(gates), len(outs), len(data)), file=sys.stderr)

    L = []
    L.append("// AUTO-GENERATED by tools/float_to_empbc.py. DO NOT EDIT.")
    L.append("// Each float builtin as an embedded .empbc blob, parsed and cached")
    L.append("// once per process (call_once) the first time it is used.")
    L.append("")
    L.append('#include "emp-tool/circuits/float_builtins.h"')
    L.append('#include "emp-tool/circuits/empbc.h"')
    L.append("#include <mutex>")
    L.append("")
    L.append("namespace emp {")
    L.append("namespace circuit {")
    L.append("")
    for stem, enum, data in blobs:
        L.append(emit_array(stem + "_empbc", data))
    L.append("")
    L.append("namespace {")
    L.append("struct Blob { const unsigned char* p; std::size_t n; };")
    L.append("const Blob kBlobs[] = {")
    for stem, enum, data in blobs:
        L.append("  { %s_empbc, sizeof(%s_empbc) }," % (stem, stem))
    L.append("};")
    L.append("}  // namespace")
    L.append("")
    L.append("const BooleanProgram& builtin_circuit(BuiltinCircuit id) {")
    L.append("  constexpr int N = (int)BuiltinCircuit::Count;")
    L.append("  const int i = (int)id;")
    L.append("  if (i < 0 || i >= N) throw std::runtime_error(\"builtin_circuit: invalid circuit id\");")
    L.append("  static BooleanProgram progs[N];")
    L.append("  static std::once_flag flags[N];")
    L.append("  std::call_once(flags[i], [i] { progs[i] = load_empbc(kBlobs[i].p, kBlobs[i].n); });")
    L.append("  return progs[i];")
    L.append("}")
    L.append("")
    L.append("}  // namespace circuit")
    L.append("}  // namespace emp")
    open(OUT, "w").write("\n".join(L) + "\n")
    print("wrote " + os.path.normpath(OUT), file=sys.stderr)


if __name__ == "__main__":
    main()
