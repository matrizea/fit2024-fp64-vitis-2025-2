# Exact binary64 multiplier experiments (2025.2)

The candidates are independent snapshots of `src/` from before the optimization. Only `fp_mul_bits` changes are intended for integration. `prepare.py` recreates the snapshots/configuration files; `check.py` is a standalone two-suite runner, while the repository's expanded `scripts/check_arithmetic.py` also runs the rounding suite.

All HLS reports use ZCU104 (`xczu7ev-ffvc1156-2-e`), 10 ns target, 27% uncertainty, and the same handshake interface as the baseline. Values below are HLS synthesis estimates, not placed-and-routed implementation results.

| Candidate | DSP | FF | LUT | HLS latency / interval | Estimated period (ns) | Decision |
|---|---:|---:|---:|---:|---:|---|
| Baseline | 9 | 124 | 795 | 1 / 2 | 7.296 | Previous implementation |
| `slice` | 9 | 124 | 706 | 1 / 2 | 7.182 | Narrow normalization selection from 106 to 56 bits |
| `direct` | 9 | 81 | 601 | 1 / 2 | 6.417 | Dedicated multiplication rounding, faster alternative |
| `direct_52` | 9 | 81 | 774 | 1 / 2 | 6.893 | 52-bit fractional product plus hidden-bit terms; no DSP saving |
| `direct_or` | 9 | 92 | 594 | 1 / 2 | 7.215 | Reduction intrinsics and combined exponent advance |
| `direct_flags` | 9 | 139 | 683 | 1 / 2 | 5.317 | Unified exception flags; additional LUT/FF |
| `direct_tz` | 9 | 136 | 952 | 1 / 2 | 7.100 | Sticky from trailing-zero counts; no DSP saving |
| `direct_signed` | 9 | 136 | 767 | 1 / 2 | 5.802 | Signed 53-bit product plus exact correction; no DSP saving |
| **`direct_narrow`** | **9** | **80** | **571** | **1 / 2** | **6.704** | **Recommended: dedicated rounding plus bounded exponents** |
| `direct_split` | 8 | 162 | 964 | 1 / 2 | 6.349 | Four exact 26/27-bit partial products; DSP count matches paper but area cost too high |
| Paper, Table 2 | 8 | 123 | 577 | Reported latency 2 | 7.011 (142.63 MHz) | Original unpublished source unavailable |

The recommended candidate reduces LUT by 28.18% and FF by 35.48% from baseline. Its LUT estimate is 1.04% below the paper. DSP remains one higher. HLS top latency remains 1 and transaction interval remains 2; the paper's reported latency definition may differ and is not claimed to match the top latency.

Correctness is unchanged: exact product, round-to-nearest/ties-to-even, normal-precision rounding before flush-to-zero, NaN/Inf handling, and signed zero. The recommended candidate and `direct` pass all four independent suites: native oracle 63,072 checks, AMD ordinary model 63,072, AMD boundary model 85,920, and AMD rounding/carry/sticky model 32,928, for 244,992 checks with zero failures. Logs and source hashes are in `direct_narrow/verification/arithmetic_tests.json` and `build/opt_mul_narrow_arithmetic/`.

The 53-bit retained significand may be incremented to exactly 2^53. In that case the low 52 fraction bits are zero, so a second wide shift/multiplexer is unnecessary. A product already normalized through bit 105 cannot produce a second rounding carry: `(2^53 - 1)^2 < 2^106 - 2^53`, so its retained significand is at most 2^53 - 2 before increment. Thus normalization carry and rounding carry are mutually exclusive, and the exponent advances by their Boolean OR.

## Source archive note

This archive includes the independent candidate source snapshots. Historical experiment runners and raw logs are not included. Use scripts/check_arithmetic.py --source-dir experiments/mul/<candidate> --build-root build/<candidate>_arithmetic --results-dir build/<candidate>_validation to repeat arithmetic validation. The candidate tops.cpp and kernels.hpp preserve the common HLS wrappers. For synthesis, make a separate copy of the archive and replace src/ with the chosen snapshot before running scripts/run_hls.py.
