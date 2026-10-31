#!/usr/bin/env python3
"""The generator against the MATLAB study at its own conditions: no-EQ EVM and BER (and with
--lms the LMS FFE) of arof_stream next to matlab/results/compare_results.mat and
impairments_results.mat (tests/data/matlab_*_results.csv).

  python tools/compare_matlab.py [--bin build/arof_stream] [--set compare|impairments|all]
                                 [--seeds 6] [--jobs 4] [--lms] [--only 300,-11.75]

Each point runs `arof_stream --calibrate --json` with seeds 1..N: the calibration block is one
sequence of the study (n_sym 50000, the first 10 % train), scored by evaluate_outputs.m's rules.
With --lms each point records a 50000-symbol stream instead and runs evaluate_outputs.py on it
(slower). MATLAB's random numbers are not reproduced, so the check is statistical:
  EVM   within 2.5 % (relative) of the MATLAB mean;
  BER   within a factor 2 of the MATLAB mean when it is >= 1e-4 (enough errors), else reported.
"""
import argparse
import csv
import json
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "tests" / "data"


def read_csv(name):
    with open(DATA / name) as f:
        return list(csv.DictReader(line for line in f if not line.startswith("#")))


def points(which):
    pts = []
    if which in ("compare", "all"):
        for r in read_csv("matlab_compare_results.csv"):
            pts.append({"label": f"{float(r['vpp_mv']):g} mV {float(r['rop_dbm']):g} dBm",
                        "link": {"vpp_mv": float(r["vpp_mv"]), "rop_dbm": float(r["rop_dbm"])}, "ref": r})
    if which in ("impairments", "all"):
        for r in read_csv("matlab_impairment_results.csv"):
            v = float(r["value"])
            pts.append({"label": f"{r['impairment']} {v:g}",
                        "link": {"vpp_mv": 300, "rop_dbm": -11.75, r["setting"]: v}, "ref": r})
    return pts


def run_point(args, pt, seed):
    over = json.dumps({"link": pt["link"], "seed": seed})
    base = [args.bin, "--config", str(ROOT / "config" / "default.json"), "--set", over]
    if not args.lms:
        out = subprocess.run(base + ["--calibrate", "--json"], capture_output=True, text=True, check=True)
        c = json.loads(out.stdout)["calibration"]
        return {"evm": c["evm_no_eq"], "ber": c["ber_no_eq"]}
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(base + ["--record", d, "--samples", "49999"], capture_output=True, check=True)
        out = subprocess.run([sys.executable, str(ROOT / "tools" / "evaluate_outputs.py"), d, "--json"],
                             capture_output=True, text=True, check=True)
        r = json.loads(out.stdout)["results"]
        return {"evm": r["w/o EQ"]["evm"], "ber": r["w/o EQ"]["ber"],
                "evm_lms": r["LMS FFE"]["evm"], "ber_lms": r["LMS FFE"]["ber"]}


def verdict(evm, ber, evm_ref, ber_ref):
    ok_evm = abs(evm / evm_ref - 1) <= 0.025
    if ber_ref >= 1e-4:
        ok_ber = ber > 0 and abs(np.log10(ber / ber_ref)) <= np.log10(2)
    else:
        ok_ber = None
    return ok_evm, ok_ber


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "build" / "arof_stream"))
    ap.add_argument("--set", default="compare", choices=["compare", "impairments", "all"])
    ap.add_argument("--seeds", type=int, default=6)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--lms", action="store_true")
    ap.add_argument("--only", help="vpp_mv,rop_dbm or impairment name: run only matching points")
    a = ap.parse_args()

    pts = points(a.set)
    if a.only:
        pts = [p for p in pts if a.only.replace(",", " mV ").rstrip() in p["label"] or a.only in p["label"]]
    jobs = [(i, s) for i in range(len(pts)) for s in range(1, a.seeds + 1)]
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(lambda j: run_point(a, pts[j[0]], j[1]), jobs))
    per = [[] for _ in pts]
    for (i, _), r in zip(jobs, res):
        per[i].append(r)

    head = f"{'point':24s} {'EVM %':>7s} {'MATLAB':>7s} {'BER':>9s} {'MATLAB':>9s}"
    if a.lms:
        head += f" | {'LMS EVM':>7s} {'MATLAB':>7s} {'LMS BER':>9s} {'MATLAB':>9s}"
    print(f"arof_stream vs MATLAB, {a.seeds} seeds per point (MATLAB: mean of 6 runs)\n" + head)
    n_fail = 0
    for p, rs in zip(pts, per):
        evm, ber = np.mean([r["evm"] for r in rs]), np.mean([r["ber"] for r in rs])
        evm_ref, ber_ref = float(p["ref"]["evm_noeq"]), float(p["ref"]["ber_noeq"])
        ok_evm, ok_ber = verdict(evm, ber, evm_ref, ber_ref)
        line = f"{p['label']:24s} {100 * evm:7.2f} {100 * evm_ref:7.2f} {ber:9.2e} {ber_ref:9.2e}"
        if a.lms:
            el, bl = np.mean([r["evm_lms"] for r in rs]), np.mean([r["ber_lms"] for r in rs])
            line += f" | {100 * el:7.2f} {100 * float(p['ref']['evm_lms']):7.2f} {bl:9.2e} {float(p['ref']['ber_lms']):9.2e}"
            ok_evm = ok_evm and abs(el / float(p["ref"]["evm_lms"]) - 1) <= 0.025
        bad = not ok_evm or ok_ber is False
        n_fail += bad
        print(line + ("  FAIL" if bad else "") + ("" if ok_ber is not None else "  (BER: few errors)"))
    print(f"\n{len(pts) - n_fail}/{len(pts)} points within tolerance")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
