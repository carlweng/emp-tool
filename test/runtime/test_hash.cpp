// crypto/hash.h — SHA-256 wrapper around OpenSSL EVP. Read example() first;
// the rest is verification. (EC-point KDF moved to crypto/ro.h,
// covered by test_ro.cpp.)
//
// What's in hash.h:
//   Hash::put(p, n)                           feed n bytes
//   Hash::put_block(b, n=1)                   feed n blocks
//   Hash::digest(out, reset_after=true)       finalize (or snapshot)
//   Hash::reset()                             reinit context
//   Hash::hash_once(out, p, n)                one-shot SHA-256
//   Hash::hash_for_block(p, n)                first 16 bytes of SHA-256, as block

#include "emp-tool/emp-tool.h"

#include <openssl/sha.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;


// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Streaming put + digest.
	Hash h;
	h.put("abc", 3);
	uint8_t out[Hash::DIGEST_SIZE];
	h.digest(out);
	cout << "  SHA-256(\"abc\") =\n    " << to_hex(out, Hash::DIGEST_SIZE) << "\n";

	// (2) put_block: feed a block-typed buffer.
	block b = makeBlock(0xDEADBEEFULL, 0xCAFEBABEULL);
	h.put_block(&b);
	h.digest(out);
	cout << "  SHA-256(b)     =\n    " << to_hex(out, Hash::DIGEST_SIZE) << "\n";

	// (3) Snapshot mode (reset_after = false): finalize a copy without
	// disturbing the running transcript. Used for streaming Fiat-Shamir.
	h.put("hello", 5);
	uint8_t snap1[Hash::DIGEST_SIZE], snap2[Hash::DIGEST_SIZE];
	h.digest(snap1, /*reset_after=*/false);
	h.put(" world", 6);
	h.digest(snap2);  // resets
	cout << "  snap1=" << to_hex(snap1, 8) << "...  snap2=" << to_hex(snap2, 8) << "...\n";

	// (4) One-shot helpers.
	uint8_t one[Hash::DIGEST_SIZE];
	Hash::hash_once(one, "abc", 3);
	block hb = Hash::hash_for_block("abc", 3);
	cout << "  hash_once(\"abc\")[0..8]   = " << to_hex(one, 8) << "\n";
	cout << "  hash_for_block(\"abc\")    = " << hb << "\n";
}

// ---------- correctness ----------

static bool check_known_vectors() {
	// FIPS 180-4 examples.
	struct V { const char *msg; size_t n; const char *hex; };
	V vs[] = {
		{"abc", 3,
		 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
		{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
		 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
		{"", 0,
		 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
	};
	bool all_ok = true;
	for (auto &v : vs) {
		uint8_t out[Hash::DIGEST_SIZE];
		Hash::hash_once(out, v.msg, (int)v.n);
		bool ok = to_hex(out, Hash::DIGEST_SIZE) == v.hex;
		all_ok &= ok;
	}
	cout << "  [FIPS 180-4 known answers]            " << (all_ok ? "OK" : "FAIL") << "\n";
	return all_ok;
}

static bool check_against_openssl(int trials = 64) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		int n = 1 + (t * 17) % 4096;
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), n);

		uint8_t emp_out[Hash::DIGEST_SIZE];
		Hash::hash_once(emp_out, buf.data(), n);

		uint8_t ref[SHA256_DIGEST_LENGTH];
		SHA256(buf.data(), n, ref);

		if (memcmp(emp_out, ref, SHA256_DIGEST_LENGTH) != 0) {
			cout << "  [Hash::hash_once vs OpenSSL SHA256]  FAIL  t=" << t << " n=" << n << "\n";
			return false;
		}
	}
	cout << "  [Hash::hash_once vs OpenSSL SHA256]   OK   trials=" << trials << "\n";
	return true;
}

static bool check_streaming_equals_oneshot(int trials = 32) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		int n = 256 + (t * 31) % 8192;
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), n);

		uint8_t one[Hash::DIGEST_SIZE], stream[Hash::DIGEST_SIZE];
		Hash::hash_once(one, buf.data(), n);

		Hash h;
		// Feed in random-sized chunks.
		int off = 0;
		while (off < n) {
			int chunk = 1 + (off * 7 + t) % 257;
			if (chunk > n - off) chunk = n - off;
			h.put(buf.data() + off, chunk);
			off += chunk;
		}
		h.digest(stream);

		if (memcmp(one, stream, Hash::DIGEST_SIZE) != 0) {
			cout << "  [streaming = one-shot]                FAIL  t=" << t << "\n";
			return false;
		}
	}
	cout << "  [streaming put = hash_once]           OK   trials=" << trials << "\n";
	return true;
}

static bool check_snapshot_does_not_disturb() {
	// digest(_, reset_after=false) must leave the transcript intact.
	Hash a, b;
	a.put("hello", 5);
	b.put("hello", 5);
	uint8_t snap[Hash::DIGEST_SIZE];
	a.digest(snap, /*reset_after=*/false);
	a.put(" world", 6);
	b.put(" world", 6);
	uint8_t da[Hash::DIGEST_SIZE], db[Hash::DIGEST_SIZE];
	a.digest(da);
	b.digest(db);
	bool ok = memcmp(da, db, Hash::DIGEST_SIZE) == 0;
	cout << "  [snapshot leaves transcript intact]   " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_known_vectors();
	ok &= check_against_openssl();
	ok &= check_streaming_equals_oneshot();
	ok &= check_snapshot_does_not_disturb();
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
