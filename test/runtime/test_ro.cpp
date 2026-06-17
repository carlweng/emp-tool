// crypto/ro.h — random-oracle helper. Absorb typed, length-framed fields,
// squeeze a block / 32-byte digest / curve Point.
//
// These tests pin the wire encoding by rebuilding the absorbed message M with
// an independent manual frame-builder (the format spec) and checking that the
// RO produces the same SHA-256 / hash_to_point output. Any drift in absorb's
// framing — field order, type tag, or length prefix — breaks an assert.

#include "emp-tool/emp-tool.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace std;
using namespace emp;

// Independent re-implementation of RO's frame format:
//   u32_LE(type) || u32_LE(len) || bytes
static void mframe(vector<unsigned char>& m, uint32_t type,
                   const void* p, size_t n) {
	uint32_t len = (uint32_t)n;
	size_t off = m.size();
	m.resize(off + 8 + n);
	memcpy(m.data() + off, &type, 4);
	memcpy(m.data() + off + 4, &len, 4);
	if (n) memcpy(m.data() + off + 8, p, n);
}
enum { T_STR = 1, T_BYTES = 2, T_BLOCK = 3, T_U64 = 4, T_POINT = 5 };

static bool eq(block a, block b) { return memcmp(&a, &b, sizeof(block)) == 0; }

int main() {
	ECGroup G;
	const char* dom = "emp-tool:test:ro";
	block sid = makeBlock(0xA1A2A3A4A5A6A7A8ULL, 0x1112131415161718ULL);
	block x   = makeBlock(0xDEADBEEFCAFEF00DULL, 0x0102030405060708ULL);
	uint64_t id = 0x4243444546474849ULL;
	Point P = G.mul_gen(G.rand_scalar());
	size_t plen = P.size();
	vector<unsigned char> pbin(plen);
	P.to_bin(pbin.data(), plen);

	// 1. block output: domain + sid + id + block + point + raw bytes.
	{
		const char* raw = "raw-bytes-field";
		vector<unsigned char> m;
		mframe(m, T_STR,   dom, strlen(dom));
		mframe(m, T_BLOCK, &sid, sizeof(block));
		mframe(m, T_U64,   &id, sizeof(id));
		mframe(m, T_BLOCK, &x, sizeof(block));
		mframe(m, T_POINT, pbin.data(), plen);
		mframe(m, T_BYTES, raw, strlen(raw));

		block got = RO(dom, sid).absorb(id).absorb(x).absorb(P)
		                .absorb(raw, strlen(raw)).squeeze_block();
		assert(eq(got, Hash::hash_for_block(m.data(), (int64_t)m.size())));
	}

	// 2. squeeze_digest equals hash_once over the same M.
	{
		vector<unsigned char> m;
		mframe(m, T_STR,   dom, strlen(dom));
		mframe(m, T_BLOCK, &sid, sizeof(block));
		mframe(m, T_U64,   &id, sizeof(id));
		unsigned char want[Hash::DIGEST_SIZE], got[Hash::DIGEST_SIZE];
		Hash::hash_once(want, m.data(), (int64_t)m.size());
		RO(dom, sid).absorb(id).squeeze_digest(got);
		assert(memcmp(want, got, Hash::DIGEST_SIZE) == 0);
	}

	// 3. squeeze_point equals hash_to_point(M, domain-as-DST).
	{
		vector<unsigned char> m;
		mframe(m, T_STR,   dom, strlen(dom));
		mframe(m, T_BLOCK, &sid, sizeof(block));
		mframe(m, T_BLOCK, &x, sizeof(block));
		Point want = G.hash_to_point((const char*)m.data(), m.size(),
		                             dom, strlen(dom));
		Point got  = RO(dom, sid).absorb(x).squeeze_point(G);
		assert(want == got);
	}

	// 4. label absorb (string_view) matches a raw type-1 frame.
	{
		const char* label = "CRS g0";
		vector<unsigned char> m;
		mframe(m, T_STR,   dom, strlen(dom));
		mframe(m, T_BLOCK, &sid, sizeof(block));
		mframe(m, T_STR,   label, strlen(label));
		block got = RO(dom, sid).absorb(string_view(label)).squeeze_block();
		assert(eq(got, Hash::hash_for_block(m.data(), (int64_t)m.size())));
	}

	// 5. Injective in the call sequence: absorb(block) (type 3) differs from
	//    absorb(&block, 16) (type 2) of the same 16 bytes.
	{
		block a = RO(dom, sid).absorb(x).squeeze_block();
		block b = RO(dom, sid).absorb(&x, sizeof(block)).squeeze_block();
		assert(!eq(a, b));
	}

	// 6. Different sid → different output; length framing can't be forged.
	{
		block sid2 = makeBlock(0, 1);
		assert(!eq(RO(dom, sid).squeeze_block(), RO(dom, sid2).squeeze_block()));
		// "ab"+"c" vs "a"+"bc": length framing keeps them distinct.
		block s1 = RO(dom, sid).absorb(string_view("ab")).absorb(string_view("c")).squeeze_block();
		block s2 = RO(dom, sid).absorb(string_view("a")).absorb(string_view("bc")).squeeze_block();
		assert(!eq(s1, s2));
	}

	cout << "All RO tests passed." << endl;
	return 0;
}
