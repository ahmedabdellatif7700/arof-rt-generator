#!/usr/bin/env python3
"""EVM and BER of a recording (or a dataset .mat) without EQ and with the LMS FFE, as
matlab/main_demo.m steps 8-9: matlab/core/evaluate_outputs.m on the outputs of run_lms.m.

  python tools/evaluate_outputs.py RECORDING_DIR_OR_MAT [--taps 10] [--no-lms] [--json]

Test symbols: ks_test = taps + round(train_frac*n_sym) : n_sym; every output estimates the
symbol at the LMS center tap, d(ks_test - tau). EVM = sqrt(mean(|y - d|^2)) on the continuous
output (unit average power); BER counts Gray bit errors of hard decisions (qamdemod)."""
import argparse
import json
import sys

import numpy as np

from recording import load
from run_lms import matlab_round, run_lms

# experiment_setup.m
CFG = {"M": 16, "train_frac": 0.1, "taps": 10, "lms_mu": 1e-3, "lms_epochs": 20}

# qammod(0:15, 16, 'gray', 'UnitAveragePower', true)
QAM16 = np.array([-3 + 3j, -3 + 1j, -3 - 3j, -3 - 1j, -1 + 3j, -1 + 1j, -1 - 3j, -1 - 1j,
                  3 + 3j, 3 + 1j, 3 - 3j, 3 - 1j, 1 + 3j, 1 + 1j, 1 - 3j, 1 - 1j]) / np.sqrt(10)


def qamdemod(y):
    """qamdemod(y, 16, 'gray', 'UnitAveragePower', true): label of the nearest point."""
    return np.argmin(np.abs(y[:, None] - QAM16[None, :]), axis=1)


def biterr(a, b):
    return int(sum(bin(int(v)).count("1") for v in np.bitwise_xor(a, b)))


def evaluate_outputs(cfg, outs, D, IDX):
    """outs: list of test outputs; D, IDX: the whole sequence. Returns evm, ber per output."""
    n_sym = len(D)
    tau = cfg["taps"] // 2
    ks_test = np.arange(cfg["taps"] + matlab_round(cfg["train_frac"] * n_sym), n_sym + 1)
    d_ref = D[ks_test - tau - 1]
    ref = IDX[ks_test - tau - 1]
    evm, ber = [], []
    for y in outs:
        evm.append(float(np.sqrt(np.mean(np.abs(y - d_ref) ** 2))))
        ber.append(biterr(qamdemod(y), ref) / (ks_test.size * 4))
    return evm, ber, ks_test.size


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="recording directory or dataset .mat")
    ap.add_argument("--taps", type=int, default=CFG["taps"])
    ap.add_argument("--train-frac", type=float, default=CFG["train_frac"])
    ap.add_argument("--no-lms", action="store_true", help="no EQ only")
    ap.add_argument("--json", action="store_true", help="print the results as JSON")
    a = ap.parse_args()

    cfg = dict(CFG, taps=a.taps, train_frac=a.train_frac)
    s = load(a.path)
    n_sym = len(s.x)
    tau = cfg["taps"] // 2
    ks_test = np.arange(cfg["taps"] + matlab_round(cfg["train_frac"] * n_sym), n_sym + 1)
    outs, names = [s.x[ks_test - tau - 1]], ["w/o EQ"]           # no EQ: the received symbol
    if not a.no_lms:
        outs.append(run_lms(cfg, s.x, s.d))
        names.append("LMS FFE")
    evm, ber, n_test = evaluate_outputs(cfg, outs, s.d, s.idx)

    if a.json:
        print(json.dumps({"source": s.source, "n_sym": n_sym, "n_test": n_test, "cfg": cfg,
                          "results": {n: {"evm": e, "ber": b} for n, e, b in zip(names, evm, ber)}}))
        return 0
    m = s.meta.get("settings", {}).get("link", s.meta)
    print(f"{s.source}: {n_sym} symbols"
          + (f", {m['vpp_mv']:g} mV, ROP {m['rop_dbm']:.2f} dBm" if "vpp_mv" in m else ""))
    print(f"\nTest symbols: {n_test} ({4 * n_test} bits)")
    print(f"  {'equalizer':34s} {'EVM %':>8s} {'BER':>10s}")
    for n, e, b in zip(names, evm, ber):
        print(f"  {n:34s} {100 * e:8.1f} {b:10.2e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
