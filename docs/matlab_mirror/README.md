# MATLAB demo ↔ Raspberry Pi generator

How the C++ generator mirrors `reference/main_demo.m`, one step at a time: the MATLAB code, the
module of `matlab/link/` that `transmit_receive.m` calls, the C++ file that mirrors it, and what
differs on the Pi.

| `main_demo.m` step | MATLAB module (via `transmit_receive.m`) | C++ on the Pi | Notes |
|---|---|---|---|
| 0 Settings | `default_tx.m`, `default_link.m`, `experiment_setup.m` | `link/default_tx.h`, `link/default_link.*`, `core/link_settings.*`, `config/default.json` | [00_settings.md](00_settings.md) |
| 1 Transmitter | inline: `randi`, `qammod`, `upfirdn`, IF, AWG | `link/transmitter.*`, `core/comm.*` | [01_transmitter.md](01_transmitter.md) |
| 2 Driver + MZM | `driver_amp.m`, `mzm.m` | `link/driver_amp.h`, `link/mzm.h` | [02_driver_mzm.md](02_driver_mzm.md) |
| 3 Fiber | `fiber.m` | `link/fiber.*` | [03_fiber.md](03_fiber.md) |
| 4 Remote antenna unit | `photodetector.m`, `carrier_noise.m`, `rf_amp.m`, `rf_response.m` | `link/photodetector.*`, `link/carrier_noise.h`, `link/rf_amp.*`, `link/rf_response.*` | [04_remote_antenna_unit.md](04_remote_antenna_unit.md) |
| 5 Wireless hop | `wireless_hop.m` | `link/wireless_hop.*` | [05_wireless_hop.md](05_wireless_hop.md) |
| 6 Detector + ADC | `envelope_detector.m`, `adc.m` | `link/envelope_detector.*`, `link/adc.*` | [06_detector_adc.md](06_detector_adc.md) |
| 7 Offline DSP | inline: conv, timing, gain | `link/receiver.*` | [07_offline_dsp.md](07_offline_dsp.md) |
| 8 Equalization | `core/run_lms.m` (and the networks) | not on the Pi (the KV260 equalizes); `tools/run_lms.py` as an offline check | |
| 9 Evaluation | `core/evaluate_outputs.m` | `core/evaluate_outputs.*`, `tools/evaluate_outputs.py` | |
| The chain as a stream | `transmit_receive.m` | `link/transmit_receive.*` | |
| Training samples, streaming | `analysis/export_dataset.m` (layout) | `stream/*`, `tools/export_dataset.py` | |

C++ paths are relative to `src/`.

## The one structural difference

MATLAB processes one finished sequence (50 000 symbols); the Pi produces an endless stream.
Wherever MATLAB uses a statistic of the whole sequence, the Pi measures it once on a calibration
block of the same size (`n_sym + 2*guard` symbols, not streamed) and keeps it fixed:

| Step | Whole-sequence quantity in MATLAB | On the Pi |
|---|---|---|
| 1 | AWG scaling `vpp/(max(v) - min(v))` | `k_awg` from the calibration block |
| 3 | one FFT over the whole sequence | overlap-save FFT, same filter |
| 6 | ADC range `min`, `max` (and `mean`, `std` for clipping) | fixed full scale from the calibration block |
| 7 | `mean(r)`, symbol timing, complex gain | from the calibration block, then fixed |

Everything else is the same formula, sample by sample. The random numbers are the generator's
own (not MATLAB's `rng`), so the results agree statistically, not sample for sample.
