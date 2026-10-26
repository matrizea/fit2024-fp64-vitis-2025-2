#include "kernels.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <hls_fpo.h>

#ifdef HLS_NO_XIL_FPO_LIB
#error "RTL operator checks require the AMD FPO model, not its native fallback"
#endif

#define REPRO_STRINGIFY_INNER(x) #x
#define REPRO_STRINGIFY(x) REPRO_STRINGIFY_INNER(x)
#ifndef REPRO_TEST_GJ
#define REPRO_TEST_GJ 0
#endif

namespace {
typedef double (*BinaryTop)(double, double);
typedef void (*MatrixTop)(double [10][11]);

std::uint64_t bits(double x) {
    std::uint64_t value;
    std::memcpy(&value, &x, sizeof value);
    return value;
}

double value(std::uint64_t x) {
    double result;
    std::memcpy(&result, &x, sizeof result);
    return result;
}

bool nan_encoding(std::uint64_t x) {
    return (x & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)
        && (x & UINT64_C(0x000fffffffffffff)) != 0;
}

std::uint64_t random64(std::uint64_t &state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

int check_model_case(const char *name, BinaryTop top, unsigned case_number,
                     std::uint64_t a_bits, std::uint64_t b_bits) {
    const double a = value(a_bits), b = value(b_bits);
    const double expected = name[0] == 'a' ? HLS_FPO_ADD(a, b) :
                            name[0] == 's' ? HLS_FPO_SUB(a, b) : HLS_FPO_MUL(a, b);
    // Call the selected top for every vector, including exceptional operands.
    // During RTL cosimulation this call goes through the generated wrapper.
    const double actual = top(a, b);
    const std::uint64_t want = bits(expected), got = bits(actual);
    if (want == got || (nan_encoding(want) && nan_encoding(got))) return 0;
    std::printf("FAIL %s FPO case=%u a=%016llx b=%016llx got=%016llx expected=%016llx\n",
                name, case_number, (unsigned long long)a_bits, (unsigned long long)b_bits,
                (unsigned long long)got, (unsigned long long)want);
    return 1;
}

int test_custom_model(const char *name, BinaryTop top) {
    // Cross all 32 directed encodings: signed zeros, subnormals, the minimum
    // normal boundary, rounding ties, overflow, infinities, quiet/signaling NaN.
    // In particular min-normal * nextafter(1, 0) distinguishes FPO underflow
    // behavior from native IEEE arithmetic. NaN payload/sign are class-checked.
    const std::uint64_t directed[] = {
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
        UINT64_C(0x4000000000000000), UINT64_C(0x4008000000000000),
        UINT64_C(0x7fdfffffffffffff), UINT64_C(0x7fefffffffffffff),
        UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000000),
        UINT64_C(0xfff8000000000001), UINT64_C(0x7ff0000000000001)
    };
    int errors = 0;
    unsigned checks = 0;
    const unsigned size = unsigned(sizeof directed / sizeof directed[0]);
    for (unsigned i = 0; i < size; ++i)
        for (unsigned j = 0; j < size; ++j)
            errors += check_model_case(name, top, checks++, directed[i], directed[j]);
    std::uint64_t seed = UINT64_C(0x28ad61e7f5930bc4);
    for (unsigned i = 0; i < 128; ++i) {
        const std::uint64_t a = random64(seed), b = random64(seed);
        errors += check_model_case(name, top, checks++, a, b);
    }
    std::printf("%s: %s (%u AMD FPO cases; NaN payload/sign class-checked)\n",
                name, errors ? "FAIL" : "PASS", checks);
    return errors;
}

int test_binary(const char *name, BinaryTop top) {
    // Normal finite inputs and results: the custom datapath's denormal policy
    // is tested independently in tb_fp64.cpp. Include exact cancellation,
    // signs, exponent alignment, overflow, and ties-to-even rounding here.
    const double inputs[][2] = {
        {0.0, 0.0}, {-0.0, -0.0}, {1.0, 1.0}, {1.0, -1.0},
        {-1.0, 1.0}, {-1.0, -1.0}, {1.5, 2.25}, {-1.5, 2.25},
        {3.141592653589793, 0.125}, {1.0, std::ldexp(1.0, -53)},
        {1.0000000000000002, std::ldexp(1.0, -53)},
        {std::ldexp(1.0, 500), std::ldexp(1.0, -400)},
        {std::ldexp(1.9999999999999998, 500), 1.0000000000000002},
        {std::ldexp(1.0, 1023), 2.0},
        {std::ldexp(1.0, -500), std::ldexp(1.0, -400)},
        {-1193046.471111111, 0.0004871961805555555}
    };
    int errors = 0;
    for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        const volatile double a = inputs[i][0], b = inputs[i][1];
        const double expected = name[0] == 'a' ? a + b :
                                name[0] == 's' ? a - b : a * b;
        const double actual = top(a, b);
        if (bits(expected) != bits(actual)) {
            std::printf("FAIL %s case=%u a=%a b=%a got=%a expected=%a\n",
                        name, i, double(a), double(b), actual, expected);
            ++errors;
        }
    }
    std::printf("%s: %s (%u binary cases)\n", name, errors ? "FAIL" : "PASS",
                unsigned(sizeof inputs / sizeof inputs[0]));
    if (std::strstr(name, "_custom") != 0) errors += test_custom_model(name, top);
    return errors;
}

// An independent, long-double CPU solver is the numerical reference.
void reference_solve(const double input[10][11], long double solution[10]) {
    long double a[10][11];
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 11; ++c) a[r][c] = input[r][c];
    for (int p = 0; p < 10; ++p) {
        int pivot = p;
        for (int r = p + 1; r < 10; ++r)
            if (std::fabs(a[r][p]) > std::fabs(a[pivot][p])) pivot = r;
        for (int c = p; c < 11; ++c) {
            long double temp = a[p][c];
            a[p][c] = a[pivot][c]; a[pivot][c] = temp;
        }
        for (int r = p + 1; r < 10; ++r) {
            long double factor = a[r][p] / a[p][p];
            for (int c = p; c < 11; ++c) a[r][c] -= factor * a[p][c];
        }
    }
    for (int r = 9; r >= 0; --r) {
        long double value = a[r][10];
        for (int c = r + 1; c < 10; ++c) value -= a[r][c] * solution[c];
        solution[r] = value / a[r][r];
    }
}

void make_system(int case_number, double a[10][11], double expected[10]) {
    for (int c = 0; c < 10; ++c)
        expected[c] = double((c * 7 + case_number * 3) % 17 - 8) / 4.0;
    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) {
            if (case_number == 0) a[r][c] = r == c ? double(r + 1) : 0.0;
            else a[r][c] = r == c ? 16.0 + r :
                double((r * 13 + c * 7 + case_number * 5) % 9 - 4) / 8.0;
        }
        a[r][10] = 0.0;
        for (int c = 0; c < 10; ++c) a[r][10] += a[r][c] * expected[c];
    }
    // Force nontrivial pivot selection while retaining the same solution.
    if (case_number == 2 || case_number == 3) {
        for (int c = 0; c < 11; ++c) {
            const double temp = a[0][c]; a[0][c] = a[9][c]; a[9][c] = temp;
        }
    }
    // A scaled system verifies that tolerance is relative to the input norm.
    if (case_number == 3)
        for (int r = 0; r < 10; ++r)
            for (int c = 0; c < 11; ++c) a[r][c] *= 1048576.0;
}

int test_matrix(const char *name, MatrixTop top) {
    int errors = 0;
    double worst_relative_residual = 0.0, worst_solution_error = 0.0;
    for (int trial = 0; trial < 4; ++trial) {
        double original[10][11], actual[10][11], known[10];
        long double reference[10] = {};
        make_system(trial, original, known);
        std::memcpy(actual, original, sizeof actual);
        reference_solve(original, reference);
        top(actual);
        double norm_a = 0.0, norm_x = 0.0, norm_b = 0.0, norm_r = 0.0;
        for (int r = 0; r < 10; ++r) {
            const double x = actual[r][10];
            const double error = std::fabs(x - double(reference[r]));
            if (error > worst_solution_error) worst_solution_error = error;
            if (!std::isfinite(x) || error > 1e-10 || std::fabs(x - known[r]) > 1e-10) {
                std::printf("FAIL %s trial=%d x[%d]=%.17g expected=%.17Lg\n",
                            name, trial, r, x, reference[r]);
                ++errors;
            }
            if (std::fabs(x) > norm_x) norm_x = std::fabs(x);
            long double residual = -original[r][10];
            double row_sum = 0.0;
            for (int c = 0; c < 10; ++c) {
                residual += (long double) original[r][c] * actual[c][10];
                row_sum += std::fabs(original[r][c]);
                const double identity_error = std::fabs(actual[r][c] - (r == c ? 1.0 : 0.0));
                if (!std::isfinite(actual[r][c]) || identity_error > 1e-10) ++errors;
            }
            if (row_sum > norm_a) norm_a = row_sum;
            if (std::fabs(original[r][10]) > norm_b) norm_b = std::fabs(original[r][10]);
            if (std::fabs(residual) > norm_r) norm_r = double(std::fabs(residual));
        }
        const double relative_residual = norm_r / (norm_a * norm_x + norm_b);
        if (!std::isfinite(relative_residual) || relative_residual > 1e-12) ++errors;
        if (relative_residual > worst_relative_residual) worst_relative_residual = relative_residual;
    }
    std::printf("%s: %s (4 systems; max solution error=%.3e, max relative residual=%.3e)\n",
                name, errors ? "FAIL" : "PASS", worst_solution_error, worst_relative_residual);
    return errors;
}
} // namespace

int main() {
    int errors = 0;
#ifdef REPRO_TOP
    // RTL cosimulation must call only the selected synthesis top.
#if REPRO_TEST_GJ
    errors += test_matrix(REPRO_STRINGIFY(REPRO_TOP), REPRO_TOP);
#else
    errors += test_binary(REPRO_STRINGIFY(REPRO_TOP), REPRO_TOP);
#endif
#else
#define TEST_BINARY(top) errors += test_binary(#top, top)
    TEST_BINARY(add_native); TEST_BINARY(sub_native); TEST_BINARY(mul_native);
    TEST_BINARY(add_bind); TEST_BINARY(sub_bind); TEST_BINARY(mul_bind);
    TEST_BINARY(add_custom); TEST_BINARY(sub_custom); TEST_BINARY(mul_custom);
#define TEST_MATRIX(top) errors += test_matrix(#top, top)
    TEST_MATRIX(gj_native);
    TEST_MATRIX(gj_bind_mul); TEST_MATRIX(gj_bind_sub); TEST_MATRIX(gj_bind_both);
    TEST_MATRIX(gj_custom_mul); TEST_MATRIX(gj_custom_sub); TEST_MATRIX(gj_custom_both);
    TEST_MATRIX(gj_inline_mul); TEST_MATRIX(gj_inline_sub); TEST_MATRIX(gj_inline_both);
#endif
    std::printf("Kernel tests: %s (errors=%d)\n", errors ? "FAIL" : "PASS", errors);
    return errors ? 1 : 0;
}
