# Step 5: Wireless hop

The 60 GHz signal crosses 1 m of free space from the RAU's horn to the UE's horn and LNA. In the
model a link budget sets the CNR at the UE, and the LNA restores the level: only its noise is
added. Second noise source of the link.

## MATLAB

`main_demo.m` step 5 (`wireless_hop.m` does the same):

```matlab
fspl_db   = 20*log10(4*pi*dist_m*f_rf/c0);
noise_dbm = 10*log10(1.380649e-23*290*1e3) + 10*log10(cnr_bw) + nf_db;
cnr_ue    = p_tx_dbm + g_tx_dbi + g_rx_dbi - fspl_db - noise_dbm;
A = A + noise(numel(A), cnr_ue);                        % the LNA restores the level
```

## 5a. Link budget

| Term | Formula | Value |
|---|---|---|
| Carrier | `f_rf = c/λ_lo − c/λ_sig` (beat of the two lasers) | 59.7 GHz |
| Free-space path loss | `20·log10(4π · 1 m · f_rf / c)` | 68.0 dB |
| Carrier at the UE | `p_tx + g_tx + g_rx − FSPL` = −11 + 20 + 20 − 68.0 | −39.0 dBm |
| Thermal noise | `10·log10(k · 290 K · 1e3)` | −174.0 dBm/Hz |
| In the symbol-rate band | `+ 10·log10(0.4 GHz)` | −88.0 dBm |
| With the noise figure | `+ nf_db` (7 dB) | −81.0 dBm |
| **CNR at the UE** | −39.0 − (−81.0) | **42.0 dB** |

`p_tx_dbm = -11` is fitted to give 42 dB (`default_link.m`).

- The UE CNR does not depend on the ROP (the RAU's AGC fixed the level); only the PD CNR does.
- At the default point the PD noise (33.5 dB) dominates; at high ROP the 42 dB UE noise is the
  floor, where the study's BER curves flatten.

The noise is `carrier_noise.m`, as in step 4.

## 5b. On the Pi (`src/link/wireless_hop.cpp`)

The budget once, at configuration, with the same formula and constants; then per sample
`A += carrier_noise(rng, sigma)` if `wireless.enable`.

- Same parameter names as `default_link.m` (`wireless.f_rf`, `distance_m`, `g_tx_dbi`,
  `g_rx_dbi`, `p_tx_dbm`, `nf_db`), settable through `"stages"`, e.g.
  `{"stages": {"wireless": {"distance_m": 3}}}`.
- Disabled: no noise, as `wireless_hop.m`.
- Its own random stream (UE noise).
- `--calibrate` prints `wireless: 59.7 GHz, FSPL 68.0 dB over 1 m, UE CNR 42.0 dB`, as
  `main_demo.m`.

## What differs on the Pi

Only the noise realization. No memory, nothing calibrated.

## Verified

- `tests/test_link_stages.cpp` `wireless_hop_cnr`: the CNR equals the budget written out (1e-12),
  42.0 dB at the defaults; disabled leaves the signal unchanged.
- `tests/test_transmit_receive.cpp` `noise_free_link_has_no_errors`: PD and UE noise off → no bit
  errors, 12.07 % EVM (the distortion floor of step 2; the noise raises it to 13.3 %).
- `tools/compare_matlab.py`: the ROP sweeps at 300, 400 and 500 mV (PD vs UE noise balance) within
  tolerance at every point, e.g. 500 mV, −11.25 dBm: 21.96 % (MATLAB 21.99 %).
