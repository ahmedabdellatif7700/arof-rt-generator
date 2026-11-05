# arof-rt-generator

Real-time A-RoF sample generator for the KV260 equalizer engine. It is a C++17 port of the
60-GHz analog radio-over-fiber link of `main_demo.m` (steps 1–7), runs in a Docker container on a
Raspberry Pi, and streams the samples of `main_demo.m` step 8 (3 taps, center-tap target, unit
power, as `equalizer.py` builds them for the 6-12-2 tanh) as Q8.24 words over TCP at a fixed rate.

- **[docs/DEMO.md](docs/DEMO.md):** the reference document: block diagrams of the system, the
  link and the Pi; each step against MATLAB; the samples and the dataset; the frame format; what
  the KV260 does with the stream; how to run it; the measurements; decisions and open points.
- **[docs/matlab_mirror/](docs/matlab_mirror/README.md):** the mirror with MATLAB step by step: each
  step's MATLAB code, its C++ twin, what differs on the Pi, and how it is verified.
- **[docs/INTEGRATION_HANDOFF.md](docs/INTEGRATION_HANDOFF.md):** merging this repository into
  the main project (MATLAB, `nn_training_model`, RTL, KV260 software): where it goes, the files
  that depend on the rest of the project, the contracts, what is still to do.

## Quick start

On the Pi:

```
make            # build the image if needed, stream to the receiver in config/default.json (the KV260)
make fake       # stream to 127.0.0.1, through the SSH tunnel to the laptop
make status     # is the streamer container up
```

On the laptop (`host/`, Fedora: `make deps` once):

```
make            # build arof_rx, open the SSH tunnel to the Pi, receive 10 cycles as datasets
```

Native build and tests (C++17, CMake ≥ 3.20, FFTW 3; the tools test needs numpy and scipy):

```
make build && make test
```

## Layout

```
src/link/      the link, one module per matlab/link/*.m function
src/core/      rng, comm, evaluate_outputs, link_settings, config
src/stream/    sample_former, recorder, frame.h (wire format), sample_ring, streamer
src/main.cpp   arof_stream
tests/         C++ suites and test_tools.py; data/: MATLAB results and the Q8.24 fixture
tools/         export_dataset, evaluate_outputs, run_lms, compare_matlab, fake_kv260, recording
host/          the laptop kit: Makefile (SSH tunnel), arof_rx (receiver, datasets via matio)
reference/     copies of MATLAB files: main_demo.m, equalizer.py, export_dataset.m, arof_training_data.mat
config/        default.json
docs/          DEMO.md, matlab_mirror/ (step by step), INTEGRATION_HANDOFF.md
```
