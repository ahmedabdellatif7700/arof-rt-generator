# arof-rt-generator

Real-time A-RoF sample generator for the KV260 training engine. It is a C++17 port of the
60-GHz analog radio-over-fiber link of `reference/main_demo.m` (steps 1–7), runs in a Docker
container on a Raspberry Pi, and streams received 16QAM symbols as Q8.24 training samples over
TCP at a fixed symbol clock.

The plan is [docs/rpi_stream_generator_handoff.md](docs/rpi_stream_generator_handoff.md). The
MATLAB link, its demo notes and the reference dataset are in [reference/](reference/).

## Status

| Milestone | Content | State |
|---|---|---|
| 1 | Skeleton: CMake, Dockerfile, default settings | done |
| 2 | Link stages, receiver (calibration + acquisition), sample former, recorder, unit tests | – |
| 3 | Recordings in the layout of `arof_training_data.mat`, `tools/to_mat.py`, `tools/evm_ber.py` | – |
| 4 | `--bench`, TCP sender with absolute-time pacing, control port, `tools/fake_kv260.py` | – |

## Layout

```
CMakeLists.txt
Makefile                 wrapper: build, test, run, docker, clean
Dockerfile               arm64, multi-stage, debian:bookworm-slim pinned by digest
config/default.json      settings; defaults are main_demo.m section 0
include/, src/           generator (src/main.cpp only, for now)
tests/                   unit tests
tests/data/              small fixtures for the unit tests
tools/                   Python helpers (to_mat.py, evm_ber.py, fake_kv260.py)
docs/                    handoff plan
reference/               main_demo.m, DEMO.md, export_dataset.m, arof_training_data.mat
```

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

`config/default.json` mirrors section 7 of the plan. The impairment switches use the same names
and "off" values as `main_demo.m`; `-inf` and `inf` are written as the strings `"-inf"` and
`"inf"` because JSON has no infinities.
