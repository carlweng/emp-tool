#include <typeinfo>
#include "emp-tool/emp-tool.h"
#include <iostream>
#include <cmath>
#include <vector>

// Use the standard block-wire circuit aliases in this test translation unit.
using namespace emp::block_types;
using namespace std;
using namespace emp;

template<typename F>
void print_bits(const F& a) {
	for(int i = (int)a.size() - 1; i >= 0; i--)
		printf("%d", a[i].template reveal<bool>());
	cout << endl;
}

void print_float(float a) {
	unsigned char* c = (unsigned char*)&a;
	for(int i = 0; i < 4; i++)
		printf("%X", c[i]);
	cout << endl;
}

bool equal(Float a, float b) {
	unsigned char pa[sizeof(float)];
	memset(pa, 0, sizeof(float));
	unsigned char *pb = (unsigned char*)(&b);
	for(int i = 0; i < (int)sizeof(float); i++) {
		for(int j = 0; j < 8; j++) {
			pa[i] += (a[i*8+j].reveal<bool>())<<j;
		}
	}
	if(memcmp(pa, pb, sizeof(float)) == 0)
		return true;
	else return false;
}

bool accurate(double a, double b, double err) {
	if (fabs(a - b) < fabs(err*a) and fabs(a - b) < fabs(err*b))
		return true;
	else return false;
}

// Bit-exact accuracy harness for the binary fp32 operators (+ - * /). Each runs
// through the on-disk fp32 circuit and must match host float arithmetic exactly.
template<typename Op, typename Op2>
bool test_float(double precision, int runs = 1000) {
	PRG prg;
	int bad = 0;
	for(int i = 0; i < runs; ++i) {
		int ia = 0, ib = 0;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		float da = (float)(ia) / 10000000.0;
		float db = (float)(ib) / 10000000.0;

		Float a(da, PUBLIC);
		Float b(db, PUBLIC);
		Float res = Op2()(a,b);

		if(precision > 0.0) {
			if (not accurate(res.reveal<double>(PUBLIC), Op()(da,db), precision)) {
				cout << "Inaccuracy:\t"<<typeid(Op2).name()<<"\t"<< da <<"\t"<<db<<"\t"<<Op()(da,db)<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
				++bad;
			}
		} else {
			if (not equal(res, Op()(da,db))) {
				cout << "Inaccuracy:\t"<<typeid(Op2).name()<<"\t"<< da <<"\t"<<db<<"\t"<<Op()(da,db)<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
				++bad;
			}
		}
	}
	cout << typeid(Op2).name()<<"\t\t\tDONE  -  "
	     << (bad ? "FAIL" : "ok") << endl;
	return bad == 0;
}

// Width-generic wiring smoke test: each op packs inputs, runs the matching
// fp<W> circuit, and unpacks. The circuits themselves are bit-exact verified
// elsewhere; here we confirm the class plumbing for Float16/Float32/Float64 on
// a handful of values exactly representable in all three.
template<typename F>
bool width_smoke(const char* name) {
	int bad = 0;
	auto chk = [&](const char* lbl, double got, double want) {
		if (std::fabs(got - want) > 1e-9 * std::max(1.0, std::fabs(want))) {
			cout << "  [" << name << "] " << lbl << " got " << got << " want " << want << endl;
			++bad;
		}
	};
	auto chkb = [&](const char* lbl, bool got, bool want) {
		if (got != want) { cout << "  [" << name << "] " << lbl << " got " << got << " want " << want << endl; ++bad; }
	};
	F a(2.0, PUBLIC), b(3.0, PUBLIC), nine(9.0, PUBLIC), z(0.0, PUBLIC);
	chk("add",   (a + b).template reveal<double>(PUBLIC),  5.0);
	chk("sub",   (a - b).template reveal<double>(PUBLIC), -1.0);
	chk("mul",   (a * b).template reveal<double>(PUBLIC),  6.0);
	chk("div",   (b / a).template reveal<double>(PUBLIC),  1.5);
	chk("sqr",   a.sqr().template reveal<double>(PUBLIC),  4.0);
	chk("sqrt",  nine.sqrt().template reveal<double>(PUBLIC), 3.0);
	chk("recip", a.recip().template reveal<double>(PUBLIC), 0.5);
	chk("min",   a.min(b).template reveal<double>(PUBLIC), 2.0);
	chk("max",   a.max(b).template reveal<double>(PUBLIC), 3.0);
	chk("fma",   a.fma(b, a).template reveal<double>(PUBLIC), 8.0);   // 2*3 + 2
	chk("neg",   (-a).template reveal<double>(PUBLIC), -2.0);
	chk("copysign", a.copysign(-b).template reveal<double>(PUBLIC), -2.0);
	chkb("lt",     a.less_than(b).template reveal<bool>(PUBLIC), true);
	chkb("eq",     a.equal(a).template reveal<bool>(PUBLIC), true);
	chkb("ne",     a.not_equal(b).template reveal<bool>(PUBLIC), true);
	chkb("ge",     b.greater_equal(a).template reveal<bool>(PUBLIC), true);
	chkb("iszero", z.is_zero().template reveal<bool>(PUBLIC), true);
	chkb("isnan",  a.is_nan().template reveal<bool>(PUBLIC), false);
	cout << name << " smoke\t\t\tDONE  -  " << (bad ? "FAIL" : "ok") << endl;
	return bad == 0;
}

void scratch_pad(double num) {
	cout << "input: " << num << endl;
	Float x(num, PUBLIC);

	cout << "ultimate: ";
	for(int i = x.size()-1; i >= 0; i--) {
		cout << x.value[i].reveal<bool>(PUBLIC);
	}
	cout << endl;

	cout << "test reveal: ";
	cout << x.reveal<string>() << " or " << x.reveal<double>() << endl << endl;
}

void fp_cmp(double a, double b) {
	cout << "compare (eq, le, lt): " << a << " " << b << " - ";
	Float x(a, PUBLIC);
	Float y(b, PUBLIC);

	Bit z = x.equal(y);
	cout << z.reveal<bool>() << " ";
	z = x.less_equal(y);
	cout << z.reveal<bool>() << " ";
	z = x.less_than(y);
	cout << z.reveal<bool>() << endl;
}

void fp_if(double a, double b) {
	cout << "if true/false: " << a << " " << b << " - ";
	Float x(a, PUBLIC);
	Float y(b, PUBLIC);
	Bit one = Bit(true, PUBLIC);
	Bit zero = Bit(false, PUBLIC);

	Float z = If(one).Then(y).Else(x);
	cout << z.reveal<string>() << " ";
	z = If(zero).Then(y).Else(x);
	cout << z.reveal<string>() << endl<<endl;
	swap(Bit(true, PUBLIC), x, y);
	cout << x.reveal<string>() << " "<<y.reveal<string>();
}

void fp_abs(double a) {
	cout << "abs: " << a << " - ";
	Float x(a, PUBLIC);

	Float z = x.abs();
	cout << z.reveal<string>() << endl;
}

// Round-trip int → float → int at the given scale with random magnitudes
// that fit in 24 bits (Float's significand precision). Bit-exact.
bool test_float_int_roundtrip_small(size_t scale, int runs = 1000) {
	PRG prg;
	int rate_cnt = 0;
	for (int i = 0; i < runs; ++i) {
		int64_t raw;
		prg.random_data_unaligned(&raw, sizeof(raw));
		// Sign-extend the top 24 bits to int64 → range [-2^23, 2^23).
		int64_t v = raw >> 40;

		// signed
		SignedInt_T<block, 64> si(v, PUBLIC);
		Float f1 = si.to_float(scale);
		int64_t back_s = f1.to_signed<64>(scale).reveal<int64_t>(PUBLIC);
		if (back_s != v) {
			cout << "Inaccuracy (signed s=" << scale << "): "
			     << v << " -> " << back_s << endl;
			rate_cnt++;
		}

		// unsigned: feed |v|
		uint64_t uv = (uint64_t)(v < 0 ? -v : v);
		UnsignedInt_T<block, 64> ui(uv, PUBLIC);
		Float f2 = ui.to_float(scale);
		uint64_t back_u = f2.to_unsigned<64>(scale).reveal<uint64_t>(PUBLIC);
		if (back_u != uv) {
			cout << "Inaccuracy (unsigned s=" << scale << "): "
			     << uv << " -> " << back_u << endl;
			rate_cnt++;
		}
	}
	cout << "test_float_int_roundtrip_small s=" << scale
	     << "\t\tDONE  -  accuracy : "
	     << (1.0 - (float)rate_cnt / (2 * runs)) << endl;
	return rate_cnt == 0;
}

// Larger magnitudes (up to ~52 bits) where the float's 24-bit significand
// truncates low bits. Check the round-trip is within float precision.
bool test_float_int_roundtrip_lossy(size_t scale, int runs = 200) {
	PRG prg;
	int rate_cnt = 0;
	for (int i = 0; i < runs; ++i) {
		int64_t raw;
		prg.random_data_unaligned(&raw, sizeof(raw));
		int64_t v = raw >> 12;     // 52-bit signed range

		SignedInt_T<block, 64> si(v, PUBLIC);
		Float f1 = si.to_float(scale);
		int64_t back = f1.to_signed<64>(scale).reveal<int64_t>(PUBLIC);

		// Float keeps 24 significant bits; allow up to 2^(53-24) = 2^29
		// of relative error in the worst case, which is the trailing
		// bits the float can't represent.
		int64_t diff = back - v;
		double rel = std::fabs((double)diff / (v == 0 ? 1.0 : (double)v));
		if (rel > 1e-6) {
			cout << "Inaccuracy (lossy s=" << scale << "): "
			     << v << " -> " << back << " (rel " << rel << ")" << endl;
			rate_cnt++;
		}
	}
	cout << "test_float_int_roundtrip_lossy s=" << scale
	     << "\t\tDONE  -  accuracy : "
	     << (1.0 - (float)rate_cnt / runs) << endl;
	return rate_cnt == 0;
}

bool test_float_int_edges() {
	cout << "Edge cases:" << endl;
	int bad = 0;
	auto check = [&](const char* label, double got, double want) {
		cout << label << got << " (expect " << want << ")" << endl;
		if (got != want) {
			cout << "  mismatch: got " << got << " want " << want << endl;
			++bad;
		}
	};

	// Zero in both directions.
	SignedInt_T<block, 32> z_int(0, PUBLIC);
	double zf = z_int.to_float(0).reveal<double>(PUBLIC);
	check("  int 0 -> float : ", zf, 0.0);

	Float zfloat(0.0f, PUBLIC);
	int32_t zb = zfloat.to_signed<32>(0).reveal<int32_t>(PUBLIC);
	check("  float 0.0 -> int : ", (double)zb, 0.0);

	// Sign-flip
	SignedInt_T<block, 32> sp(7, PUBLIC), sn(-7, PUBLIC);
	check("   7 -> float : ", sp.to_float(0).reveal<double>(PUBLIC), 7.0);
	check("  -7 -> float : ", sn.to_float(0).reveal<double>(PUBLIC), -7.0);

	// Cross-check known values.
	Float onehalf(1.5f, PUBLIC);
	check("  1.5  (s=1) -> int : ",
	      (double)onehalf.to_signed<32>(1).reveal<int32_t>(PUBLIC), 3.0);
	Float quarter(0.25f, PUBLIC);
	check("  0.25 (s=4) -> int : ",
	      (double)quarter.to_signed<32>(4).reveal<int32_t>(PUBLIC), 4.0);
	Float negthree(-3.0f, PUBLIC);
	check(" -3.0 (s=0) -> int : ",
	      (double)negthree.to_signed<32>(0).reveal<int32_t>(PUBLIC), -3.0);

	// Verify SignedInt::to_float matches -(unsigned magnitude):
	SignedInt_T<block, 32> sp42(42, PUBLIC), sn42(-42, PUBLIC);
	float pf = sp42.to_float(0).reveal<float>(PUBLIC);
	float nf = sn42.to_float(0).reveal<float>(PUBLIC);
	cout << "  42 = " << pf << ",  -42 = " << nf
	     << " (expect 42 and -42)" << endl;
	if (pf != 42.0f || nf != -42.0f) ++bad;

	// Width-generic int -> float for the other widths.
	SignedInt_T<block, 64> big(123456789, PUBLIC);
	check("  123456789 -> fp64 : ",
	      big.to_float<64>(0).reveal<double>(PUBLIC), 123456789.0);
	SignedInt_T<block, 32> hundred(-100, PUBLIC);
	check("  -100 -> fp16 : ", hundred.to_float<16>(0).reveal<double>(PUBLIC), -100.0);
	return bad == 0;
}

int main(int argc, char** argv) {
	setup_clear_backend();
	int bad = 0;

	cout << "Test function:" << endl;
	fp_cmp(52.21875, 52.21875);
	fp_cmp(24.4332565, 52.21875);
	fp_if(24.4332565, 52.21875);
	fp_abs(-24.422432);
	fp_abs(24.422432);

	cout << endl << "Test fp32 accuracy (bit-exact through the on-disk circuits):" << endl;
	bad += !test_float<std::plus<float>, std::plus<Float>>(0.0);
	bad += !test_float<std::minus<float>, std::minus<Float>>(0.0);
	bad += !test_float<std::multiplies<float>, std::multiplies<Float>>(0.0);
	bad += !test_float<std::divides<float>, std::divides<Float>>(0.0);

	cout << endl << "Test Float16 / Float32 / Float64 wiring:" << endl;
	bad += !width_smoke<Float16>("fp16");
	bad += !width_smoke<Float32>("fp32");
	bad += !width_smoke<Float64>("fp64");

	cout << endl << "Test float <-> int conversion:" << endl;
	bad += !test_float_int_edges();
	bad += !test_float_int_roundtrip_small(0);
	bad += !test_float_int_roundtrip_small(1);
	bad += !test_float_int_roundtrip_small(10);
	bad += !test_float_int_roundtrip_small(20);
	bad += !test_float_int_roundtrip_lossy(0);
	bad += !test_float_int_roundtrip_lossy(10);

	finalize_clear_backend();
	return bad == 0 ? 0 : 1;
}
