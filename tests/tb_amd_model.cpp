#include "fp64.hpp"
#include <hls_fpo.h>
#include <cstdint>
#include <cstdio>

// Independent oracle: AMD's installed PG060 v7.1 bit-accurate C model.
// Do not define HLS_NO_XIL_FPO_LIB: that would silently replace this oracle
// with native floating-point arithmetic. NaN signs/payloads are intentionally
// ignored, while every finite value, infinity, and signed zero is bit-exact.
#ifdef HLS_NO_XIL_FPO_LIB
#error "The AMD bit-accurate model is required for this test"
#endif

namespace {
const uint64_t SIGN = UINT64_C(0x8000000000000000);
const uint64_t EXP = UINT64_C(0x7ff0000000000000);
const uint64_t FRAC = UINT64_C(0x000fffffffffffff);
const uint64_t QNAN = UINT64_C(0x7ff8000000000000);
unsigned failures = 0;
unsigned checks = 0;
unsigned nan_checks = 0;
unsigned zero_checks = 0;

uint64_t bits(double x) { return repro::bits_of(x).to_uint64(); }
double value(uint64_t x) { return repro::value_of(ap_uint<64>(x)); }
bool is_nan(uint64_t x) { return (x & EXP) == EXP && (x & FRAC) != 0; }
uint64_t canonical_nan(uint64_t x) { return is_nan(x) ? QNAN : x; }

uint64_t oracle(unsigned op, uint64_t a, uint64_t b) {
    // Supply the original encodings: input flushing is the model's job.
    const double da = value(a), db = value(b);
    double result;
    if (op == 0) result = HLS_FPO_ADD(da, db);
    else if (op == 1) result = HLS_FPO_SUB(da, db);
    else result = HLS_FPO_MUL(da, db);
    return bits(result);
}

uint64_t actual(unsigned op, uint64_t a, uint64_t b) {
    if (op == 0) return repro::fp_add_bits(a, b).to_uint64();
    if (op == 1) return repro::fp_sub_bits(a, b).to_uint64();
    return repro::fp_mul_bits(a, b).to_uint64();
}

void check(uint64_t a, uint64_t b, const char *phase) {
    const char *names[] = {"add", "sub", "mul"};
    for (unsigned op = 0; op != 3; ++op) {
        const uint64_t want = oracle(op, a, b);
        const uint64_t got = actual(op, a, b);
        ++checks;
        if (is_nan(want)) ++nan_checks;
        if ((want & ~SIGN) == 0) ++zero_checks;
        const bool signaling_result = is_nan(got) &&
            (got & UINT64_C(0x0008000000000000)) == 0;
        if (canonical_nan(got) != canonical_nan(want) || signaling_result) {
            if (failures < 24)
                std::printf("FAIL AMD %s %s a=%016llx b=%016llx got=%016llx want=%016llx\n",
                            phase, names[op], (unsigned long long)a,
                            (unsigned long long)b, (unsigned long long)got,
                            (unsigned long long)want);
            ++failures;
        }
    }
}

uint64_t random64(uint64_t &state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}
} // namespace

int main() {
    const uint64_t directed[] = {
        UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000),
        UINT64_C(0x0000000000000001), UINT64_C(0x8000000000000001),
        UINT64_C(0x000fffffffffffff), UINT64_C(0x800fffffffffffff),
        UINT64_C(0x0010000000000000), UINT64_C(0x8010000000000000),
        UINT64_C(0x0010000000000001), UINT64_C(0x001ffffffffffffe),
        UINT64_C(0x001fffffffffffff), UINT64_C(0x0020000000000000),
        UINT64_C(0x0020000000000001), UINT64_C(0x3c90000000000000),
        UINT64_C(0x3ca0000000000000), UINT64_C(0x3ca0000000000001),
        UINT64_C(0x3fd0000000000000), UINT64_C(0x3fdfffffffffffff),
        UINT64_C(0x3fe0000000000000), UINT64_C(0x3fe0000000000001),
        UINT64_C(0x3feffffffffffffe), UINT64_C(0x3fefffffffffffff),
        UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000),
        UINT64_C(0x3ff0000000000001), UINT64_C(0x3fffffffffffffff),
        UINT64_C(0x4000000000000000), UINT64_C(0x4008000000000000),
        UINT64_C(0x7fdfffffffffffff), UINT64_C(0x7fefffffffffffff),
        UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000000),
        UINT64_C(0xfff8000000000001), UINT64_C(0x7ff0000000000001)
    };
    const unsigned count = sizeof(directed) / sizeof(directed[0]);
    for (unsigned i = 0; i != count; ++i)
        for (unsigned j = 0; j != count; ++j) {
            check(directed[i], directed[j], "directed");
            check(directed[i] ^ SIGN, directed[j], "directed-sign");
        }

    // Exact products around the half-ULP boundary below min-normal. These
    // distinguish rounding before FTZ from flushing an unrounded exact result.
    for (unsigned i = 0; i != 32; ++i)
        for (unsigned j = 0; j != 32; ++j) {
            const uint64_t a = UINT64_C(0x0010000000000000) + i;
            const uint64_t b = UINT64_C(0x3ff0000000000000) - j;
            check(a, b, "min-normal-rounding");
            check(a ^ SIGN, b, "min-normal-rounding-sign");
        }

    uint64_t state = UINT64_C(0x9c6b71a3df2458e0);
    for (unsigned i = 0; i != 16000; ++i) {
        const uint64_t ra = random64(state), rb = random64(state);
        check(ra, rb, "random-encoding");
        if (i < 4000) {
            // Force products close to the normal/subnormal threshold, with
            // independently random significands and signs. Both inputs normal.
            const unsigned ea = 1 + i % 1019;
            const unsigned eb = 1021 + i % 5 - ea;
            const uint64_t a = (ra & (SIGN | FRAC)) | (uint64_t(ea) << 52);
            const uint64_t b = (rb & (SIGN | FRAC)) | (uint64_t(eb) << 52);
            check(a, b, "underflow-product");
            // Close cancellation at low exponents stresses signed FTZ output.
            const uint64_t c = (ra & (SIGN | FRAC)) | (uint64_t(1 + i % 6) << 52);
            const uint64_t d = (c ^ SIGN) + (i % 31);
            check(c, d, "underflow-cancellation");
        }
    }

    std::printf("AMD model: %u checks, %u NaN comparisons, %u signed-zero comparisons, %u failures\n",
                checks, nan_checks, zero_checks, failures);
    return failures ? 1 : 0;
}
