#ifndef EMP_PRG_H__
#define EMP_PRG_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/aes.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/constants.h"
#include <memory>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <emmintrin.h>

#ifdef ENABLE_RDSEED
#include <x86intrin.h>
#else 
#include <random>
#endif

namespace emp {

class PRG { public:
	uint64_t counter = 0;
	AES_KEY aes;
	block key;
	PRG(const void * seed = nullptr, int id = 0) {	
		if (seed != nullptr) {
			reseed((const block *)seed, id);
		} else {
			block v;
#ifndef ENABLE_RDSEED
			unsigned char r[16];
			int fd = open("/dev/urandom", O_RDONLY);
			if(fd == -1)
				error("urandom open fail!\n");
			int filled = 0;
			while (filled < 16) {
				int res = read(fd, r+filled, 16-filled);
				if(res == -1)
					error("urandom read fail!\n");
				filled+=res;
			}
			close(fd);
			v = _mm_loadu_si128((const __m128i *)r);
#else
			unsigned long long r0, r1;
			int i = 0;
			for(; i < 10; ++i)
				if(_rdseed64_step(&r0) == 1) break;
			if(i == 10)error("RDSEED FAILURE");

			for(i = 0; i < 10; ++i)
				if(_rdseed64_step(&r1) == 1) break;
			if(i == 10)error("RDSEED FAILURE");

			v = makeBlock(r0, r1);
#endif
			reseed(&v);
		}
	}
	void reseed(const block* seed, uint64_t id = 0) {
		block v = *seed;
		v ^= makeBlock(0LL, id);
		key = v;
		AES_set_encrypt_key(v, &aes);
		counter = 0;
	}

	void random_data(void *data, int nbytes) {
		random_block((block *)data, nbytes/16);
		if (nbytes % 16 != 0) {
			block extra;
			random_block(&extra, 1);
			memcpy((nbytes/16*16)+(char *) data, &extra, nbytes%16);
		}
	}

	void random_bool(bool * data, int length) {
		uint8_t * uint_data = (uint8_t*)data;
		random_data_unaligned(uint_data, length);
		for(int i = 0; i < length; ++i)
			data[i] = uint_data[i] & 1;
	}

	void random_data_unaligned(void *data, int nbytes) {
		size_t size = nbytes;
		void *aligned_data = data;
		if(std::align(sizeof(block), sizeof(block), aligned_data, size)) {
			int chopped = nbytes - size;
			random_data(aligned_data, nbytes - chopped);
			block tmp[1];
			random_block(tmp, 1);
			memcpy(data, tmp, chopped);
		} else {
			block tmp[2];
			random_block(tmp, 2);
			memcpy(data, tmp, nbytes);
		}
	}

	void random_block(block * data, int nblocks=1) {
		block tmp[AES_BATCH_SIZE];
		for(int i = 0; i < nblocks/AES_BATCH_SIZE; ++i) {
			for (int j = 0; j < AES_BATCH_SIZE; ++j) 
				tmp[j] = makeBlock(0LL, counter++);
			AES_ecb_encrypt_blks<AES_BATCH_SIZE>(tmp, &aes);
			memcpy(data + i*AES_BATCH_SIZE, tmp, AES_BATCH_SIZE*sizeof(block));
		}
		int remain = nblocks % AES_BATCH_SIZE;
		for (int j = 0; j < remain; ++j) 
			tmp[j] = makeBlock(0LL, counter++);
		AES_ecb_encrypt_blks(tmp, remain, &aes);
		memcpy(data + (nblocks/AES_BATCH_SIZE)*AES_BATCH_SIZE, tmp, remain*sizeof(block));
	}
};
}
#endif// PRP_H__
