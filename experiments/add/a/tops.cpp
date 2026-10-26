#include "kernels.hpp"
#include "fp64.hpp"

#ifndef REPRO_ADD_IMPL
#define REPRO_ADD_IMPL fabric
#endif
#ifndef REPRO_MUL_IMPL
#define REPRO_MUL_IMPL maxdsp
#endif
#define REPRO_STRINGIFY_INNER(x) #x
#define REPRO_STRINGIFY(x) REPRO_STRINGIFY_INNER(x)
#define REPRO_PRAGMA(x) _Pragma(REPRO_STRINGIFY(x))
#define REPRO_BINARY_PORTS \
    REPRO_PRAGMA(HLS INTERFACE ap_none port=a) \
    REPRO_PRAGMA(HLS INTERFACE ap_none port=b) \
    REPRO_PRAGMA(HLS INTERFACE ap_ctrl_hs port=return)

double add_native(double a, double b) {
    REPRO_BINARY_PORTS
    return a + b;
}
double sub_native(double a, double b) {
    REPRO_BINARY_PORTS
    return a - b;
}
double mul_native(double a, double b) {
    REPRO_BINARY_PORTS
    return a * b;
}
double add_bind(double a, double b) {
    REPRO_BINARY_PORTS
    double result;
    REPRO_PRAGMA(HLS BIND_OP variable=result op=dadd impl=REPRO_ADD_IMPL latency=3)
    result = a + b;
    return result;
}
double sub_bind(double a, double b) {
    REPRO_BINARY_PORTS
    double result;
    REPRO_PRAGMA(HLS BIND_OP variable=result op=dsub impl=REPRO_ADD_IMPL latency=3)
    result = a - b;
    return result;
}
double mul_bind(double a, double b) {
    REPRO_BINARY_PORTS
    double result;
    REPRO_PRAGMA(HLS BIND_OP variable=result op=dmul impl=REPRO_MUL_IMPL latency=2)
    result = a * b;
    return result;
}
double add_custom(double a, double b) {
    REPRO_BINARY_PORTS
    return repro::fp_add(a, b);
}
double sub_custom(double a, double b) {
    REPRO_BINARY_PORTS
    return repro::fp_sub(a, b);
}
double mul_custom(double a, double b) {
    REPRO_BINARY_PORTS
    return repro::fp_mul(a, b);
}

namespace {
// These separate functions preserve the operator call boundary for Table 3's
// non-inline cases. The corresponding Inline* policies omit this boundary.
double custom_mul_boundary(double a, double b) {
#pragma HLS INLINE off
    return repro::fp_mul(a, b);
}
double custom_sub_boundary(double a, double b) {
#pragma HLS INLINE off
    return repro::fp_sub(a, b);
}

struct NativeMul {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return a * b;
    }
};
struct NativeSub {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return a - b;
    }
};
struct BindMul {
    static double eval(double a, double b) {
#pragma HLS INLINE
        double result;
#pragma HLS BIND_OP variable=result op=dmul impl=fabric latency=2
        result = a * b;
        return result;
    }
};
struct BindSub {
    static double eval(double a, double b) {
#pragma HLS INLINE
        double result;
#pragma HLS BIND_OP variable=result op=dsub impl=fabric latency=3
        result = a - b;
        return result;
    }
};
struct CustomMul {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return custom_mul_boundary(a, b);
    }
};
struct CustomSub {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return custom_sub_boundary(a, b);
    }
};
struct InlineMul {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return repro::fp_mul(a, b);
    }
};
struct InlineSub {
    static double eval(double a, double b) {
#pragma HLS INLINE
        return repro::fp_sub(a, b);
    }
};

template <class Multiply, class Subtract>
void gauss_jordan(double a[10][11]) {
#pragma HLS INLINE
    // Static loop bounds also make the reported latency deterministic.
    pivot_loop: for (int p = 0; p < repro::system_size; ++p) {
#pragma HLS PIPELINE off
        int pivot_row = p;
        double largest = a[p][p] < 0.0 ? -a[p][p] : a[p][p];
        pivot_search: for (int r = 0; r < repro::system_size; ++r) {
#pragma HLS PIPELINE off
            const double magnitude = a[r][p] < 0.0 ? -a[r][p] : a[r][p];
            if (r > p && magnitude > largest) {
                largest = magnitude;
                pivot_row = r;
            }
        }
        // Singular input has no unique solution; the public testbench uses
        // nonsingular systems. Return without dividing by zero if encountered.
        if (largest == 0.0) return;
        swap_columns: for (int c = 0; c < repro::augmented_columns; ++c) {
#pragma HLS PIPELINE off
            const double saved = a[p][c];
            a[p][c] = a[pivot_row][c];
            a[pivot_row][c] = saved;
        }
        const double inverse_pivot = 1.0 / a[p][p];
        normalize_columns: for (int c = 0; c < repro::augmented_columns; ++c) {
#pragma HLS PIPELINE off
            a[p][c] = Multiply::eval(a[p][c], inverse_pivot);
        }
        eliminate_rows: for (int r = 0; r < repro::system_size; ++r) {
#pragma HLS PIPELINE off
            if (r != p) {
                const double factor = a[r][p];
                eliminate_columns: for (int c = 0; c < repro::augmented_columns; ++c) {
#pragma HLS PIPELINE off
                    const double product = Multiply::eval(factor, a[p][c]);
                    a[r][c] = Subtract::eval(a[r][c], product);
                }
            }
        }
    }
}
} // namespace

#define REPRO_GJ_TOP(name, multiply, subtract) \
    void name(double a[10][11]) { \
        REPRO_PRAGMA(HLS INTERFACE ap_memory port=a) \
        REPRO_PRAGMA(HLS INTERFACE ap_ctrl_hs port=return) \
        gauss_jordan<multiply, subtract>(a); \
    }
REPRO_GJ_TOP(gj_native, NativeMul, NativeSub)
REPRO_GJ_TOP(gj_bind_mul, BindMul, NativeSub)
REPRO_GJ_TOP(gj_bind_sub, NativeMul, BindSub)
REPRO_GJ_TOP(gj_bind_both, BindMul, BindSub)
REPRO_GJ_TOP(gj_custom_mul, CustomMul, NativeSub)
REPRO_GJ_TOP(gj_custom_sub, NativeMul, CustomSub)
REPRO_GJ_TOP(gj_custom_both, CustomMul, CustomSub)
REPRO_GJ_TOP(gj_inline_mul, InlineMul, NativeSub)
REPRO_GJ_TOP(gj_inline_sub, NativeMul, InlineSub)
REPRO_GJ_TOP(gj_inline_both, InlineMul, InlineSub)
