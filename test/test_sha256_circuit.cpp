// Test for emp-tool/circuits/sha256_circuit.h:
//   - NIST vectors for "abc" and the empty string
//   - cross-check vs OpenSSL EVP_sha256 across block-boundary lengths
//   - a multi-block message
//   - reports the AND-gate count per compression block

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/sha256_circuit.h"
#include <openssl/evp.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace emp;

// Reference digest via OpenSSL.
static void openssl_sha256(const uint8_t* in, size_t n, uint8_t out[32]) {
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
	EVP_DigestUpdate(ctx, in, n);
	unsigned int len = 32;
	EVP_DigestFinal_ex(ctx, out, &len);
	EVP_MD_CTX_free(ctx);
}

// In-circuit digest of `n` cleartext bytes (fed as ALICE so the clear backend
// counts ANDs rather than constant-folding them).
static void circuit_sha256(const uint8_t* in, size_t n, uint8_t out[32]) {
	std::vector<Bit> bits;
	bits.reserve(n * 8);
	for (size_t i = 0; i < n; ++i)
		for (int k = 0; k < 8; ++k)
			bits.push_back(Bit(((in[i] >> k) & 1) != 0, ALICE));   // LSB-first

	BitVec output;
	SHA256_Calculator calc;
	calc.sha256(&output, bits.data(), bits.size());
	output.reveal(out, PUBLIC);
}

static bool eq32(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 32) == 0; }

static void hex(const char* tag, const uint8_t* d) {
	printf("  %s", tag);
	for (int i = 0; i < 32; ++i) printf("%02x", d[i]);
	printf("\n");
}

// example(): run a SINGLE compression directly (no padding / multi-block
// driver), the lowest-level entry point. The 512-bit block is given as 16
// big-endian 32-bit words; here it is the padded block for "abc"
// (0x61626380, 0..., bit-length 0x18), folded into the SHA-256 IV. The
// result is the SHA-256("abc") digest words.
static bool example() {
	UInt32 state[8];
	for (int i = 0; i < 8; ++i) state[i] = UInt32(sha256_detail::H0[i], PUBLIC);

	const uint32_t blk[16] = {0x61626380,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x18};
	UInt32 msg[16];
	for (int j = 0; j < 16; ++j) msg[j] = UInt32(blk[j], PUBLIC);

	sha256_compress<block>(state, msg);

	const uint32_t want[8] = {0xba7816bf,0x8f01cfea,0x414140de,0x5dae2223,
	                          0xb00361a3,0x96177a9c,0xb410ff61,0xf20015ad};
	bool ok = true;
	printf("single compression \"abc\":");
	for (int i = 0; i < 8; ++i) {
		uint32_t w = state[i].reveal<uint32_t>(PUBLIC);
		printf(" %08x", w);
		ok &= (w == want[i]);
	}
	printf("  %s\n", ok ? "OK" : "FAIL");
	return ok;
}

int main() {
	setup_clear_backend();
	int fail = 0;

	if (!example()) ++fail;

	// 1. NIST vector: "abc".
	{
		const uint8_t msg[3] = {'a', 'b', 'c'};
		const uint8_t want[32] = {
			0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
			0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
		uint8_t got[32];
		circuit_sha256(msg, 3, got);
		bool ok = eq32(got, want);
		printf("SHA-256(\"abc\"): %s\n", ok ? "OK" : "FAIL");
		if (!ok) { hex("got : ", got); hex("want: ", want); ++fail; }
	}

	// 2. NIST vector: empty string.
	{
		const uint8_t want[32] = {
			0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
			0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
		uint8_t got[32];
		circuit_sha256(nullptr, 0, got);
		bool ok = eq32(got, want);
		printf("SHA-256(\"\"):    %s\n", ok ? "OK" : "FAIL");
		if (!ok) { hex("got : ", got); hex("want: ", want); ++fail; }
	}

	// 3. Cross-check vs OpenSSL at lengths around the 55/56/64-byte block edges
	//    (the cases where padding spills into an extra block).
	{
		const size_t lens[] = {1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 200};
		int xv_fail = 0;
		srand(0x5a256);
		for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); ++li) {
			size_t n = lens[li];
			std::vector<uint8_t> msg(n);
			for (size_t i = 0; i < n; ++i) msg[i] = (uint8_t)(rand() & 0xff);
			uint8_t got[32], ref[32];
			circuit_sha256(msg.data(), n, got);
			openssl_sha256(msg.data(), n, ref);
			if (!eq32(got, ref)) {
				++xv_fail;
				printf("  XV FAIL at len %zu\n", n);
				hex("got: ", got); hex("ref: ", ref);
			}
		}
		printf("OpenSSL cross-check (11 lengths): %d failures\n", xv_fail);
		fail += xv_fail;
	}

	// 4. Multi-block message (2000 bytes), and an AND-count per block.
	{
		std::vector<uint8_t> msg(2000);
		for (size_t i = 0; i < msg.size(); ++i) msg[i] = (uint8_t)(i % 251);
		uint8_t ref[32];
		openssl_sha256(msg.data(), msg.size(), ref);

		uint64_t and_before = backend->num_and();
		uint8_t got[32];
		circuit_sha256(msg.data(), msg.size(), got);
		uint64_t ands = backend->num_and() - and_before;

		bool ok = eq32(got, ref);
		// 2000 bytes -> ceil((2000+1+8)/64) = 32 compression blocks.
		printf("2000-byte message: %s (%llu ANDs, %llu/block over 32 blocks)\n",
		       ok ? "OK" : "FAIL", (unsigned long long)ands,
		       (unsigned long long)(ands / 32));
		if (!ok) ++fail;
	}

	printf("%s\n", fail == 0 ? "ALL OK" : "FAILURES PRESENT");
	finalize_clear_backend();
	return fail == 0 ? 0 : 1;
}
