"""Read an arof_stream recording (see src/stream/recorder.h) or a dataset .mat in the layout of
matlab/analysis/export_dataset.m, as unit-power symbol sequences x, d and their Gray labels idx."""
import json
from pathlib import Path

import numpy as np

GRID = np.sqrt(10)          # unit power -> integer grid {+-1, +-3}


class Sequence:
    def __init__(self, x, d, idx, meta, source):
        self.x, self.d, self.idx = x, d, idx    # received, sent (unit power), labels 0..15
        self.meta, self.source = meta, source


def load(path):
    """A recording directory (symbols.bin, meta.json) or a dataset .mat file."""
    path = Path(path)
    if path.is_dir():
        raw = np.fromfile(path / "symbols.bin", dtype="<f8").reshape(-1, 5)
        meta = json.loads((path / "meta.json").read_text())
        return Sequence(raw[:, 0] + 1j * raw[:, 1], raw[:, 2] + 1j * raw[:, 3],
                        raw[:, 4].astype(np.int64), meta, str(path))
    import scipy.io
    m = scipy.io.loadmat(path)
    rx, tx = m["rx_aligned"].ravel(), m["tx_aligned"].ravel()
    k = 1.0 if np.iscomplexobj(tx) and np.max(np.abs(tx)) < 2 else GRID   # unit or grid scale
    meta = {k2: m[k2].item() for k2 in ("vpp_mv", "rop_dbm", "seed") if k2 in m}
    return Sequence(rx / k, tx / k, m["tx_indices"].ravel().astype(np.int64), meta, str(path))
