#!/usr/bin/env python3
"""A recording as a training dataset in the layout of matlab/analysis/export_dataset.m
(reference/arof_training_data.mat), for nn_training_model/equalizer_q8_24.py --dataset.

  python tools/export_dataset.py RECORDING_DIR OUT.mat

With T taps (meta.json), sample n is X = [Re x(n) Im x(n) Re x(n-1) Im x(n-1) ...] (newest first)
and Y = sent symbol d(n). Scale grid: x and d times sqrt(10), Y int16 on {-3,-1,1,3}. N symbols
give N - T + 1 samples (9999 -> 9998: the 80/20 split is 7998 + 2000). X is built from the float
symbols, as export_dataset.m; the recording's .mem words are those of the same values."""
import argparse
import sys

import numpy as np
import scipy.io

from recording import GRID, load


def export(rec_dir, out_file):
    s = load(rec_dir)
    meta = s.meta
    taps = int(meta.get("taps", 2))
    scale = meta.get("scale", "grid")
    link = meta["settings"]["link"]

    k = GRID if scale == "grid" else 1.0
    rx = k * s.x
    tx = np.round(k * s.d) if scale == "grid" else s.d           # tx = round(grid * d)
    n = len(rx)
    X = np.hstack([np.column_stack([rx[taps - 1 - m:n - m].real, rx[taps - 1 - m:n - m].imag])
                   for m in range(taps)])
    Y = np.column_stack([tx[taps - 1:].real, tx[taps - 1:].imag])
    if scale == "grid":
        Y = Y.astype(np.int16)

    idx = s.idx.astype(np.uint8)
    bits = ((idx[:, None] >> np.array([3, 2, 1, 0])) & 1).astype(np.uint8).reshape(-1, 1)  # de2bi left-msb
    evm = float(np.sqrt(np.mean(np.abs(s.x - s.d) ** 2) / np.mean(np.abs(s.d) ** 2)))

    S = {
        "X": X, "Y": Y,
        "rx_aligned": rx.reshape(-1, 1), "tx_aligned": tx.astype(complex).reshape(-1, 1),
        "tx_symbols": tx.astype(complex).reshape(-1, 1),
        "tx_indices": idx.reshape(-1, 1), "tx_bits": bits,
        "mod_order": np.uint8(16), "bits_per_symbol": np.uint8(4), "delay": np.uint8(taps - 1),
        "equalizer_taps": np.uint8(taps), "equalizer_input_size": np.uint8(2 * taps),
        "equalizer_output_size": np.uint8(2),
        "vpp_mv": float(link["vpp_mv"]), "rop_dbm": float(link["rop_dbm"]),
        "seed": float(meta["settings"]["seed"]), "evm_no_eq": evm,
        "source": "A-RoF 60 GHz link (Liu JLT 2017), arof_stream (C++ port of matlab/link/transmit_receive.m)",
    }
    scipy.io.savemat(out_file, S, do_compression=True)
    print(f"{out_file}: {X.shape[0]} samples, {link['vpp_mv']:g} mV, {link['rop_dbm']:.2f} dBm, "
          f"seed {meta['settings']['seed']}, no-EQ EVM {100 * evm:.1f} %")
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
