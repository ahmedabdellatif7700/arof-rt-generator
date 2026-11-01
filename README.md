# arof-rt-generator

Real-time A-RoF sample generator for the KV260 training engine. It is a C++17 port of the
60-GHz analog radio-over-fiber link of `reference/main_demo.m` (steps 1–7), runs in a Docker
container on a Raspberry Pi, and streams received 16QAM symbols as Q8.24 training samples over
TCP at a fixed symbol clock.

The plan is [docs/rpi_stream_generator_handoff.md](docs/rpi_stream_generator_handoff.md). The
MATLAB link, its demo notes and the reference dataset are in [reference/](reference/).

## Status

Milestones 1-4 are implemented and checked on the Pi 4, natively and in Docker; the details and
measurements are in [docs/STATUS.md](docs/STATUS.md). In short: 74/74 MATLAB reference points
within tolerance (no-EQ EVM 13.19 % vs 13.21 % at 300 mV, -11.75 dBm); a 50 000-symbol recording
made in the container scores as `main_demo.m` (no EQ 13.3 %, LMS 12.9 %); recordings
byte-compatible with `nn_training_model`; 34-35 k samples/s on one core; a 10-min Dockerized
stream at 10 k samples/s with -0.00005 % rate error, 0.12 ms rms jitter and no gaps.

## Layout

```
CMakeLists.txt, Makefile, Dockerfile, config/default.json
src/link/      one module per matlab/link/*.m function: default_tx, default_link, driver_amp,
               mzm, fiber, photodetector, carrier_noise, rf_amp, rf_response, wireless_hop,
               envelope_detector, adc; transmitter/receiver/transmit_receive = transmit_receive.m
src/core/      rng, comm (qammod, qamdemod, rcosdesign), evaluate_outputs, link_settings
               (main_demo.m switches, applied as impairment_sweeps.m), config
src/stream/    sample_former (Q8.24), recorder, frame (wire format), sample_ring, streamer
               (generator, sender, control threads), --bench
src/main.cpp   arof_stream
tools/         export_dataset.py, evaluate_outputs.py + run_lms.py, compare_matlab.py,
               fake_kv260.py, recording.py
tests/         C++ unit tests, test_tools.py; data/: MATLAB results and Q8.24 fixtures
docs/          handoff plan, STATUS.md
reference/     main_demo.m, DEMO.md, export_dataset.m, arof_training_data.mat
```

## Usage

```
arof_stream --calibrate                    # the link as main_demo.m prints it (no EQ, 44991 test symbols)
arof_stream --record DIR [--samples N]     # a recording, no network
arof_stream --bench 15                     # generation rate of this machine
arof_stream                                # stream to stream.host:port, control on control_port
arof_stream --set '{"link": {"rop_dbm": -13}}' ...   # any setting from the command line

python tools/export_dataset.py DIR out.mat            # recording -> arof_training_data.mat layout
python tools/evaluate_outputs.py DIR_OR_MAT           # no EQ and LMS FFE, as main_demo.m steps 8-9
python tools/compare_matlab.py --set all              # against the MATLAB study's results
python tools/fake_kv260.py --port 5000                # receiver stand-in: checks frames, rate, jitter
echo '{"set": {"rop_dbm": -13}}' | nc RPI 5001        # live change (also {"status": true}, {"stop": true})
```

The Python tools need numpy and scipy (`python -m venv .venv && .venv/bin/pip install numpy scipy`).

## Build

The [Makefile](Makefile) wraps the commands below: `make build`, `make test`, `make run`,
`make docker`, `make docker-run`, `make clean`, `make distclean` (`make help` lists them).
`MCPU`, `CONFIG`, `BUILD_DIR` and `IMAGE` can be overridden, e.g. `make docker MCPU=cortex-a76`.

### Docker (on the Pi)

```
docker build -t arof-stream .                                # Pi 4 (-mcpu=cortex-a72)
docker build --build-arg MCPU=cortex-a76 -t arof-stream .    # Pi 5
```

The build stage also runs the unit tests.

### Native (Pi or laptop)

Needs a C++17 compiler, CMake ≥ 3.20, pkg-config and FFTW 3 (double). nlohmann/json is used
if installed and fetched otherwise (v3.11.3, pinned hash).

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

On aarch64, `-mcpu` defaults to `cortex-a72` (Pi 4). Set `-DAROF_MCPU=cortex-a76` for a Pi 5,
or `-DAROF_MCPU=` to disable it. Other architectures get no `-mcpu`.

## Run

```
docker run --rm --network host --cpus 3 --cap-add SYS_NICE \
    -v $PWD/config:/config -v $PWD/records:/records \
    arof-stream --config /config/default.json
```

- `--network host` avoids the bridge NAT and its extra latency.
- `SYS_NICE` allows `SCHED_FIFO` for the sender thread.
- Port 5001 is the JSON control port; the stream goes to `stream.host:stream.port`
  (the KV260, default 192.168.1.10:5000).

## Settings

`config/default.json` mirrors section 7 of the plan. `"link"` holds the switches of
`main_demo.m` section 0 with the same names and "off" values (`-inf` and `inf` are the strings
`"-inf"` and `"inf"`, as JSON has no infinities). An optional `"stages"` section overrides any
field of `default_link.m` by its MATLAB name, e.g. `{"stages": {"pd": {"cnr_db": 40}}}`, and
`"tx"` any field of `default_tx.m`.
