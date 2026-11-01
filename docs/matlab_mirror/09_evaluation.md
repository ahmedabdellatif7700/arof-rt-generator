# Step 9: Evaluation, EVM and BER

How good the received symbols are: EVM and BER on the test symbols. Like step 8, this does not
shape the stream: the samples sent to the KV260 are the same with or without it. On the Pi it is
used for reporting and checking only.

(Agreed for later, see `docs/STATUS.md`: take EVM/BER out of the C++ generator and keep them in
the tools and tests. This note describes the code as it is now.)

## MATLAB

`main_demo.m` step 9 (`core/evaluate_outputs.m`):

```matlab
[evm, ber] = evaluate_outputs(cfg, outs, D, IDX);       % hard decisions, Gray bits
n_bits = numel(ks_test) * log2(M);
fprintf('\nTest symbols: %d (%d bits)\n', numel(ks_test), n_bits);
...
fprintf('  %-34s %8.1f %10.2e\n', names{m}, 100*evm(m), ber(m));
```

## 9a. The rules of `evaluate_outputs.m`

- **Test symbols:** `ks_test = taps + round(train_frac·n_sym) … n_sym` = 10 + 5000 … 50 000,
  **44 991 symbols**; the first 10 % trained the equalizers (step 8) and is not scored.
- **Reference:** every output estimates the symbol at the LMS center tap, `d(ks_test − τ)`,
  `τ = floor(taps/2) = 5`. No EQ is the received symbol `x(ks_test − τ)` itself.
- **EVM** = `sqrt(mean(|y − d|²))`. No further normalization: the constellation has unit
  average power (`qammod(..., 'UnitAveragePower', true)`), and the receiver's gain (step 7) has
  already scaled `x`.
- **BER** = `biterr(qamdemod(y), idx) / (44 991 · 4)`: hard decision to the nearest 16QAM point,
  Gray labels, errors counted in bits.

## 9b. Where the Pi computes it

| Where | What | Definition |
|---|---|---|
| `src/core/evaluate_outputs.*`, in `TransmitReceive::calibrate()` | no EQ on the calibration block's 50 000 symbols | `evaluate_outputs.m` exactly (test symbols, τ) |
| `--calibrate`, streamer log, CONFIG / status JSON (`evm_no_eq`, `ber_no_eq`, `n_test`) | that result, printed as `main_demo.m` prints its "w/o EQ" row | same |
| `--record`, `meta.json` (`evm_no_eq`, `ber_no_eq`) | every recorded symbol | `sqrt(mean|x − d|²)`, all symbols |
| `tools/evaluate_outputs.py` | no EQ and LMS FFE on a recording or a `.mat` | `evaluate_outputs.m` exactly |
| `tools/export_dataset.py`, the `.mat` field `evm_no_eq` | every sample of the dataset | `export_dataset.m`: `sqrt(mean|x − d|² / mean|d|²)` |

The streamed samples are never scored live: the calibration block is the only place the
generator measures EVM (it already has the symbols there, and the block is not streamed).

C++ (`src/core/evaluate_outputs.cpp`):

```cpp
// ks_test - tau, 1-based -> 0-based
const long a = first - tau - 1, b = static_cast<long>(n_sym) - tau - 1;
...
se  += std::norm(s[i].x - s[i].d);
err += __builtin_popcount(qamdemod16(s[i].x) ^ s[i].idx);   // Gray bits
e.evm = std::sqrt(se / n);  e.ber = err / (4.0 * n);
```

`qamdemod16` (`src/core/comm.cpp`) is the nearest point of `qam16_gray()`, the same map as
`qammod(0:15, 16, 'gray', 'UnitAveragePower', true)`.

Python (`tools/evaluate_outputs.py`): the same lines with numpy, `matlab_round` for
`round(train_frac·n_sym)`, the same Gray table; prints the table of `main_demo.m` step 9.

## What differs on the Pi

Nothing in the definition. The difference is what is scored: the calibration block (a fresh
50 000-symbol sequence, as one MATLAB run), not the streamed symbols. For a stream, record it and
run `tools/evaluate_outputs.py`.

## Verified

- `tests/test_transmit_receive.cpp` `reference_point_against_matlab`: 44 991 test symbols per
  seed, mean no-EQ EVM 13.21 % ± 0.3 points (MATLAB 13.21 %).
- `tests/test_tools.py`: a recording and `reference/arof_training_data.mat`, both scored by
  `evaluate_outputs.py`: no EQ 13.28 % vs 13.32 %, LMS 12.98 % vs 12.98 %.
- `tools/compare_matlab.py`: no-EQ EVM and BER from `--calibrate --json` (this step's C++)
  against all 74 points of the study, all within tolerance; with `--lms`, recordings scored by
  `evaluate_outputs.py` (step 8).
- `main_demo.m` (reference/DEMO.md) against a recording made in the container: no EQ 13.3 % /
  1.1e-4 vs 13.3 % / 1.06e-4.
