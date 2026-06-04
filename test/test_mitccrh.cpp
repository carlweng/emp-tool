// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Yang 2019).
// A batched key schedule + AES-encrypt-then-XOR-input construction used by
// half-gates garbling. Read example() first; the rest is verification.
//
// What's in mitccrh.h:
//   MITCCRH<BatchSize>                       AES_KEY scheduled_key[B], gid, start_point
//   setS(s)                                  reset start_point + gid
//   renew_ks() / renew_ks(gid)               schedule B keys from gid sequence
//   renew_ks(const block * tweaks)           schedule B keys from explicit tweaks
//   hash<K, H>(blks)                         consume K scheduled keys, hash K*H blocks
//   hash_cir<K, H>(blks)                     sigma() each block first, then hash<K,H>

#include "emp-tool/emp-tool.h"

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

	// (1) setS + hash<K, H>: feed K*H blocks, the construction XORs input back
	// over the encrypted output (TMMO).
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	MITCCRH<8> mit;
	mit.setS(S);

	block buf[8] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4),
		makeBlock(0, 5), makeBlock(0, 6), makeBlock(0, 7), makeBlock(0, 8),
	};
	mit.hash<8, 1>(buf);
	cout << "  hash<8,1>(buf)[0]   = " << buf[0] << "\n";
	cout << "  hash<8,1>(buf)[7]   = " << buf[7] << "\n";

	// (2) hash_cir<K, H> applies sigma() first, then hash. Used in half-gates.
	mit.setS(S);  // reset for a fresh transcript
	block buf2[2] = {makeBlock(0, 0xAA), makeBlock(0, 0xBB)};
	mit.hash_cir<2, 1>(buf2);
	cout << "  hash_cir<2,1>[0]    = " << buf2[0] << "\n";

	// (3) renew_ks(tweaks): schedule from explicit tweaks instead of gid.
	mit.setS(S);
	block tweaks[8];
	for (int i = 0; i < 8; ++i) tweaks[i] = makeBlock(i + 100, 0);
	mit.renew_ks(tweaks);
	block buf3[8];
	for (int i = 0; i < 8; ++i) buf3[i] = makeBlock(i, i);
	mit.hash<8, 1>(buf3);
	cout << "  hash<8,1> w/ tweaks[0] = " << buf3[0] << "\n";
}

// ---------- correctness ----------
//
// Reference: keys[i] = S ^ makeBlock(gid0+i, 0); schedule all B with
// AES_opt_key_schedule<B>; ParaEnc<K, H> consumes scheduled_key[0..K), then
// MITCCRH XORs the ciphertext with the original input.

template <int K, int H, int B = 8>
static bool check_mitccrh_against_paraenc() {
	static_assert(K <= B && B % K == 0, "MITCCRH<B> requires K | B");
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	const uint64_t gid0 = 7;

	MITCCRH<B> mit;
	mit.setS(S);
	mit.gid = gid0;
	mit.key_used = B;  // force a fresh schedule on first hash()

	block in[K * H];
	PRG().random_block(in, K * H);

	block emp_out[K * H];
	memcpy(emp_out, in, sizeof(in));
	mit.template hash<K, H>(emp_out);

	block ref_keys[B];
	for (int i = 0; i < B; ++i) ref_keys[i] = S ^ makeBlock(gid0 + (uint64_t)i, 0);
	AES_KEY skeys[B];
	AES_opt_key_schedule<B>(ref_keys, skeys);

	block ref_out[K * H];
	memcpy(ref_out, in, sizeof(in));
	ParaEnc<K, H>(ref_out, skeys);
	for (int i = 0; i < K * H; ++i) ref_out[i] = ref_out[i] ^ in[i];

	bool ok = memcmp(emp_out, ref_out, sizeof(in)) == 0;
	ostringstream os;
	os << "  [MITCCRH<" << B << ">::hash<" << K << "," << H << "> = ParaEnc^in]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir() {
	const block S = makeBlock(0x1234ULL, 0x5678ULL);
	MITCCRH<8> a, b;
	a.setS(S); b.setS(S);
	a.gid = b.gid = 0;
	a.key_used = b.key_used = 8;

	block in[K * H];
	PRG().random_block(in, K * H);

	block via_cir[K * H], via_manual[K * H];
	memcpy(via_cir, in, sizeof(in));
	a.template hash_cir<K, H>(via_cir);

	for (int i = 0; i < K * H; ++i) via_manual[i] = sigma(in[i]);
	b.template hash<K, H>(via_manual);

	bool ok = memcmp(via_cir, via_manual, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash_cir<" << K << "," << H << "> = hash(sigma(.))]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_out_of_place() {
	const block S = makeBlock(0x9999ULL, 0xaaaaULL);
	const uint64_t gid0 = 42;
	MITCCRH<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.gid = out_place.gid = gid0;
	in_place.key_used = out_place.key_used = 8;

	block in[K * H];
	PRG().random_block(in, K * H);
	block saved[K * H], via_in_place[K * H], via_out_place[K * H];
	memcpy(saved, in, sizeof(in));
	memcpy(via_in_place, in, sizeof(in));

	in_place.template hash<K, H>(via_in_place);
	out_place.template hash<K, H>(via_out_place, in);

	bool ok = memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
	          memcmp(in, saved, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash<" << K << "," << H << "> out-of-place = in-place]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir_out_of_place() {
	const block S = makeBlock(0xbbbbULL, 0xccccULL);
	const uint64_t gid0 = 77;
	MITCCRH<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.gid = out_place.gid = gid0;
	in_place.key_used = out_place.key_used = 8;

	block in[K * H];
	PRG().random_block(in, K * H);
	block saved[K * H], via_in_place[K * H], via_out_place[K * H];
	memcpy(saved, in, sizeof(in));
	memcpy(via_in_place, in, sizeof(in));

	in_place.template hash_cir<K, H>(via_in_place);
	out_place.template hash_cir<K, H>(via_out_place, in);

	bool ok = memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
	          memcmp(in, saved, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash_cir<" << K << "," << H << "> out-of-place = in-place]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_setS_resets_gid() {
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.gid = 99;
	mit.setS(makeBlock(3, 4));
	bool ok = (mit.gid == 0);
	cout << "  [setS resets gid]                              " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_setS_resets_gid();
	ok &= check_mitccrh_against_paraenc<1, 1>();
	ok &= check_mitccrh_against_paraenc<1, 4>();
	ok &= check_mitccrh_against_paraenc<2, 1>();
	ok &= check_mitccrh_against_paraenc<4, 1>();
	ok &= check_mitccrh_against_paraenc<8, 1>();
	ok &= check_mitccrh_against_paraenc<8, 4>();
	ok &= check_hash_cir<2, 1>();
	ok &= check_hash_cir<2, 2>();
	ok &= check_hash_cir<8, 1>();
	ok &= check_out_of_place<1, 1>();
	ok &= check_out_of_place<8, 4>();
	ok &= check_hash_cir_out_of_place<2, 1>();
	ok &= check_hash_cir_out_of_place<8, 1>();
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
