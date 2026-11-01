# Step 8: Equalization (not on the Pi)

In `main_demo.m` step 8 equalizes the received symbols: the LMS FFE and the neural equalizers.
**The Pi does not equalize.** Its output stops at step 7, the received symbols without
equalization; equalizing them is the job of the KV260, which receives them as its input samples
(step 11).

What the repo keeps of step 8 is an offline check: `tools/run_lms.py`, a Python port of
`run_lms.m`, run on a recording after the fact to show that the Pi's data trains an equalizer as
MATLAB's does. It is not part of the generator and not in the Docker image.

## MATLAB

`main_demo.m` step 8:

```matlab
cfg = struct('M', M, 'n_sym', n_sym, 'train_frac', n_train/n_sym, 'taps', lms_taps, ...
    'lms_mu', lms_mu, 'lms_epochs', lms_epochs, 'seed', seed);
tau = floor(cfg.taps / 2);
ks_test = cfg.taps + n_train : n_sym;                   % test symbols, as core/run_lms.m
Y_lms = run_lms(cfg, X, D);                             % LMS FFE (MATLAB)
... run_network(nets{i}, cfg, X, D) ...                 % neural equalizers (equalizer.py)
outs = [{X(ks_test - tau)}, {Y_lms}, Y_nets];           % no EQ: the received symbol itself
```

`lms_taps = 10`, `lms_mu = 1e-3`, `lms_epochs = 20`, `train_frac = 0.1` (5000 symbols).

## 8a. What `run_lms.m` does

- **Input:** a tapped delay line of the 10 latest received symbols, I and Q interleaved
  (`[I(k) Q(k) I(k−1) Q(k−1) …]`, 20 reals).
- **Equalizer:** a 2 × 20 real matrix `W`, two outputs I and Q (widely linear).
- **Training:** online LMS, `W += μ (d − W·x) xᵀ`, 20 passes over the training symbols
  `k = taps … taps + n_train − 1`, target the symbol at the center tap, `d(k − τ)`, `τ = 5`.
- **Test:** `W` fixed, applied to `ks_test = taps + n_train … n_sym` (44 991 symbols).
- **No EQ** is scored on the same symbols: the received symbol `x(k − τ)` itself.

The networks (`run_network`, `equalizer.py`) are not ported: they are the KV260 side's work.

## 8b. The offline check (`tools/run_lms.py`)

The same function line for line: `tdl()` builds the delay line with MATLAB's 1-based indices,
`matlab_round` (halves away from zero) for `round(train_frac·n_sym)`, `W` from zeros, the same
update and epochs. Used by:

- `tools/evaluate_outputs.py` on a recording (or an exported `.mat`): no EQ and LMS FFE, as
  `main_demo.m` steps 8-9;
- `tools/compare_matlab.py --lms`: records 50 000 symbols per point and seed, scores both;
- `tests/test_tools.py`: on a real recording.

## What differs on the Pi

Everything: the Pi has no step 8. The table row exists to show where MATLAB's equalizers go.

## Verified

`tools/compare_matlab.py --lms` (6 seeds of 50 000 recorded symbols against
`compare_results.mat`):

| Point | no EQ Pi | MATLAB | LMS Pi | MATLAB | LMS BER Pi | MATLAB |
|---|---|---|---|---|---|---|
| 300 mV, −11.75 dBm | 13.20 % | 13.21 % | 12.80 % | 12.81 % | 6.5e-06 | 4.6e-06 |
| 300 mV, −15.5 dBm | 17.06 % | 17.07 % | 16.80 % | 16.80 % | 2.9e-03 | 3.0e-03 |
| 400 mV, −11.5 dBm | 17.96 % | 18.00 % | 17.51 % | 17.54 % | 1.2e-04 | 1.6e-04 |

And `main_demo.m` against a recording made in the container (`docs/STATUS.md`): LMS 12.9 % in
both. The data from the Pi gives the equalizer the same gain over no EQ as MATLAB's data, which
is the point of the check.
