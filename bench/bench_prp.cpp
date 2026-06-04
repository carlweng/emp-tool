// crypto/prp.h — pseudorandom permutation = fixed-key AES-128-ECB. Models
// f(x) = AES_K(x) as a random permutation in the random-permutation model.
// Throughput benchmark.
//
// What's in prp.h:
//   PRP()                          zero-key constructor
//   PRP(const char * key)          key from 16 raw bytes (loadu)
//   PRP(const block & key)         key from a block
//   permute_block(blks, n)         n-block in-place AES-ECB
// To re-key, reassign: prp = PRP(new_key).

#include "emp-tool/emp-tool.h"

#include <openssl/evp.h>

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

// ---------- helpers ----------

static block bytes_to_block(const uint8_t b[16]) {
	return _mm_loadu_si128(reinterpret_cast<const __m128i *>(b));
}

static void openssl_aes128_ecb(const uint8_t key[16], const uint8_t *in,
                               uint8_t *out, int nblks) {
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr);
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	int outl = 0, finl = 0;
	EVP_EncryptUpdate(ctx, out, &outl, in, 16 * nblks);
	EVP_EncryptFinal_ex(ctx, out + outl, &finl);
	EVP_CIPHER_CTX_free(ctx);
}

// ---------- throughput bench ----------

template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
	for (int i = 0; i < 32; ++i) {
		fn();
		asm volatile("" : "+m"(*(char *)clob) : : "memory");
	}
	int64_t iters = 64;
	while (true) {
		auto a = clk::now();
		for (int64_t i = 0; i < iters; ++i) {
			fn();
			asm volatile("" : "+m"(*(char *)clob) : : "memory");
		}
		double el = chrono::duration<double>(clk::now() - a).count();
		if (el >= seconds) return double(iters) / el;
		iters *= 2;
	}
}

static void print_vec(const string &lbl, double calls, int n) {
	double GiBps = calls * (double)n * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * n);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

static void bench(double sec) {
	PRG prg;
	PRP prp;

	cout << "=== permute_block (sweep N blocks) ===\n";
	for (int n : {1, 4, 8, 16, 64, 256, 1024, 4096, 16384}) {
		vector<block> buf(n);
		prg.random_block(buf.data(), n);
		double calls = run_for(sec, [&]() { prp.permute_block(buf.data(), n); }, buf.data());
		ostringstream lbl; lbl << "permute_block(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
