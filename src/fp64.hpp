#ifndef REPRO_FP64_HPP
#define REPRO_FP64_HPP

#include <ap_int.h>
#include <stdint.h>

// Independent reconstruction, not the unpublished implementation from FIT C-003.
// Binary64, round-to-nearest/ties-to-even. Subnormal operands and results are
// flushed to signed zero. Results are rounded at normal 53-bit precision before
// the underflow test, as observed in the AMD FPO bit-accurate model; no gradual
// underflow rounding at subnormal spacing is performed.
// No floating-point arithmetic is used by the bit-level operators.
namespace repro {

inline ap_uint<64> bits_of(double value) {
#pragma HLS INLINE
    union { double d; uint64_t u; } v;
    v.d = value;
    return ap_uint<64>(v.u);
}

inline double value_of(ap_uint<64> bits) {
#pragma HLS INLINE
    union { double d; uint64_t u; } v;
    v.u = bits.to_uint64();
    return v.d;
}

namespace detail {

inline ap_uint<64> pack(bool sign, unsigned exponent, ap_uint<52> fraction) {
#pragma HLS INLINE
    ap_uint<64> result = 0;
    result[63] = sign;
    result.range(62, 52) = ap_uint<11>(exponent);
    result.range(51, 0) = fraction;
    return result;
}

inline ap_uint<64> quiet_nan() {
#pragma HLS INLINE
    return ap_uint<64>(UINT64_C(0x7ff8000000000000));
}

// Right shift with a sticky low bit. The n >= W branch also avoids shifts by
// the type width. Using width-bounded operands keeps the aligner finite.
template<int W>
inline ap_uint<W> shift_right_jam(ap_uint<W> value, unsigned n) {
#pragma HLS INLINE
    if (n == 0) return value;
    if (n >= unsigned(W)) return ap_uint<W>(value != 0);
    ap_uint<W> shifted = value >> n;
    const ap_uint<W> discarded = value << (W - n);
    shifted[0] = shifted[0] || (discarded != 0);
    return shifted;
}

// Use the width-aware HLS leading-zero intrinsic. A hand-written sequence of
// full-width shifts builds a second normalization network in addition to the
// final variable shift in fp_add_bits. The zero result is handled by its caller.
inline unsigned leading_zeros_56(ap_uint<56> value) {
#pragma HLS INLINE
    return value.countLeadingZeros();
}

// ext has 53 retained bits plus guard, round and sticky bits, with its normal
// hidden bit at bit 55. exponent uses the biased binary64 exponent convention.
inline ap_uint<64> round_pack(bool sign, int exponent, ap_uint<56> ext) {
#pragma HLS INLINE
    const ap_uint<53> retained = ext.range(55, 3);
    const bool round_up = ext[2] && (ext[1] || ext[0] || retained[0]);
    const ap_uint<54> significand = ap_uint<54>(retained) + ap_uint<1>(round_up);
    if (significand[53]) ++exponent;
    // A rounding carry makes significand exactly 2^53. Its fraction is zero
    // with or without a right shift, so no 52-bit fraction multiplexer is needed.
    // The FPO model first rounds the normalized significand. A rounding carry
    // can promote exponent 0 to min-normal; other tiny results flush to zero.
    if (exponent <= 0) return pack(sign, 0, 0);
    if (exponent >= 2047) return pack(sign, 2047, 0);
    return pack(sign, unsigned(exponent), ap_uint<52>(significand));
}

} // namespace detail

inline ap_uint<64> fp_add_bits(ap_uint<64> a, ap_uint<64> b) {
#pragma HLS INLINE
    bool sign_a = a[63], sign_b = b[63];
    unsigned exp_a = ap_uint<11>(a.range(62, 52)).to_uint();
    unsigned exp_b = ap_uint<11>(b.range(62, 52)).to_uint();
    const ap_uint<52> frac_a = a.range(51, 0);
    const ap_uint<52> frac_b = b.range(51, 0);

    if ((exp_a == 2047 && frac_a != 0) || (exp_b == 2047 && frac_b != 0))
        return detail::quiet_nan();
    if (exp_a == 2047 || exp_b == 2047) {
        if (exp_a == 2047 && exp_b == 2047 && sign_a != sign_b)
            return detail::quiet_nan();
        return detail::pack(exp_a == 2047 ? sign_a : sign_b, 2047, 0);
    }
    if (exp_a == 0 && exp_b == 0) return detail::pack(sign_a && sign_b, 0, 0);
    if (exp_a == 0) return b;
    if (exp_b == 0) return a;

    ap_uint<53> sig_a = (ap_uint<53>(1) << 52) | ap_uint<53>(frac_a);
    ap_uint<53> sig_b = (ap_uint<53>(1) << 52) | ap_uint<53>(frac_b);
    // Larger magnitude first: subtraction can then use an unsigned datapath.
    // The sign-free encoding orders normal magnitudes lexicographically by
    // exponent and fraction, avoiding separate exponent/equality comparisons.
    if (ap_uint<63>(a.range(62, 0)) < ap_uint<63>(b.range(62, 0))) {
        const unsigned temp_exp = exp_a; exp_a = exp_b; exp_b = temp_exp;
        const ap_uint<53> temp_sig = sig_a; sig_a = sig_b; sig_b = temp_sig;
        const bool temp_sign = sign_a; sign_a = sign_b; sign_b = temp_sign;
    }
    const ap_uint<56> ext_a = ap_uint<56>(sig_a) << 3;
    const ap_uint<56> ext_b = detail::shift_right_jam<56>(ap_uint<56>(sig_b) << 3, exp_a - exp_b);
    int exponent = int(exp_a);
    ap_uint<56> ext;
    if (sign_a == sign_b) {
        const ap_uint<57> sum = ap_uint<57>(ext_a) + ap_uint<57>(ext_b);
        if (sum[56]) {
            ext = detail::shift_right_jam<57>(sum, 1);
            ++exponent;
        } else ext = sum;
    } else {
        ext = ext_a - ext_b;
        if (ext == 0) return detail::pack(false, 0, 0);
        const unsigned shift = detail::leading_zeros_56(ext);
        ext <<= shift;
        exponent -= int(shift);
    }
    return detail::round_pack(sign_a, exponent, ext);
}

inline ap_uint<64> fp_sub_bits(ap_uint<64> a, ap_uint<64> b) {
#pragma HLS INLINE
    b[63] = !b[63];
    return fp_add_bits(a, b);
}

inline ap_uint<64> fp_mul_bits(ap_uint<64> a, ap_uint<64> b) {
#pragma HLS INLINE
    const bool sign = a[63] != b[63];
    const ap_uint<11> exp_a = a.range(62, 52);
    const ap_uint<11> exp_b = b.range(62, 52);
    const ap_uint<52> frac_a = a.range(51, 0);
    const ap_uint<52> frac_b = b.range(51, 0);
    if ((exp_a == 2047 && frac_a != 0) || (exp_b == 2047 && frac_b != 0))
        return detail::quiet_nan();
    if (exp_a == 2047 || exp_b == 2047) {
        if (exp_a == 0 || exp_b == 0) return detail::quiet_nan();
        return detail::pack(sign, 2047, 0);
    }
    if (exp_a == 0 || exp_b == 0) return detail::pack(sign, 0, 0);
    const ap_uint<53> sig_a = (ap_uint<53>(1) << 52) | ap_uint<53>(frac_a);
    const ap_uint<53> sig_b = (ap_uint<53>(1) << 52) | ap_uint<53>(frac_b);
    const ap_uint<106> product = sig_a * sig_b;
    // The finite exponent sum is at most 4092; all bias/rounding corrections
    // fit in signed 13 bits. Keep this arithmetic off a general int datapath.
    ap_int<13> exponent = ap_uint<12>(exp_a) + ap_uint<12>(exp_b);
    exponent -= ap_int<13>(1023);
    const bool high = product[105];
    const ap_uint<53> retained = high ? ap_uint<53>(product.range(105, 53))
                                      : ap_uint<53>(product.range(104, 52));
    const bool low_sticky = product.range(50, 0) != 0;
    const bool guard = high ? product[52] : product[51];
    const bool sticky = low_sticky || (high && product[51]);
    const bool increment = guard && (sticky || retained[0]);
    const ap_uint<54> rounded = ap_uint<54>(retained) + ap_uint<1>(increment);
    // If high is set, retained <= 2^53-2, so rounding cannot also carry.
    const ap_uint<1> advance = high || rounded[53];
    exponent += advance;
    // As above, a rounding carry produces a zero fraction; no wide mux/shift.
    const ap_uint<52> fraction = rounded.range(51, 0);
    if (exponent <= 0) return detail::pack(sign, 0, 0);
    if (exponent >= 2047) return detail::pack(sign, 2047, 0);
    return detail::pack(sign, unsigned(exponent), fraction);
}

inline double fp_add(double a, double b) {
#pragma HLS INLINE
    return value_of(fp_add_bits(bits_of(a), bits_of(b)));
}
inline double fp_sub(double a, double b) {
#pragma HLS INLINE
    return value_of(fp_sub_bits(bits_of(a), bits_of(b)));
}
inline double fp_mul(double a, double b) {
#pragma HLS INLINE
    return value_of(fp_mul_bits(bits_of(a), bits_of(b)));
}

} // namespace repro
#endif
