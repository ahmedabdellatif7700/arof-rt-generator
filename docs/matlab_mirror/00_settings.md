# Step 0: Settings

## MATLAB

- `main_demo.m` section 0: the operating point `vpp_mv = 300`, `rop_dbm = -11.75`, `seed = 2017`,
  `n_sym = 50000`, and six impairment switches, all off: `bias_drift = 0`, `rin_dbhz = -inf`,
  `pd_sat = inf`, `pa_memory = 0`, `ripple_rho = 0`, `adc_clip = inf`.
- `link/default_tx.m`: 16QAM, 0.4 GBd, 20 GS/s, IF 0.8 GHz, RRC roll-off 0.75, span 16 symbols.
- `link/default_link.m`: the stage parameters (driver gain 24, Vπ 4.4 V, PD CNR 37 dB at
  -10 dBm, PA saturation 1.3, ...), with an `enable` per stage and the optional impairments off.
- `core/experiment_setup.m`: `n_sym 50000`, `train_frac 0.1`, `taps 10`, LMS settings, seed 2017.

## C++

| MATLAB | C++ |
|---|---|
| `default_tx.m` | `src/link/default_tx.h`: struct `TxConfig`, same fields and values |
| `default_link.m` | `src/link/default_link.*`: struct `Link`, same nesting and names (`link.driver.gain`, `link.pd.rin.dbhz`, ...); `ref.a0 = abs(mzm(0, link.mzm))` |
| the switches of `main_demo.m` | `src/core/link_settings.*`: applied as the apply functions of `core/impairment_sweeps.m` (`bias_drift`: `vbias + x*vpi`; `pa_memory`: scales the memory coefficients; `ripple_rho`, `pd_sat`, `rin_dbhz`: enable and set; `adc_clip`: `clip_std`; `fiber_m`: length) |
| `n_sym`, `n_train` | `config/default.json` `"rx"`: `calib_symbols` 50000, `train_symbols` 5000 |

`config/default.json` sets everything at run time:

- `"link"`: the `main_demo.m` switches, same names and "off" values (`"inf"`, `"-inf"` as strings);
- `"stages"`: optional overrides of any `default_link.m` field by its MATLAB name,
  e.g. `{"stages": {"pd": {"cnr_db": 40}}}`;
- `"tx"`: optional overrides of `default_tx.m`;
- `"seed"`, `"rx"`, `"samples"`, `"stream"`, `"record"`.

## What differs on the Pi

Only the random numbers: the generator's own (xoshiro256++, one stream per noise source), seeded
from `seed`, not MATLAB's `rng(2017)`. Same seed and settings give the same stream, bit for bit.

## Verified

- `tests/test_link_stages.cpp` `default_link_values`: a0, ref.bw, f_rf, the impairments off.
- `tests/test_transmit_receive.cpp` `impairment_switches_as_impairment_sweeps`: every switch.
- `tests/test_samples.cpp` `default_config_file`: `config/default.json` gives exactly `default_link`.
