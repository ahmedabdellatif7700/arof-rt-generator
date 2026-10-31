#!/usr/bin/env python3
"""Stand-in for stream_rx on the KV260: a TCP server that receives arof_stream's frames
(src/stream/frame.h), checks them and measures the rate, for tests without a board.

  python tools/fake_kv260.py [--port 5000] [--duration 60] [--save samples.bin] [--json]

Checks per frame: magic, version, seq +1, first_sample continuous, words_per_sample constant,
config_id only rising (each change announced by a CONFIG frame, the next data frame flagged
RECALIBRATED). Reports the mean sample rate (from the first data frame on), the frame arrival
jitter against the ideal schedule, and a histogram of the inter-arrival times. Exits 1 on any
protocol error. Stops at an END frame, after --duration seconds, or on Ctrl-C."""
import argparse
import json
import socket
import struct
import sys
import time

import numpy as np

HDR = struct.Struct("<IHHIQIHHII")   # magic version flags seq first_sample n wps reserved config_id seed
MAGIC, VERSION = 0x46526F41, 1
RECAL, TEST, END, CONFIG = 1, 2, 4, 8


def recv_exact(conn, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5000)
    ap.add_argument("--duration", type=float, default=0, help="stop after this many seconds (0: at END)")
    ap.add_argument("--save", help="write the received words (int32) to this file")
    ap.add_argument("--json", action="store_true", help="print the summary as JSON")
    a = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((a.host, a.port))
    srv.listen(1)
    print(f"fake_kv260: listening on {a.host}:{a.port}", file=sys.stderr, flush=True)
    conn, peer = srv.accept()
    print(f"fake_kv260: connection from {peer[0]}", file=sys.stderr, flush=True)

    errors, configs, recal_frames, test_samples = [], [], 0, 0
    seq_next, sample_next, wps, cid, cid_announced = None, 0, None, 0, 0
    arrivals, sizes = [], []
    n_samples, t_first, t_last = 0, None, None
    out = open(a.save, "wb") if a.save else None
    t_start = time.monotonic()
    ended = False
    try:
        while not (a.duration and time.monotonic() - t_start > a.duration):
            raw = recv_exact(conn, HDR.size)
            if raw is None:
                break
            now = time.monotonic()
            magic, ver, flags, seq, first, n, w, _, config_id, seed = HDR.unpack(raw)
            if magic != MAGIC or ver != VERSION:
                errors.append(f"bad magic/version {magic:#x}/{ver} at frame {seq}")
                break
            if seq_next is not None and seq != seq_next:
                errors.append(f"sequence gap: {seq_next} expected, {seq} received")
            seq_next = seq + 1
            if flags & CONFIG:
                payload = recv_exact(conn, n)
                if payload is None:
                    break
                cfg = json.loads(payload)
                configs.append(cfg)
                if config_id < cid:
                    errors.append(f"config_id went back: {cid} -> {config_id}")
                cid_announced = config_id
                continue
            if flags & END:
                ended = True
                break
            if wps is None:
                wps = w
            elif w != wps:
                errors.append(f"words_per_sample changed: {wps} -> {w}")
            payload = recv_exact(conn, n * w * 4)
            if payload is None:
                break
            if first != sample_next:
                errors.append(f"sample gap: {sample_next} expected, {first} received")
            sample_next = first + n
            if config_id != cid:
                if config_id != cid_announced:
                    errors.append(f"config_id {config_id} without a CONFIG frame")
                if cid and not flags & RECAL:
                    errors.append(f"config_id {cid} -> {config_id} without RECALIBRATED")
                cid = config_id
            recal_frames += bool(flags & RECAL)
            test_samples += n if flags & TEST else 0
            if out:
                out.write(payload)
            if t_first is None:
                t_first = now
            t_last = now
            arrivals.append(now)
            sizes.append(n)
            n_samples += n
    except KeyboardInterrupt:
        pass
    finally:
        conn.close()
        srv.close()
        if out:
            out.close()

    s = {"frames": len(arrivals), "samples": n_samples, "errors": errors, "ended": ended,
         "configs": len(configs), "recalibrated_frames": recal_frames, "test_samples": test_samples}
    if len(arrivals) > 2:
        t = np.array(arrivals)
        cum = np.cumsum(sizes)
        dur = t[-1] - t[0]
        rate = (cum[-1] - sizes[0]) / dur        # samples after the first frame over the time since it
        dt = np.diff(t) * 1e3
        # jitter: arrival time against the ideal schedule of a constant rate through the same counts
        ideal = t[0] + (cum - sizes[0]) / rate
        jit = (t - ideal) * 1e3
        s.update({
            "seconds": dur, "rate_sps": rate,
            "nominal_rate_sps": configs[0]["settings"]["stream"]["rate_sps"] if configs else None,
            "interarrival_ms": {"mean": float(dt.mean()), "p50": float(np.percentile(dt, 50)),
                                "p99": float(np.percentile(dt, 99)), "max": float(dt.max())},
            "jitter_ms": {"rms": float(jit.std()), "p99": float(np.percentile(np.abs(jit - jit.mean()), 99)),
                          "max": float(np.abs(jit - jit.mean()).max())},
        })
        edges = [0, 0.5, 0.9, 1.1, 1.5, 2, 5, 10, 1e9]
        hist = np.histogram(dt, bins=edges)[0]
        s["interarrival_hist_ms"] = {f"{lo:g}-{hi:g}": int(c) for lo, hi, c in zip(edges[:-1], edges[1:], hist)}
        if s["nominal_rate_sps"]:
            s["rate_error"] = rate / s["nominal_rate_sps"] - 1
    if a.json:
        print(json.dumps(s))
    else:
        print(f"frames {s['frames']}, samples {s['samples']}, configs {s['configs']}, "
              f"RECALIBRATED frames {recal_frames}, TEST samples {test_samples}, END {'yes' if ended else 'no'}")
        if "rate_sps" in s:
            print(f"rate {s['rate_sps']:.2f} samples/s over {s['seconds']:.1f} s"
                  + (f" (nominal {s['nominal_rate_sps']:g}, error {100 * s['rate_error']:+.3f} %)" if s.get("rate_error") is not None else ""))
            ia, jt = s["interarrival_ms"], s["jitter_ms"]
            print(f"inter-arrival ms: mean {ia['mean']:.3f}, p50 {ia['p50']:.3f}, p99 {ia['p99']:.3f}, max {ia['max']:.3f}")
            print(f"jitter vs ideal schedule ms: rms {jt['rms']:.3f}, p99 {jt['p99']:.3f}, max {jt['max']:.3f}")
            print("inter-arrival histogram (ms): " + ", ".join(f"{k}: {v}" for k, v in s["interarrival_hist_ms"].items()))
        print("protocol errors: " + ("none" if not errors else "; ".join(errors[:10])))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
