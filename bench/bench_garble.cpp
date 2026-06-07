#include <iostream>

#include "emp-tool/emp-tool.h"

// Use the standard block-wire circuit aliases in this test translation unit.
using namespace emp::block_types;
using namespace std;
using namespace emp;
using namespace emp::legacy;

class AbandonIO: public IOChannel { public:
	void send_data_internal(const void * /*data*/, int64_t /*len*/) override {}
	void recv_data_internal(void * /*data*/, int64_t /*len*/) override {}
};

int port, party;
template <typename T>
void test(T* netio) {
	Bit* key = new Bit[128];
	Bit* msg = new Bit[128];
	Bit* out = new Bit[128];

	PRG prg;
	prg.random_block(reinterpret_cast<block*>(key), 128);
	prg.random_block(reinterpret_cast<block*>(msg), 128);

	AES_Calculator_T<block> aes;
	auto garble_once = [&]() {
		aes.encrypt_with_key(msg, key, out);
	};

	const int N = 1000;
	const int AES_ANDS = 6400;
	if (party == BOB) {
		backend = new HalfGateEva(netio);
		for (int i = 0; i < N; ++i) garble_once();
		delete backend;
		backend = nullptr;
	} else {
		AbandonIO* aio = new AbandonIO();
		backend = new HalfGateGen(aio);

		auto start = clock_start();
		for (int i = 0; i < N; ++i) garble_once();
		double interval = time_from(start);
		cout << "Pure AES garbling speed : " << N * AES_ANDS / interval << " million gate per second\n";
		delete aio;
		delete backend; backend = nullptr;

		backend = new HalfGateGen(netio);

		start = clock_start();
		for (int i = 0; i < N; ++i) garble_once();
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : " << N * AES_ANDS / interval << " million gate per second\n";

		delete backend; backend = nullptr;
	}

	delete[] key;
	delete[] msg;
	delete[] out;
}

int main(int argc, char** argv) {
	parse_party_and_port(argv, &party, &port);
	cout << "Using NetIO\n";
	NetIO* netio = new NetIO(party == ALICE ? nullptr : "127.0.0.1", port);
	test<NetIO>(netio);
	delete netio;
}
