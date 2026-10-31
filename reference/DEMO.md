# Demo: the A-RoF link and its equalizers, step by step

`main_demo.m` follows one 16QAM sequence through the 60-GHz analog radio-over-fiber link of
Liu et al. (JLT 2017), from the transmitted bits to the equalized symbols, and scores every
equalizer. It runs in about a minute:

```
matlab -batch "run('matlab/main_demo.m')"
```

You can also open it in MATLAB and run it section by section (Ctrl+Enter) to inspect each stage.

## Structure

```
matlab/main_demo.m
│
├── Steps 0–7: the link, written out in the demo itself
│     0 settings, 1 transmitter, 2 driver + MZM, 3 fiber,
│     4 photodiode + PD noise, RF amplifier, 5 wireless hop + LNA noise,
│     6 envelope detector + ADC, 7 receiver DSP  →  x  (received symbols)
│
├── Step 8: equalization
│     ├── core/run_lms.m              LMS equalizer, 10 taps (MATLAB)
│     ├── core/equalizer_defs.m       network settings, chosen in eq_keys
│     └── core/run_network.m          one call per network
│           └── equalizer.py          trains and runs the network (Python)
│
├── Step 9: evaluation
│     └── core/evaluate_outputs.m     EVM and BER of every equalizer
│
└── Step 10: figures
      ├── results/demo_link.png         the link stages
      └── results/demo_equalizers.png   constellation of every equalizer
```

The link (steps 1–7) is written out in the demo rather than called from `link/`. It gives the same
received symbols as `link/transmit_receive.m`, the chain every study uses, bit for bit.

The equalizers and the evaluation (steps 8–9) call the study's modules, in the same way as
`main_compare.m`. "No EQ" goes through the same evaluation, so all rows are comparable.

## Requirements

- MATLAB with the Communications and Signal Processing Toolboxes.
- Python 3 with numpy and scipy, reachable through MATLAB's `pyenv`, for the neural networks.
- The `core/` folder and `equalizer.py`. `link/` is not needed.

## Settings (step 0)

| Setting | Default | Meaning |
|---|---|---|
| `vpp_mv`, `rop_dbm` | 300 mV, -11.75 dBm | drive and received optical power |
| `seed`, `n_sym` | 2017, 50 000 | sequence; the first 10 % train the equalizers |
| `eq_keys` | `liu`, `own_tanh_6`, `own_multilevel_10` | networks, by key in `core/equalizer_defs.m` |
| `lms_taps`, `lms_mu`, `lms_epochs` | 10, 1e-3, 20 | LMS settings, as in the study |

The impairments of the impairment study are switches in the same section, all off:

- MZM bias drift;
- laser RIN;
- PD saturation;
- PA memory;
- RF response ripple;
- ADC clipping.

The comments give an example value for each. Fiber length is a plain parameter.

## Example result

At 300 mV and -11.75 dBm, one sequence, 44 991 test symbols:

| Equalizer | EVM | BER |
|---|---|---|
| No EQ | 13.3 % | 1.1e-4 |
| LMS FFE | 12.9 % | 1.1e-5 |
| Liu ANN (replicated) | 15.3 % | 6.1e-5 |
| Own 6-12-2, tanh | 11.3 % | 5.6e-6 |
| Own 10-12-2, multilevel | 6.6 % | 1.1e-5 |

These numbers come from a single sequence, so they vary from one seed to the next. The reference
results are the study's averages over 6 sequences (`main_compare.m`, `main_impairments.m`). For
example, the Liu replica averages 12.9 % EVM at this point in the study.

## Note on `equalizer.py`

MATLAB drops trailing singleton dimensions, so the demo's single sequence reaches Python as a 2-D
array instead of `samples x runs x conditions`. `equalizer.py` restores the missing dimensions.
The study always passes 3-D arrays, for which this does nothing. On the same 3-D inputs, the
outputs before and after this change were checked to be identical for the tanh, multilevel and
Liu networks.
