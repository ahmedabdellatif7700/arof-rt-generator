"""matlab/core/run_lms.m for one sequence: LMS FFE, a linear combiner on cfg.taps interleaved I/Q
taps with two real outputs (widely linear), trained by online LMS for cfg.lms_epochs epochs on the
training symbols, then applied to the test symbols. Returns the test outputs, estimating the
symbols at the center tap. Indices follow the MATLAB code (1-based) and are converted at use."""
import numpy as np


def matlab_round(v):
    """MATLAB round: halves away from zero."""
    return int(np.sign(v) * np.floor(abs(v) + 0.5))


def tdl(r, ks, M):
    """r: complex sequence; ks: 1-based indices -> 2M x numel(ks), rows [I(k) Q(k) I(k-1) Q(k-1) ...]."""
    ks = np.atleast_1d(np.asarray(ks))
    X = np.zeros((2 * M, ks.size))
    for m in range(1, M + 1):
        seg = r[ks - m]                         # r(ks - m + 1), 1-based
        X[2 * m - 2] = seg.real
        X[2 * m - 1] = seg.imag
    return X


def run_lms(cfg, x, d):
    n_sym = len(x)
    tau = cfg["taps"] // 2
    k_start = cfg["taps"]                       # first index with a full TDL
    n_train = matlab_round(cfg["train_frac"] * n_sym)
    ks_train = np.arange(k_start, k_start + n_train)
    ks_test = np.arange(k_start + n_train, n_sym + 1)

    W = np.zeros((2, 2 * cfg["taps"]))
    Xtr = tdl(x, ks_train, cfg["taps"])
    Dtr = np.vstack([d[ks_train - tau - 1].real, d[ks_train - tau - 1].imag])
    mu = cfg["lms_mu"]
    for _ in range(cfg["lms_epochs"]):
        for j in range(ks_train.size):
            xk = Xtr[:, j]
            y = W @ xk
            W += mu * np.outer(Dtr[:, j] - y, xk)
    y = W @ tdl(x, ks_test, cfg["taps"])
    return y[0] + 1j * y[1]
