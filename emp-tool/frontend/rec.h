#ifndef EMP_FRONTEND_REC_H__
#define EMP_FRONTEND_REC_H__
// Recording aliases for compile(): a circuit-library writer names value types over
// RecordCtx without spelling RecordCtx. `auto c = compile<rec::UInt<32>, rec::UInt<32>>(body);`
#include "emp-tool/circuits/typed.h"
namespace emp { namespace rec {
using Bit = Bit_T<RecordCtx>;
template <int N> using UInt  = UInt_T<RecordCtx, N>;
template <int N> using Int   = Int_T<RecordCtx, N>;
template <int W> using Float = Float_T<RecordCtx, W>;
template <int N> using Bits  = Bits_T<RecordCtx, N>;
}}  // namespace emp::rec
#endif
