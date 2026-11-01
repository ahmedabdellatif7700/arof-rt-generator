#!/usr/bin/env python3
"""A recording as a dataset in the layout of matlab/analysis/export_dataset.m
(reference/arof_training_data.mat): the samples matlab/equalizer.py builds for main_demo.m step 8.

  python tools/export_dataset.py RECORDING_DIR OUT.mat

With T taps and delay D = floor(T/2) (meta.json), the sample whose newest symbol is k is
X = [Re x(k) Im x(k) Re x(k-1) Im x(k-1) ... Re x(k-T+1) Im x(k-T+1)] (newest first) and
Y = [Re d(k-D) Im d(k-D)], the center tap. Scale unit: unit-power symbols (levels +-1/sqrt(10),
+-3/sqrt(10)); grid: x and d times sqrt(10). N symbols give N - T + 1 samples; the recording's
.mem files hold the same samples, split by the stream's TEST flag (demo: 5000 + 44 991)."""
import argparse
import sys

import numpy as np
import scipy.io

from recording import GRID, load


def export(rec_dir, out_file):
    s = load(rec_dir)
    meta = s.meta
    taps = int(meta.get("taps", 3))
    scale = meta.get("scale", "unit")
    delay = taps // 2
    link = meta["settings"]["link"]

    g = GRID if scale == "grid" else 1.0
    rx, tx = g * s.x, g * s.d
    k = np.arange(taps - 1, len(rx))                               # newest symbol of each window
    X = np.column_stack([part for m in range(taps) for part in (rx[k - m].real, rx[k - m].imag)])
    Y = np.column_stack([tx[k - delay].real, tx[k - delay].imag])  # center tap

    idx = s.idx.astype(np.uint8)
    bits = ((idx[:, None] >> np.array([3, 2, 1, 0])) & 1).astype(np.uint8).reshape(-1, 1)  # de2bi left-msb
    evm = float(np.sqrt(np.mean(np.abs(s.x - s.d) ** 2) / np.mean(np.abs(s.d) ** 2)))

    S = {
        "X": X, "Y": Y,
        "rx_aligned": rx.reshape(-1, 1), "tx_aligned": tx.reshape(-1, 1), "tx_symbols": tx.reshape(-1, 1),
        "tx_indices": idx.reshape(-1, 1), "tx_bits": bits,
        "mod_order": np.uint8(16), "bits_per_symbol": np.uint8(4), "delay": np.uint8(delay),
        "equalizer_taps": np.uint8(taps), "equalizer_input_size": np.uint8(2 * taps),
        "equalizer_output_size": np.uint8(2), "scale": scale,
        "vpp_mv": float(link["vpp_mv"]), "rop_dbm": float(link["rop_dbm"]),
        "seed": float(meta["settings"]["seed"]), "evm_no_eq": evm,
        "source": "A-RoF 60 GHz link (Liu JLT 2017), arof_stream (C++ port of matlab/link/transmit_receive.m)",
    }
    scipy.io.savemat(out_file, S, do_compression=True)
    print(f"{out_file}: {X.shape[0]} samples x {X.shape[1]} inputs, {taps} taps, delay {delay}, {scale} scale, "
          f"{link['vpp_mv']:g} mV, {link['rop_dbm']:.2f} dBm, seed {meta['settings']['seed']}, "
          f"no-EQ EVM {100 * evm:.1f} %")
    return S


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recording")
    ap.add_argument("out")
    a = ap.parse_args()
    export(a.recording, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
