# Step 4: Remote antenna unit (RAU)

The optical field `E` becomes a 60 GHz electrical signal, amplified for the antenna: the
photodetector (first noise), the RF amplifier, the RF response ripple. Formulas applied sample by
sample; the parts with memory are short delay lines. Nothing is calibrated in this step.

## MATLAB

`main_demo.m` step 4 (`transmit_receive.m` calls `photodetector.m`, `rf_amp.m`, `rf_response.m`):

```matlab
A = 10^((rop_dbm - rop_ref_dbm)/10) * E;
if isfinite(rin_dbhz)                                   % each laser's intensity noise
    sigma = sqrt(10^(rin_dbhz/10) * fs/2);
    A = A .* (1 + sigma*(randn(size(A)) + randn(size(A)))/2);
end
if isfinite(pd_sat), A = rapp(A, pd_sat*a0, 2); end     % PD saturation
A_pd = A + noise(numel(A), pd_cnr_db);                  % PD noise
A_in = A_pd / 10^((rop_dbm - rop_ref_dbm)/10);          % AGC
A_pa = rapp(A_in, pa_asat*a0, pa_smooth);               % PA compression
if pa_memory > 0 ... memory polynomial ... end
A = A_pa;
if ripple_rho > 0, A = A + ripple_rho*[zeros(Dr,1); A(1:end-Dr)]; end
```

## 4a. Photodetector (`photodetector.m`, `carrier_noise.m`)

Heterodyne with the second laser: a 60 GHz current whose envelope follows `E`, amplitude
proportional to the optical power, `A = s·E`, `s = 10^((rop_dbm − rop_ref_dbm)/10)`. Against the
fixed PD noise this changes the CNR by **2 dB per dB of ROP** (33.5 dB at −11.75 dBm: 37 − 2·1.75).

In order:

1. laser RIN (`rin.enable`, switch `rin_dbhz`): `A .* (1 + σ(n1 + n2)/2)`, σ = sqrt(10^(dbhz/10)·fs/2);
2. PD saturation (`sat.enable`, switch `pd_sat`): Rapp of |A| at `level·a0`, smoothness 2;
3. PD noise (`p.enable`): `carrier_noise` at `cnr_db = 37` dB (at −10 dBm; RIN, ASE, shot and
   thermal lumped).

`carrier_noise.m`: `sigma2 = a0²/10^(cnr_db/10)·fs/bw`, `n = sqrt(sigma2/2)·(randn + j·randn)`: the
CNR in the symbol-rate band (`bw = 0.4 GHz`), the noise over the simulated band (`fs/bw = 50`).

C++: `src/link/photodetector.cpp` (the same four operations in the same order),
`src/link/carrier_noise.h` (the same `sigma2`), `src/link/rapp.h` (Rapp as written in
`photodetector.m` and `rf_amp.m`, with the `max(r, eps)` guard).

**One deliberate difference:** in MATLAB every source draws from one global stream, so switching
RIN on also changes the PD noise realization (in `impairments_results.mat`, RIN at −150 dB/Hz
moves the no-EQ EVM from 13.238 % to 13.226 %). On the Pi RIN, PD noise and UE noise each have
their own stream: switching an impairment on changes only that impairment.

## 4b. RF amplifier (`rf_amp.m`)

- **AGC:** divides by `s`, so the PA compression does not depend on the ROP.
- **PA compression:** Rapp at `asat·a0 = 1.3·a0`, smoothness 2 (`p.enable`).
- **PA memory** (`memory.enable`, switch `pa_memory`): on `u = A_in/a0` delayed by `m·D`,
  `D = round(0.6 ns·fs) = 12`, m = 1, 2:
  `A_pa += a0·Σ(linear(m)·u_m + cubic(m)·u_m·|u_m|²)`, linear = [−0.08 0.03], cubic = [−0.05 0.02],
  both scaled by the switch as in `impairment_sweeps.m`. Added even with the Rapp disabled, as
  `rf_amp.m`.

C++: `src/link/rf_amp.cpp`; the delays are a 24-sample ring buffer of past `u`, zeros at the start
(MATLAB's `zeros(m*D, 1)`), carried across blocks.

## 4c. RF response ripple (`rf_response.m`)

`ripple.enable` (switch `ripple_rho`): an echo 1 ns later,
`A(t) → A(t) + ρ·exp(j·phase_rad)·A(t − D)`, `D = round(1 ns·fs) = 20`; ripple ±20·log10(1 + ρ) dB.
`main_demo.m` omits the phase (0 by default); the Pi follows `rf_response.m`.

C++: `src/link/rf_response.cpp`, a 20-sample ring buffer, zeros at the start, carried across blocks.

## What differs on the Pi

Only the random numbers (other realizations, one stream per source). The delay lines are exact.

## Verified

`tests/test_link_stages.cpp`, each against the formula written out over the whole sequence:

- `photodetector_noise_level`: 400 000 samples have the `carrier_noise.m` power, within 1 %;
- `photodetector_scaling_rin_sat`: ROP scaling + saturation within 1e-14; RIN on leaves the PD
  noise identical;
- `rf_amp_with_memory`: AGC + Rapp + memory within 1e-14, in blocks of 37 samples;
- `rf_response_echo`: ρ = 0.2, phase 0.7 rad, in blocks of 13, exact.

`tools/compare_matlab.py`, all points within tolerance, e.g.:

| Sweep | Point | Pi | MATLAB |
|---|---|---|---|
| ROP at 300 mV | −18.5 dBm | 30.50 % | 30.49 % |
| RIN | −120 dB/Hz | 13.56 % | 13.58 % |
| PD saturation | level 0.5 | 19.22 % | 19.17 % |
| PA memory | strength 2 | 19.12 % | 19.21 % |
| Ripple | ρ = 0.3 | 15.96 % | 16.06 % |
