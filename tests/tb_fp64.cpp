#include "fp64.hpp"
#include <cfenv>
#include <cstdio>
#include <cstdint>
#ifdef REPRO_USE_FPO_GOLDEN
#include <hls_fpo.h>
#endif

// Self-contained bit-level arithmetic test. tb_kernels.cpp exercises the HLS
// top-level wrappers separately for C/RTL co-simulation.

namespace {
const uint64_t SIGN = UINT64_C(0x8000000000000000);
const uint64_t EXP = UINT64_C(0x7ff0000000000000);
const uint64_t FRAC = UINT64_C(0x000fffffffffffff);
const uint64_t QNAN = UINT64_C(0x7ff8000000000000);

uint64_t bits(double x) { return repro::bits_of(x).to_uint64(); }
double value(uint64_t x) { return repro::value_of(ap_uint<64>(x)); }
bool is_nan(uint64_t x) { return (x & EXP) == EXP && (x & FRAC); }
uint64_t ftz(uint64_t x) { return (x & EXP) == 0 ? x & SIGN : x; }
uint64_t canonical(uint64_t x) { return is_nan(x) ? QNAN : ftz(x); }

// Volatile operands and result ensure separate binary64 operations. The test
// must be built without fast-math and runs under FE_TONEAREST.
uint64_t oracle(unsigned op, uint64_t ua, uint64_t ub) {
    volatile double a = value(ftz(ua));
    volatile double b = value(ftz(ub));
    volatile double z;
#ifdef REPRO_USE_FPO_GOLDEN
    if (op == 0) z = HLS_FPO_ADD(a, b);
    else if (op == 1) z = HLS_FPO_SUB(a, b);
    else z = HLS_FPO_MUL(a, b);
#else
    if (op == 0) z = a + b;
    else if (op == 1) z = a - b;
    else z = a * b;
    // Native IEEE arithmetic rounds at subnormal spacing. AMD FPO rounds the
    // normalized significand at 53 bits before its FTZ test. Correct the narrow
    // interval where only the native result rounds upward to min-normal, using
    // an independent exact-product comparison against the normal-precision tie.
    if (op == 2 && (bits(z) & ~SIGN) == UINT64_C(0x0010000000000000)) {
        const uint64_t ua_ftz = ftz(ua), ub_ftz = ftz(ub);
        const unsigned ea = unsigned((ua_ftz & EXP) >> 52);
        const unsigned eb = unsigned((ub_ftz & EXP) >> 52);
        const ap_uint<53> sa = ap_uint<53>((ua_ftz & FRAC) | (UINT64_C(1) << 52));
        const ap_uint<53> sb = ap_uint<53>((ub_ftz & FRAC) | (UINT64_C(1) << 52));
        const ap_uint<106> product = sa * sb;
        const unsigned sum_exp = ea + eb;
        const ap_uint<106> normal_rounding_tie =
            (ap_uint<106>(1) << 105) - (ap_uint<106>(1) << 51);
        if (sum_exp < 1023 || (sum_exp == 1023 && product < normal_rounding_tie))
            return (ua_ftz ^ ub_ftz) & SIGN;
    }
#endif
    return canonical(bits(z));
}

uint64_t actual(unsigned op, uint64_t a, uint64_t b) {
    if (op == 0) return repro::fp_add_bits(a, b).to_uint64();
    if (op == 1) return repro::fp_sub_bits(a, b).to_uint64();
    return repro::fp_mul_bits(a, b).to_uint64();
}

uint64_t random64(uint64_t &s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

unsigned failures = 0;
unsigned checks = 0;
void check(uint64_t a, uint64_t b) {
    const char *names[] = {"add", "sub", "mul"};
    for (unsigned op = 0; op != 3; ++op) {
        const uint64_t want = oracle(op, a, b);
        const uint64_t got = actual(op, a, b);
        ++checks;
        if (got != want) {
            if (failures < 20)
                std::printf("FAIL %s a=%016llx b=%016llx got=%016llx want=%016llx\n",
                            names[op], (unsigned long long)a, (unsigned long long)b,
                            (unsigned long long)got, (unsigned long long)want);
            ++failures;
        }
    }
}
} // namespace

int main() {
    if (std::fesetround(FE_TONEAREST) != 0) {
        std::puts("Unable to select FE_TONEAREST");
        return 1;
    }
    const uint64_t cases[] = {
        UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000),
        UINT64_C(0x0000000000000001), UINT64_C(0x8000000000000001),
        UINT64_C(0x000fffffffffffff), UINT64_C(0x800fffffffffffff),
        UINT64_C(0x0010000000000000), UINT64_C(0x8010000000000000),
        UINT64_C(0x0010000000000001), UINT64_C(0x001fffffffffffff),
        UINT64_C(0x0020000000000000), UINT64_C(0x3c90000000000000),
        UINT64_C(0x3ca0000000000000), UINT64_C(0x3ca0000000000001),
        UINT64_C(0x3fd0000000000000), UINT64_C(0x3fdfffffffffffff),
        UINT64_C(0x3fe0000000000000), UINT64_C(0x3fefffffffffffff),
        UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000),
        UINT64_C(0x3ff0000000000001), UINT64_C(0x3fffffffffffffff),
        UINT64_C(0x4000000000000000), UINT64_C(0x3feffffffffffffe),
        UINT64_C(0x7fdfffffffffffff), UINT64_C(0x7fefffffffffffff),
        UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000000),
        UINT64_C(0xfff8000000000001), UINT64_C(0x7ff0000000000001)
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        for (unsigned j = 0; j < sizeof(cases) / sizeof(cases[0]); ++j)
            check(cases[i], cases[j]);

    uint64_t seed = UINT64_C(0x7b2c9e4136d0a5f8);
    for (unsigned i = 0; i != 12000; ++i) {
        uint64_t a = random64(seed), b = random64(seed);
        check(a, b); // All encodings, including exceptional operands.
        if (i < 4000) {
            // Near cancellation / adjacent significands at equal exponent.
            a = (a & (SIGN | FRAC)) | ((uint64_t(1 + i % 2046)) << 52);
            b = ((a ^ SIGN) & ~FRAC) | (random64(seed) & FRAC);
            check(a, b);
            // Adjacent exponents stress sticky alignment and rounding ties.
            b = (b & (SIGN | FRAC)) | ((uint64_t(1 + (i + 1) % 2046)) << 52);
            check(a, b);
        }
    }
    // Explicit wrapper smoke checks catch bit conversion/sign errors.
    if (bits(repro::fp_add(1.0, 2.0)) != bits(3.0) ||
        bits(repro::fp_sub(1.0, 2.0)) != bits(-1.0) ||
        bits(repro::fp_mul(-2.0, 3.0)) != bits(-6.0)) ++failures;
#ifdef REPRO_USE_FPO_GOLDEN
    const char *golden = "AMD FPO model";
#else
    const char *golden = "native RNE + FPO FTZ";
#endif
    std::printf("FP64: %u bit-exact checks, %u failures (%s, canonical NaN)\n",
                checks, failures, golden);
    return failures ? 1 : 0;
}
