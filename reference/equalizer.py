"""Neural equalizers for the 16QAM RoF link, called by core/run_network.m on the data it writes.

  python equalizer.py DATA.mat RESULT.mat

DATA.mat names the model ('liu' or 'real') and its settings. Both models see the same target
symbols d(j), j = k - tau over the LMS training and test indices k (core/run_lms.m); each model reads the taps
x(j + tau_m), ..., x(j + tau_m - taps + 1) around them, with tau_m = taps // 2 (center tap).
Every (condition, run) sequence is its own network; all train in lockstep (batched numpy).
Returns the outputs on the test symbols at unit power; core/evaluate_outputs.m makes the decisions. For the
'liu' model it also returns y_pre, the continuous output before the output activation, so
that its EVM can be measured on the same continuous output as the other equalizers.

model 'liu': the complex-valued multilevel ANN-NLE of Liu et al., JLT 35(20), 2017, replicated
  `taps` complex inputs, `hidden` complex neurons, one complex output, no bias (Fig. 3). Every
  neuron applies the complex multilevel sigmoid (eq. (18))
    fc(s) = f(Re s) + j f(Im s)
    f(x)  = 2 c2 / (1 + exp(-c1 (x - 2 level))) - c2 + 2 level           (eq. (2))
    level = -1 if x <= -1, 0 if -1 < x <= 1, 1 if x > 1,  c2 = (1 + e^-c1) / (1 - e^-c1)   (eq. (3))
  whose saturation regions sit at the 16QAM levels -3, -1, +1, +3, so inputs and targets are
  scaled from unit power to that grid (x sqrt(10)) and back. Online complex back-propagation
  (Appendix I), one sample per step, the training symbols repeated for n_epochs in order:
    Delta2 = eR f'(s2R) + j eI f'(s2I),  grad w2i = 2 Delta2 conj(y1i)           (I-5), (I-6)
    eh_i = conj(w2i) Delta2,  Delta1_i = ehR_i f'(s1R_i) + j ehI_i f'(s1I_i)     (I-14), (I-15)
    grad w1ij = 2 Delta1_i conj(x(n-j+1)),  w <- w - eta grad                    (I-16), (19), (20)
  Back-propagation falls into local minima where part of the symbols settle in a wrong
  saturation region, so each sequence trains `restarts` networks from different random weights
  and keeps the lowest training MSE (no test data). The restarts are not in the paper.

model 'real': the hardware-compatible network, with the structure and training rule of
  nn_training_model: real-valued, inputs [I(k) Q(k) I(k-1) Q(k-1) ...], one hidden layer
  ('sigmoid' as in the engine, 'tanh', the 'multilevel' f(x) above, or 'hard_multilevel', its
  piecewise-linear version), a linear output layer for I
  and Q, biases on both layers. Plain per-sample SGD on the MSE, samples in order, the
  training symbols repeated for n_epochs, back-propagation with the pre-update weights, no
  momentum, no restarts. Weights and biases start uniform in +-1/sqrt(fan-in). With the
  multilevel activations, inputs and targets are scaled to the {+-1, +-3} grid as above.
"""
import sys
import time

import numpy as np
import scipy.io

SCALE = np.sqrt(10)                 # unit-power 16QAM -> {+-1, +-3} grid


def multilevel(x, c1):
    """f(x) of eq. (2)-(3) and its derivative f'(x) of eq. (17), elementwise on real x."""
    c2 = (1 + np.exp(-c1)) / (1 - np.exp(-c1))
    level = np.where(x > 1, 1.0, np.where(x <= -1, -1.0, 0.0))
    z = np.exp(np.clip(-c1 * (x - 2*level), -60, 60))
    return 2*c2 / (1 + z) - c2 + 2*level, 2*c1*c2*z / (1 + z)**2


def hard_multilevel(x, c1):
    """Piecewise-linear multilevel activation: eq. (2) with the sigmoid replaced by the hard
    sigmoid clip(u/4 + 1/2, 0, 1), so c2 = 1 for c1 >= 2. Same saturation levels -3, -1, +1, +3,
    linear ramps of slope c1/2 between them; no exponentials, cheap in fixed point."""
    level = np.where(x > 1, 1.0, np.where(x <= -1, -1.0, 0.0))
    u = c1 * (x - 2*level) / 4 + 0.5
    inside = (u > 0) & (u < 1)
    return 2*np.clip(u, 0, 1) - 1 + 2*level, np.where(inside, c1 / 2, 0.0)


def sigmoid(x):
    s = 1 / (1 + np.exp(-x))
    return s, s * (1 - s)


def tanh(x):
    t = np.tanh(x)
    return t, 1 - t*t


def activation(name, c1):
    """(function, input/target scale) for a hidden activation name."""
    if name == "sigmoid":
        return sigmoid, 1.0
    if name == "tanh":
        return tanh, 1.0
    if name == "multilevel":
        return (lambda z: multilevel(z, c1)), SCALE
    if name == "hard_multilevel":
        return (lambda z: hard_multilevel(z, c1)), SCALE
    raise ValueError(f"unknown activation {name}")


def tdl(x, ks, taps):
    """x: (C, N) complex -> (C, len(ks), taps), columns [x(k) x(k-1) ... x(k-taps+1)]."""
    return np.stack([x[:, ks - m] for m in range(taps)], axis=-1)


# ---- Liu et al.: complex multilevel network ----

def fc(s, c1):
    """Complex activation (eq. (18)): value and the derivatives of its real and imaginary parts."""
    fr, dr = multilevel(s.real, c1)
    fi, di = multilevel(s.imag, c1)
    return fr + 1j*fi, dr, di


def liu_forward(W1, W2, x, c1):
    """W1: (C, H, M), W2: (C, H), x: (C, ..., M) complex -> hidden and output, with derivatives."""
    y1, d1r, d1i = fc(np.einsum("chm,c...m->c...h", W1, x), c1)
    y2, d2r, d2i = fc(np.einsum("ch,c...h->c...", W2, y1), c1)
    return (y1, d1r, d1i), (y2, d2r, d2i)


def train_liu(d, x, t, k_train, k_test, rng):
    scalar = lambda name: d[name].item()
    taps, hidden, n_epochs = int(scalar("taps")), int(scalar("hidden")), int(scalar("n_epochs"))
    eta, c1, init, K = float(scalar("eta")), float(scalar("c1")), float(scalar("init")), int(scalar("restarts"))
    x, t = SCALE * x, SCALE * t
    C = x.shape[0]

    cinit = lambda shape: init * (rng.uniform(-1, 1, shape) + 1j*rng.uniform(-1, 1, shape))
    W1 = cinit((C*K, hidden, taps))                  # network c*K + j: restart j of sequence c
    W2 = cinit((C*K, hidden))
    X = np.repeat(tdl(x, k_train, taps), K, axis=0)
    D = np.repeat(t[:, k_train - taps // 2], K, axis=0)
    for _ in range(n_epochs):
        for i in range(k_train.size):
            u = X[:, i, :]
            (y1, d1r, d1i), (y2, d2r, d2i) = liu_forward(W1, W2, u, c1)
            e = y2 - D[:, i]
            delta2 = e.real*d2r + 1j*e.imag*d2i
            eh = np.conj(W2) * delta2[:, None]
            delta1 = eh.real*d1r + 1j*eh.imag*d1i
            W2 -= eta * 2*delta2[:, None]*np.conj(y1)
            W1 -= eta * 2*delta1[:, :, None]*np.conj(u)[:, None, :]

    _, (y_train, _, _) = liu_forward(W1, W2, X, c1)
    keep = np.arange(C)*K + np.mean(np.abs(y_train - D)**2, axis=1).reshape(C, K).argmin(axis=1)
    W1, W2 = W1[keep], W2[keep]

    # outputs after the multilevel output activation, and the continuous sum before it
    y = np.empty((C, k_test.size), dtype=np.complex64)
    y_pre = np.empty_like(y)
    for c in range(0, C, 8):                         # chunks keep the test TDL small
        (y1, _, _), (out, _, _) = liu_forward(W1[c:c + 8], W2[c:c + 8], tdl(x[c:c + 8], k_test, taps), c1)
        y[c:c + 8] = out / SCALE
        y_pre[c:c + 8] = np.einsum("ch,c...h->c...", W2[c:c + 8], y1) / SCALE
    return y, f"Liu complex {taps}-{hidden}-1, {K} restarts, {n_epochs} epochs", {"y_pre": y_pre}


# ---- hardware-compatible real-valued network ----

def iq_tdl(x, ks, taps):
    """x: (C, N) complex -> (C, len(ks), 2*taps) real, columns [I(k) Q(k) I(k-1) Q(k-1) ...]."""
    cols = []
    for m in range(taps):
        cols += [x[:, ks - m].real, x[:, ks - m].imag]
    return np.stack(cols, axis=-1)


def train_real(d, x, t, k_train, k_test, rng):
    scalar = lambda name: d[name].item()
    taps, hidden, n_epochs = int(scalar("taps")), int(scalar("hidden")), int(scalar("n_epochs"))
    lr, c1 = float(scalar("lr")), float(scalar("c1"))
    name = str(d["activation"].item())
    act, scale = activation(name, c1)
    C, n_in = x.shape[0], 2*taps

    U = scale * iq_tdl(x, k_train, taps)             # (C, n_train, n_in)
    tt = scale * t[:, k_train - taps // 2]
    D = np.stack([tt.real, tt.imag], axis=-1)        # (C, n_train, 2)
    lim1, lim2 = 1/np.sqrt(n_in), 1/np.sqrt(hidden)
    W1 = rng.uniform(-lim1, lim1, (C, hidden, n_in));  b1 = rng.uniform(-lim1, lim1, (C, hidden))
    W2 = rng.uniform(-lim2, lim2, (C, 2, hidden));     b2 = rng.uniform(-lim2, lim2, (C, 2))
    for _ in range(n_epochs):
        for i in range(k_train.size):
            u = U[:, i, :]
            h, dh = act(np.einsum("chi,ci->ch", W1, u) + b1)
            e = np.einsum("coh,ch->co", W2, h) + b2 - D[:, i, :]          # dMSE/dy
            g1 = np.einsum("coh,co->ch", W2, e) * dh                      # pre-update W2
            W2 -= lr * e[:, :, None] * h[:, None, :];  b2 -= lr * e
            W1 -= lr * g1[:, :, None] * u[:, None, :];  b1 -= lr * g1

    y = np.empty((C, k_test.size), dtype=np.complex64)
    for c in range(0, C, 8):
        h, _ = act(np.einsum("chi,cti->cth", W1[c:c + 8], scale * iq_tdl(x[c:c + 8], k_test, taps))
                   + b1[c:c + 8, None, :])
        out = (np.einsum("coh,cth->cto", W2[c:c + 8], h) + b2[c:c + 8, None, :]) / scale
        y[c:c + 8] = out[..., 0] + 1j*out[..., 1]
    return y, f"real {n_in}-{hidden}-2 {name}, lr {lr:g}, {n_epochs} epochs", {}


def main(data_file, result_file):
    d = scipy.io.loadmat(data_file)
    scalar = lambda name: d[name].item()
    tau, k0, n_train = int(scalar("tau")), int(scalar("k_start")) - 1, int(scalar("n_train"))
    taps = int(scalar("taps"))

    # (N, runs, n_cond) -> (n_cond*runs, N): network c = cond*runs + run. MATLAB drops
    # trailing singleton dimensions, so one condition (or one run) arrives with fewer axes
    for key in ("x", "d"):
        d[key] = d[key].reshape(d[key].shape + (1,) * (3 - d[key].ndim))
    n, runs, n_cond = d["x"].shape
    x = d["x"].transpose(2, 1, 0).reshape(-1, n).astype(np.complex128)
    t = d["d"].transpose(2, 1, 0).reshape(-1, n).astype(np.complex128)

    # targets j = k - tau of core/run_lms.m; this model's window ends at j + taps//2
    shift = taps // 2 - tau
    k_train = np.arange(k0, k0 + n_train) + shift
    k_test = np.arange(k0 + n_train, n) + shift

    t0 = time.time()
    rng = np.random.default_rng(int(scalar("seed")))
    train = train_liu if str(d["model"].item()) == "liu" else train_real
    y, what, extra = train(d, x, t, k_train, k_test, rng)
    to_matlab = lambda a: a.reshape(n_cond, runs, -1).transpose(2, 1, 0)
    scipy.io.savemat(result_file, {"y_ann": to_matlab(y), **{k: to_matlab(v) for k, v in extra.items()}})
    print(f"  {what}: {x.shape[0]} sequences, {time.time() - t0:.1f} s")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
