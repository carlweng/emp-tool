#ifndef EMP_CCRH_H__
#define EMP_CCRH_H__
#include "emp-tool/crypto/prp.h"
#include <stdio.h>
namespace emp {

/*
 * By default, CCRH uses zero_block as the AES key.
 * Here we model f(x) = AES_{00..0}(x) as a random permutation (and thus in the RPM model)
 */
class CCRH: public PRP { public:
	CCRH(const block& key = zero_block): PRP(key) {
	}

	block H(block in) {
		alignas(16) block t = sigma(in);
		detail::ParaEncXorInput_impl<1,1>(&t, &t, &aes);
		return t;
	}

	// NOTE: the body is fully unrolled by the compiler for small n. For
	// large n the unrolled body spills its per-block scratch (each block
	// needs a SIMD register, and the target's architectural register
	// file is finite). Callers wanting large batches should use Hn()
	// below.
	//
	// H(x) = AES_K(σ(x)) ⊕ σ(x) (Davies–Meyer over σ). Compute σ(in)
	// once into `pt`, then use the AES helper that stores AES(pt) ^ pt
	// directly into `out` with one σ pass and one output write.
	template<int n>
	void H(block out[n], block in[n]) {
		block pt[n];
		for (int i = 0; i < n; ++i) pt[i] = sigma(in[i]);
		detail::ParaEncXorInput_impl<1,n>(out, pt, &aes);
	}

	void Hn(block*out, block* in, int64_t length, block * scratch = nullptr) {
		if (length <= 0) return;
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[length];
		}

		for (int64_t i = 0; i < length; ++i)
			scratch[i] = sigma(in[i]);

		detail::ParaEncXorInput_runtime_impl(out, scratch, &aes, 1, length);

		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}
};

}//namespace
#endif// CCRH_H__
