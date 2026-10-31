"""A recording through tools/export_dataset.py and tools/evaluate_outputs.py:
  - the exported .mat has the layout of reference/arof_training_data.mat (fields, MATLAB classes,
    shapes, X/Y from rx/tx, Gray bits);
  - the recorder's .mem files hold the words nn_training_model writes for that .mat (float32 cast,
    to_q, 80/20 split, uppercase hex);
  - no-EQ and LMS results of the recording are those of the reference dataset (within statistics).
Usage: python tests/test_tools.py BUILD/arof_stream   (exit 77 = skipped: no numpy/scipy)"""
import subprocess
import sys
import tempfile
import warnings
from pathlib import Path

try:
    import numpy as np
    import scipy.io
except ImportError:
    print("numpy/scipy not available: skipped")
    sys.exit(77)

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_outputs as ev                           # noqa: E402
from export_dataset import export                       # noqa: E402
from recording import load                              # noqa: E402
from run_lms import matlab_round, run_lms               # noqa: E402

failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    failures += not cond


def to_q(v):   # equalizer_q8_24.py: astype(float32), then fixedpoint.to_q (round half even, sat32)
    r = np.rint(np.asarray(v, dtype=np.float32).astype(np.float64) * 2.0 ** 24)
    return np.clip(r, -2 ** 31, 2 ** 31 - 1).astype(np.int64)


def evaluate(path):
    s = load(path)
    cfg = ev.CFG
    tau = cfg["taps"] // 2
    ks = np.arange(cfg["taps"] + matlab_round(cfg["train_frac"] * len(s.x)), len(s.x) + 1)
    evm, ber, _ = ev.evaluate_outputs(cfg, [s.x[ks - tau - 1], run_lms(cfg, s.x, s.d)], s.d, s.idx)
    return evm, ber


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        rec = Path(tmp) / "rec"
        subprocess.run([binary, "--config", str(ROOT / "config" / "default.json"), "--set",
                        '{"samples": {"schedule": "train_test"}}', "--record", str(rec)],
                       check=True, capture_output=True)
        out = Path(tmp) / "rec.mat"
        export(rec, out)

        ref = scipy.io.loadmat(ROOT / "reference" / "arof_training_data.mat")
        got = scipy.io.loadmat(out)
        keys = sorted(k for k in ref if not k.startswith("__"))
        check(keys == sorted(k for k in got if not k.startswith("__")), "same fields as arof_training_data.mat")
        warnings.simplefilter("ignore", np.exceptions.ComplexWarning)   # mat_dtype drops imaginary parts
        rc = scipy.io.loadmat(ROOT / "reference" / "arof_training_data.mat", mat_dtype=True)
        gc = scipy.io.loadmat(out, mat_dtype=True)
        for k in keys:
            if k == "source":
                continue
            check(rc[k].dtype == gc[k].dtype and rc[k].shape == gc[k].shape
                  and np.iscomplexobj(ref[k]) == np.iscomplexobj(got[k]), f"{k}: class, shape, complexity")
        X, Y, rx, tx = got["X"], got["Y"], got["rx_aligned"].ravel(), got["tx_aligned"].ravel()
        check(np.array_equal(X[:, 0] + 1j * X[:, 1], rx[1:]) and np.array_equal(X[:, 2] + 1j * X[:, 3], rx[:-1]),
              "X = [rx(n) rx(n-1)], newest first")
        check(np.array_equal(Y[:, 0] + 1j * Y[:, 1], tx[1:]), "Y = tx(n)")
        check(set(np.unique(Y)) == {-3, -1, 1, 3}, "Y on the grid {-3,-1,1,3}")
        check(np.array_equal(got["tx_bits"].ravel().reshape(-1, 4) @ [8, 4, 2, 1], got["tx_indices"].ravel()),
              "tx_bits: de2bi(idx, 4, 'left-msb')")

        # the .mem words nn_training_model would write for this .mat
        words = to_q(np.hstack([X, Y.astype(np.float64)]))
        split = int(0.8 * len(X))
        for name, part in (("train", words[:split]), ("test", words[split:])):
            lines = (rec / f"{name}_samples_q8_24.mem").read_text().split()
            exp = [f"{int(w) & 0xFFFFFFFF:08X}" for w in part.ravel()]
            check(lines == exp, f"{name}_samples_q8_24.mem = to_q(float32) of the .mat ({len(exp)} words)")

        e_ref, b_ref = evaluate(ROOT / "reference" / "arof_training_data.mat")
        e_rec, b_rec = evaluate(rec)
        print(f"     reference: no EQ {100 * e_ref[0]:.2f} %, LMS {100 * e_ref[1]:.2f} %;"
              f" recording: no EQ {100 * e_rec[0]:.2f} %, LMS {100 * e_rec[1]:.2f} %")
        check(abs(e_rec[0] - e_ref[0]) < 0.004 and abs(e_rec[1] - e_ref[1]) < 0.004, "EVM as the reference dataset")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
