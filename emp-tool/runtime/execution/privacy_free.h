#ifndef EMP_PRIVACY_FREE_H__
#define EMP_PRIVACY_FREE_H__

#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/prp.h"

namespace emp {

// Privacy-free garbling: the per-AND garble / evaluate primitives over block
// labels and an AES_KEY. XOR/NOT are free (caller-handled); only AND needs a
// single ciphertext (`table`).

inline block privacy_free_garble(block LA0, block A1, block LB0, block B1,
                                  block delta, block* table, uint64_t idx,
                                  const AES_KEY* key) {
	(void)delta;  // delta is implicit via A1 = LA0 ^ delta etc.
	block tweak = makeBlock(2 * idx, 0ULL);
	block masks[2] = { sigma(LA0) ^ tweak, sigma(A1) ^ tweak };
	block keys[2]  = { masks[0], masks[1] };
	AES_ecb_encrypt_blks(keys, 2, key);
	block HLA0 = keys[0] ^ masks[0];
	block HA1  = keys[1] ^ masks[1];
	*reinterpret_cast<char*>(&HLA0) &= 0xfe;
	*reinterpret_cast<char*>(&HA1)  |= 0x01;
	block tmp = HLA0 ^ HA1;
	table[0] = tmp ^ LB0;
	(void)B1;
	return HLA0;
}

inline block privacy_free_eval(block A, block B, const block& table,
                                uint64_t idx, const AES_KEY* key) {
	block tweak = makeBlock(2 * idx, 0ULL);
	block tmp = sigma(A) ^ tweak;
	block mask = tmp;
	AES_ecb_encrypt_blks(&tmp, 1, key);
	block HA = tmp ^ mask;
	if (getLSB(A)) {
		*reinterpret_cast<char*>(&HA) |= 0x01;
		return HA ^ table ^ B;
	}
	*reinterpret_cast<char*>(&HA) &= 0xfe;
	return HA;
}

}  // namespace emp
#endif
