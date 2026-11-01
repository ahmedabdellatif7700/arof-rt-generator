# Step 2: Central office, driver amplifier and MZM

The AWG voltage `v` becomes the optical field `E` of the modulated laser. Both parts are
memoryless (each output sample depends only on the input sample at the same instant), so there
is nothing to calibrate and no state to carry between blocks.

## MATLAB

`main_demo.m` step 2 (`transmit_receive.m` calls `driver_amp.m` and `mzm.m`):

```matlab
g  = drv_gain * v;                                              % driver: gain, Rapp saturation
vd = g ./ (1 + abs(g/drv_vsat).^(2*drv_smooth)).^(1/(2*drv_smooth));
vb = vbias + bias_drift*vpi;
E  = (1 + exp(1j*pi*(vb + vd)/vpi)) / 2;                        % single-arm MZM: optical field
```

## 2a. Driver amplifier (`driver_amp.m`)

Gain `G = 24` with Rapp saturation at `vsat = 3` V, smoothness `s = 2`:

    vd = G·v / (1 + |G·v / vsat|^(2s))^(1/(2s))

Linear for small inputs, bending over towards `vsat`. At the defaults: 300 mVpp in, 7.2 Vpp
linear, **5.44 Vpp** after compression (printed by `--calibrate`).

C++: `src/link/driver_amp.h`, the same formula. For `s = 2`, `(1 + u²)^¼` is computed as two
square roots (faster than `pow`); any other `smooth` uses `pow`. Disabled: linear gain, as
`driver_amp.m`. The AWG gain `k_awg` of step 1 is applied on the way in, in
`TransmitReceive::channel()`:

```cpp
E_[i] = mzm(driver_amp(k_awg_ * v[i], link_.driver), link_.mzm);   // AWG, driver_amp, mzm
```

## 2b. Mach-Zehnder modulator (`mzm.m`)

    E = (1 + exp(jθ)) / 2,      θ = π (vbias + vd) / vpi,      vpi = 4.4 V, vbias = 2.2 V

At quadrature the intensity is `(1 − sin(π·vd/vpi)) / 2`. A 5.44 Vpp drive against Vπ = 4.4 V
reaches deep into the sine: large symbols are compressed and, as I and Q share the IF waveform,
coupled. The single driven arm adds the phase `θ/2` (chirp), which the fiber's dispersion turns
partly into intensity (step 3). This makes the link **distortion-limited**: about 12 % EVM with
every noise source off, 13.3 % with the noise.

C++: `src/link/mzm.h`, `(1 + e^{jθ})/2` as real and imaginary parts. Disabled: the ideal linear
intensity modulator of `mzm.m`, `sqrt(max((1 − π·vd/vpi)/2, 0))`.

## 2c. Bias drift (impairment switch)

`main_demo.m`: `vb = vbias + bias_drift*vpi`; `impairment_sweeps.m`:
`l.mzm.vbias = l.mzm.vbias + x*l.mzm.vpi`. C++, `src/core/link_settings.cpp`, the same line.

`a0 = abs(mzm(0, link.mzm))` is computed in `default_link` before the drift is applied, so the
noise levels of steps 4-5 (relative to `a0`) do not move with the drift, as in MATLAB
(`main_demo.m` computes `a0` from `vbias`, not `vb`). The drift can be changed live
(`{"set": {"bias_drift": 0.05}}`): the generator recalibrates and flags the change.

## What differs on the Pi

Nothing: both functions are memoryless, so sample by sample gives what MATLAB computes on the
whole array. The Pi also uses `mzm(0)`, the unmodulated carrier, as the fiber's history before
the first sample (step 3), which is what `upfirdn`'s leading zeros give MATLAB.

## Verified

- `tests/test_link_stages.cpp` `driver_amp_and_mzm`: inputs −0.4 to +0.4 V, bias drift 0.05,
  against MATLAB's formulas with `pow` and `exp`: driver within 1e-14, field within 1e-15; both
  disabled branches.
- `tools/compare_matlab.py`: the 7 bias-drift points of `impairments_results.mat` (0 to 0.15·Vπ)
  within tolerance, e.g. 16.02 % EVM at 0.1·Vπ (MATLAB 16.08 %).
