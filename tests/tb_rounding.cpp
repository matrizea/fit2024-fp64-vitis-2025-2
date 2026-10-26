#include "fp64.hpp"
#include <hls_fpo.h>
#include <cstdint>
#include <cstdio>

// Directed regressions for changes to alignment, normalization, and the exact
// significand product. Input construction below uses integer encodings only;
// AMD's installed FPO bit-accurate model supplies every expected result.
#ifdef HLS_NO_XIL_FPO_LIB
#error "The AMD bit-accurate model is required for this test"
#endif

namespace {
const uint64_t SIGN = UINT64_C(0x8000000000000000);
const uint64_t EXP = UINT64_C(0x7ff0000000000000);
const uint64_t FRAC = UINT64_C(0x000fffffffffffff);
const uint64_t HIDDEN = UINT64_C(0x0010000000000000);
const uint64_t QNAN = UINT64_C(0x7ff8000000000000);
unsigned checks = 0;
unsigned failures = 0;

uint64_t bits(double x) { return repro::bits_of(x).to_uint64(); }
double value(uint64_t x) { return repro::value_of(ap_uint<64>(x)); }
uint64_t canonical_nan(uint64_t x) {
    return (x & EXP) == EXP && (x & FRAC) != 0 ? QNAN : x;
}
uint64_t normal(unsigned exponent, uint64_t fraction) {
    return (uint64_t(exponent) << 52) | (fraction & FRAC);
}

void check(unsigned op, uint64_t a, uint64_t b, const char *phase) {
    const double da = value(a), db = value(b);
    uint64_t want, got;
    if (op == 0) {
        want = bits(HLS_FPO_ADD(da, db));
        got = repro::fp_add_bits(a, b).to_uint64();
    } else if (op == 1) {
        want = bits(HLS_FPO_SUB(da, db));
        got = repro::fp_sub_bits(a, b).to_uint64();
    } else {
        want = bits(HLS_FPO_MUL(da, db));
        got = repro::fp_mul_bits(a, b).to_uint64();
    }
    ++checks;
    // Preserve exact signs of zero. NaN payloads are outside the API contract,
    // but the result must still be quiet if the model expects a NaN.
    const bool signaling = (got & EXP) == EXP && (got & FRAC) != 0 &&
                          (got & UINT64_C(0x0008000000000000)) == 0;
    if (canonical_nan(got) != canonical_nan(want) || signaling) {
        if (failures < 24) {
            const char *names[] = {"add", "sub", "mul"};
            std::printf("FAIL rounding %s %s a=%016llx b=%016llx got=%016llx want=%016llx\n",
                        phase, names[op], (unsigned long long)a,
                        (unsigned long long)b, (unsigned long long)got,
                        (unsigned long long)want);
        }
        ++failures;
    }
}

// Subtraction also exercises a negative small addend. Swapping inputs and
// flipping the first input covers both magnitude orders and result signs.
void add_sub(uint64_t a, uint64_t b, const char *phase) {
    for (unsigned sign = 0; sign != 2; ++sign) {
        const uint64_t signed_a = a ^ (sign ? SIGN : 0);
        check(0, signed_a, b, phase);
        check(1, signed_a, b, phase);
        check(0, b, signed_a, phase);
        check(1, b, signed_a, phase);
    }
}
void mul(uint64_t a, uint64_t b, const char *phase) {
    for (unsigned sign = 0; sign != 2; ++sign) {
        const uint64_t signed_a = a ^ (sign ? SIGN : 0);
        check(2, signed_a, b, phase);
        check(2, b, signed_a, phase);
    }
}
void report_phase(const char *phase, unsigned before) {
    std::printf("  %s: %u checks\n", phase, checks - before);
}
} // namespace

int main() {
    unsigned before = checks;
    const unsigned midpoint_exponents[] = {64, 127, 511, 1023, 1535, 1984, 2045};
    const uint64_t midpoint_fractions[] = {
        0, 1, 2, UINT64_C(0x0007ffffffffffff), FRAC - 1, FRAC
    };
    for (unsigned ei = 0; ei != sizeof(midpoint_exponents) / sizeof(midpoint_exponents[0]); ++ei) {
        const unsigned exponent = midpoint_exponents[ei];
        for (unsigned fi = 0; fi != sizeof(midpoint_fractions) / sizeof(midpoint_fractions[0]); ++fi) {
            const uint64_t a = normal(exponent, midpoint_fractions[fi]);
            // 2^(E-53) is half one ULP of a. Adjacent encodings lie either
            // side of this tie; a's retained low bit is deliberately even/odd.
            const uint64_t half_ulp = normal(exponent - 53, 0);
            for (int delta = -1; delta <= 1; ++delta)
                add_sub(a, uint64_t(int64_t(half_ulp) + delta), "add-midpoint");
        }
    }
    report_phase("add-midpoint", before);

    before = checks;
    const unsigned large_differences[] = {64, 128, 255, 1023, 2045};
    const uint64_t patterns[] = {
        0, 1, 2, FRAC, FRAC - 1, UINT64_C(0x0008000000000000),
        UINT64_C(0x0005555555555555), UINT64_C(0x000aaaaaaaaaaaaa)
    };
    for (unsigned di = 1; di <= 65; ++di) {
        const unsigned difference = di <= 60 ? di : large_differences[di - 61];
        const unsigned ea = difference <= 1022 ? 1023 : 2046;
        const unsigned eb = ea - difference;
        for (unsigned p = 0; p != 16; ++p)
            add_sub(normal(ea, patterns[p % 8]),
                    normal(eb, patterns[(p * 3 + p / 8) % 8]), "alignment");
    }
    report_phase("alignment-1-through-60-and-large", before);

    before = checks;
    const unsigned cancellation_exponents[] = {1, 2, 3, 52, 53, 1023, 2046};
    for (unsigned ei = 0; ei != sizeof(cancellation_exponents) / sizeof(cancellation_exponents[0]); ++ei) {
        const unsigned exponent = cancellation_exponents[ei];
        for (unsigned k = 0; k <= 52; ++k) {
            const uint64_t spacing = UINT64_C(1) << k;
            const uint64_t a = normal(exponent, 0);
            // Equal-exponent cancellation with every possible leading-zero
            // count, plus cancellation across a power-of-two binade boundary.
            add_sub(a, a + spacing, "cancellation");
            add_sub(a, a - spacing, "cancellation");
            add_sub(normal(exponent, FRAC), normal(exponent, FRAC) - spacing,
                    "cancellation");
        }
    }
    report_phase("cancellation", before);

    before = checks;
    for (unsigned i = 0; i <= 51; ++i) {
        const unsigned j = 51 - i;
        // (2^52+2^i)*(2^52+2^j) has exactly bit 51 set below
        // retained bit 52: a true product midpoint with product < 2.
        const uint64_t a = normal(1023, UINT64_C(1) << i);
        const uint64_t b = normal(1023, UINT64_C(1) << j);
        for (int delta = -1; delta <= 1; ++delta) {
            mul(uint64_t(int64_t(a) + delta), b, "mul-midpoint-low");
            // The same significands at the FTZ boundary exercise rounding
            // before the exponent test, using the unmodified AMD oracle.
            mul(normal(1, uint64_t(int64_t(UINT64_C(1) << i) + delta)),
                normal(1022, UINT64_C(1) << j), "mul-midpoint-low-ftz");
        }
    }
    for (unsigned i = 1; i <= 51; ++i) {
        const unsigned j = 52 - i;
        // (2^53-2^i)*(2^53-2^j) has product >= 2 and a true
        // midpoint at discarded bit 52, covering the other normalization path.
        // A coefficient of three makes the retained tie bit odd. Restrict it
        // to i <= 50 so both constructed significands stay in [1, 2).
        for (unsigned coefficient = 1; coefficient <= (i == 51 ? 1u : 3u); coefficient += 2) {
            const uint64_t fa = HIDDEN - coefficient * (UINT64_C(1) << i);
            const uint64_t fb = HIDDEN - (UINT64_C(1) << j);
            const uint64_t a = normal(1023, fa);
            const uint64_t b = normal(1023, fb);
            for (int delta = -1; delta <= 1; ++delta) {
                mul(uint64_t(int64_t(a) + delta), b, "mul-midpoint-high");
                mul(normal(1, uint64_t(int64_t(fa) + delta)),
                    normal(1022, fb), "mul-midpoint-high-ftz");
            }
        }
    }
    report_phase("mul-midpoints-both-normalization-paths", before);

    before = checks;
    for (unsigned k = 0; k <= 51; ++k)
        mul(normal(1023, 1), normal(1023, UINT64_C(1) << k), "mul-isolated-sticky");
    for (unsigned i = 1; i <= 52; ++i)
        for (unsigned j = 1; j <= 52; ++j) {
            // Long low-bit runs force carries across possible DSP partition
            // boundaries. No product or rounding algorithm is used here.
            mul(normal(1023, (UINT64_C(1) << i) - 1),
                normal(1023, (UINT64_C(1) << j) - 1), "mul-partition-carries");
        }
    report_phase("mul-sticky-and-partition-carries", before);

    std::printf("Directed rounding: %u AMD-model checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
