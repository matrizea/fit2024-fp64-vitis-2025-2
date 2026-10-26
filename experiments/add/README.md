# Add/subtract optimization candidates

Each candidate is an isolated source snapshot. The original `src` files were not edited by this experiment. All HLS runs use Vitis HLS 2025.2, xczu7ev-ffvc1156-2-e, 10 ns clock, 27% uncertainty, and the original add_custom top and interfaces. Resources are HLS estimates, not post-route measurements.

Baseline: 2217 LUT, 321 FF, 0 DSP, 2 top latency cycles, 3 interval cycles, 7.094 ns estimated period. Paper add: 1697 LUT, 347 FF, 0 DSP, latency 3.

| Candidate | Description | LUT | FF | Top latency | Interval | Period/ns |
|---|---|---:|---:|---:|---:|---:|
| a | Staged jam aligner; reuse normalized LZC value | 2111 | 437 | 3 | 4 | 6.870 |
| b | a plus explicitly narrowed exponents | 2111 | 437 | 3 | 4 | 6.870 |
| c | b plus a common addition/subtraction datapath | 2161 | 500 | 3 | 4 | 6.870 |
| d | Baseline with explicit OR reductions | 2217 | 321 | 2 | 3 | 7.094 |
| e | c with explicit OR reductions | 2161 | 500 | 3 | 4 | 6.870 |
| f | LZC priority tree with shrinking 32/16/8/4/2-bit selections | 2021 | 321 | 2 | 3 | 6.952 |
| g | f plus mask aligner and carry-only rounder | 2041 | 321 | 2 | 3 | 6.952 |
| h | LZC intrinsic | 1939 | 277 | 2 | 3 | 6.665 |
| i | f plus carry-only rounder | 1928 | 321 | 2 | 3 | 6.952 |
| j | i plus packed 63-bit magnitude comparison | 1898 | 321 | 2 | 3 | 6.952 |
| k | LZC intrinsic plus carry-only rounder plus packed magnitude comparison | 1810 | 206 | 1 | 2 | 6.931 |

Selected candidate: **k**. LUT decreases by 407 (18.36%), from 2217 to 1810. The remaining difference from the paper is +113 LUT (+6.66%). The selected changes are integrated into the final src/fp64.hpp and have passed the final synthesis and RTL checks recorded in results/.

Candidate k passed every existing test suite without modifying the tests: native oracle 63,072 checks; AMD oracle 63,072 checks; AMD boundaries 85,920 checks; directed AMD rounding 32,928 checks. See `k/validation/arithmetic_tests.json` and the logs referenced there. All finite results, infinities and signed zeros are bit-exact; NaN payload/sign comparisons follow the original tests.

The rounder uses a normal 53-bit significand. If incrementing it produces bit 53, its lower 52 bits are already zero; only the exponent needs incrementing. Removing the conditional significand shift therefore preserves rounding carry, overflow, and FTZ boundary behavior. The leading-zero intrinsic is called only after exact-zero subtraction is handled. Packed magnitude comparison is valid for the already classified positive magnitudes of finite normal operands.

Each `build/opt_add_<candidate>/experiment.json` records its source hash, exact commands, test outcomes, and synthesis metrics. The standalone candidate folders contain their own `fp64.hpp`, `tops.cpp` and `kernels.hpp`. `comparison.json` holds the table in machine-readable form.

## Source archive note

This archive includes the independent candidate source snapshots. Historical experiment runners and raw logs are not included. Use scripts/check_arithmetic.py --source-dir experiments/add/<candidate> --build-root build/<candidate>_arithmetic --results-dir build/<candidate>_validation to repeat arithmetic validation. The candidate tops.cpp and kernels.hpp preserve the common HLS wrappers. For synthesis, make a separate copy of the archive and replace src/ with the chosen snapshot before running scripts/run_hls.py.
