#ifndef REPRO_KERNELS_HPP
#define REPRO_KERNELS_HPP

// FIT2024 C-003, Table 2: the BIND_OP implementation may be swept using
// -DREPRO_ADD_IMPL=fabric|dsp|fulldsp and
// -DREPRO_MUL_IMPL=fabric|dsp|meddsp|fulldsp|maxdsp.
double add_native(double a, double b);
double sub_native(double a, double b);
double mul_native(double a, double b);
double add_bind(double a, double b);
double sub_bind(double a, double b);
double mul_bind(double a, double b);
double add_custom(double a, double b);
double sub_custom(double a, double b);
double mul_custom(double a, double b);

namespace repro {
const int system_size = 10;
const int augmented_columns = system_size + 1;
}

// The paper does not publish its Gauss-Jordan source. These kernels use the
// same partial-pivoting algorithm, memory interface, and non-pipelined loops.
// Each pivot row is normalized by one native division followed by multiplies.
// Only multiplication and subtraction are replaced. The Table 3 BIND_OP
// variants use fabric (latencies 2 and 3), independent of Table 2 sweep macros.
void gj_native(double a[10][11]);
void gj_bind_mul(double a[10][11]);
void gj_bind_sub(double a[10][11]);
void gj_bind_both(double a[10][11]);
void gj_custom_mul(double a[10][11]);
void gj_custom_sub(double a[10][11]);
void gj_custom_both(double a[10][11]);
void gj_inline_mul(double a[10][11]);
void gj_inline_sub(double a[10][11]);
void gj_inline_both(double a[10][11]);

#endif
