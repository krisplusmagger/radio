#!/usr/bin/env python3
"""Analyze WiFi sync detection logs (short vs long, with vs without ZigBee).

Each log line is:  offset=<int> key=wifi_start coarse_cfo=<float>
where 'offset' is the sample index of an emitted wifi_start tag and 'coarse_cfo'
is the detector's frequency-offset estimate.

It quantifies the false-detection behaviour we expect under ZigBee:
  * sync_short re-triggers every MIN_GAP+1 = 481 samples while the (narrowband-
    inflated) autocorrelation metric stays above threshold;
  * a real WiFi frame is ~1120 samples, so any inter-tag gap < ~1120 cannot be a
    new genuine frame -> it is a spurious re-trigger;
  * real detections have a tight, repeatable coarse_cfo; false ones are
    random-phase and scattered.

Usage:
    python3 analyze_detect.py [--plots]
"""
import argparse
import os
import re
from collections import Counter

import numpy as np

# ---- physical constants of this setup (verified from the GRC / PHY) ----
WIFI_RX_RATE = 2e6        # Hz, effective RX sample rate
WIFI_FRAME_LEN = 1120     # samples: STF160 + LTF160 + SIGNAL80 + 9*DATA80
MIN_GAP_RETRIGGER = 481   # sync_short MIN_GAP(480)+1: the false re-trigger period
CFO_CLUSTER_TOL = 0.004   # |coarse_cfo - cluster center| considered "same" detection

FILES = [
    ("short  no-zigbee  ", "wifi_short_detect_hpfilter_nozigbee_2.txt"),
    ("short  with-zigbee", "wifi_short_detect_hpfilter_withzigbee_2.txt"),
    ("long   no-zigbee  ", "wifi_long_detect_hpfilter_nozigbee_2.txt"),
    ("long   with-zigbee", "wifi_long_detect_hpfilter_withzigbee_2.txt"),
]

LINE_RE = re.compile(r"offset=(\d+)\s+key=wifi_start\s+coarse_cfo=([-+0-9.eE]+)")


def load(path):
    """Return (offsets[int64], cfo[float64]) parsed from a detect log."""
    offs, cfos = [], []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = LINE_RE.search(line)
            if m:
                offs.append(int(m.group(1)))
                cfos.append(float(m.group(2)))
    order = np.argsort(offs, kind="stable")
    return np.asarray(offs, dtype=np.int64)[order], np.asarray(cfos)[order]


def dominant_cfo_cluster(cfo, tol=CFO_CLUSTER_TOL):
    """Find the densest coarse_cfo cluster; return (center, fraction_within_tol)."""
    if cfo.size == 0:
        return float("nan"), 0.0
    # coarse histogram to seed the cluster center, then refine to its median
    bins = np.round(cfo / tol).astype(np.int64)
    center_bin = Counter(bins.tolist()).most_common(1)[0][0]
    seed = center_bin * tol
    members = cfo[np.abs(cfo - seed) <= tol]
    center = float(np.median(members)) if members.size else seed
    frac = float(np.mean(np.abs(cfo - center) <= tol))
    return center, frac


def analyze(label, path):
    if not os.path.exists(path):
        print(f"\n### {label}: FILE MISSING ({path})")
        return None
    offs, cfo = load(path)
    n = offs.size
    if n == 0:
        print(f"\n### {label}: no parseable tags")
        return None

    span = int(offs[-1] - offs[0]) if n > 1 else 0
    gaps = np.diff(offs)
    gaps = gaps[gaps > 0]  # ignore duplicate offsets

    # spacing characterization
    mode_gap, mode_cnt = (Counter(gaps.tolist()).most_common(1)[0]
                          if gaps.size else (0, 0))
    sub_frame = float(np.mean(gaps < WIFI_FRAME_LEN)) if gaps.size else 0.0
    at_481 = float(np.mean(gaps == MIN_GAP_RETRIGGER)) if gaps.size else 0.0
    rate_per_Mframe = n / (span / WIFI_FRAME_LEN) if span else float("nan")

    # CFO characterization
    center, frac_in = dominant_cfo_cluster(cfo)

    print(f"\n### {label}")
    print(f"  tags                 : {n:,}")
    print(f"  offset span          : {span:,} samples "
          f"({span / WIFI_RX_RATE * 1e3:.1f} ms @ {WIFI_RX_RATE/1e6:.0f} MHz)")
    print(f"  tags per real-frame  : {rate_per_Mframe:.2f}   "
          f"(1.0 = one tag per {WIFI_FRAME_LEN}-sample frame; >>1 = over-trigger)")
    if gaps.size:
        print(f"  gap  min/med/mean    : {gaps.min()} / "
              f"{int(np.median(gaps))} / {gaps.mean():.0f} samples")
        print(f"  modal gap            : {mode_gap} samples "
              f"({100*mode_cnt/gaps.size:.1f}% of gaps)")
        print(f"  gaps < frame(1120)   : {100*sub_frame:.1f}%  "
              f"<- can't be real new frames (spurious re-triggers)")
        print(f"  gaps == 481          : {100*at_481:.1f}%  "
              f"<- sync_short MIN_GAP+1 false-retrigger signature")
    print(f"  coarse_cfo mean/std  : {cfo.mean():+.4f} / {cfo.std():.4f}")
    print(f"  dominant CFO cluster : {center:+.4f}  "
          f"({100*frac_in:.1f}% within +/-{CFO_CLUSTER_TOL})  "
          f"<- tight = real, scattered = interference")
    return dict(label=label, offs=offs, cfo=cfo, gaps=gaps, n=n, span=span,
                mode_gap=mode_gap, sub_frame=sub_frame, at_481=at_481,
                cfo_center=center, cfo_frac=frac_in)


def comparison(results):
    res = [r for r in results if r]
    if not res:
        return
    print("\n" + "=" * 78)
    print("COMPARISON")
    print("=" * 78)
    hdr = f"{'file':20s} {'tags':>10s} {'tags/frame':>11s} {'mode gap':>9s} " \
          f"{'%<1120':>7s} {'%==481':>7s} {'cfo mean':>9s} {'cfo std':>8s} " \
          f"{'cfo cluster%':>12s}"
    print(hdr)
    print("-" * len(hdr))
    for r in res:
        tpf = r["n"] / (r["span"] / WIFI_FRAME_LEN) if r["span"] else float("nan")
        print(f"{r['label']:20s} {r['n']:>10,d} {tpf:>11.2f} "
              f"{r['mode_gap']:>9d} {100*r['sub_frame']:>6.1f}% "
              f"{100*r['at_481']:>6.1f}% {r['cfo'].mean():>+9.4f} "
              f"{r['cfo'].std():>8.4f} {100*r['cfo_frac']:>11.1f}%")
    print("\nReading the table:")
    print("  * with-zigbee SHORT should show a huge tag count, modal gap 481,")
    print("    a high %==481, and a scattered CFO (low cluster%).")
    print("  * no-zigbee files should show few tags, modal gap >> 1120,")
    print("    ~0% sub-frame gaps, and a tight CFO cluster (high cluster%).")
    print("  * with-zigbee LONG sits between: sync_long consolidates the flood")
    print("    but still has many sub-frame gaps it should have rejected.")


def make_plots(results, outdir, fmt="pdf"):
    try:
        import matplotlib
        matplotlib.use("Agg")
        # keep glyphs as real text (not outlines) in vector output: crisp,
        # selectable, and small files
        matplotlib.rcParams["svg.fonttype"] = "none"
        matplotlib.rcParams["pdf.fonttype"] = 42
        matplotlib.rcParams["ps.fonttype"] = 42
        import matplotlib.pyplot as plt
    except Exception as e:
        print(f"\n[plots skipped: matplotlib unavailable: {e}]")
        return
    res = [r for r in results if r]
    fig, axes = plt.subplots(2, 2, figsize=(13, 9))
    # gap histograms (clipped so the 481 spike is visible)
    ax = axes[0, 0]
    for r in res:
        g = r["gaps"]
        g = g[g < 2500]
        if g.size:
            ax.hist(g, bins=120, histtype="step", label=r["label"])
    ax.axvline(MIN_GAP_RETRIGGER, color="k", ls=":", lw=1, label="481")
    ax.axvline(WIFI_FRAME_LEN, color="r", ls="--", lw=1, label="frame=1120")
    ax.set(title="inter-tag gap (<2500)", xlabel="samples", ylabel="count")
    ax.legend(fontsize=7)
    # gap histograms full-range, log
    ax = axes[0, 1]
    for r in res:
        if r["gaps"].size:
            ax.hist(r["gaps"], bins=120, histtype="step", label=r["label"])
    ax.set(title="inter-tag gap (full range)", xlabel="samples", ylabel="count")
    ax.set_yscale("log")
    ax.legend(fontsize=7)
    # CFO histograms, with a dashed line + numeric mean (average) per file
    ax = axes[1, 0]
    for r in res:
        _, _, patches = ax.hist(r["cfo"], bins=160, histtype="step",
                                label=f"{r['label']} (avg={r['cfo'].mean():+.4f})")
        color = patches[0].get_edgecolor()
        ax.axvline(r["cfo"].mean(), color=color, ls="--", lw=1)
    ax.set(title="coarse_cfo distribution (dashed = mean)",
           xlabel="coarse_cfo", ylabel="count")
    ax.legend(fontsize=7)
    # CFO std bar
    ax = axes[1, 1]
    labels = [r["label"] for r in res]
    ax.bar(range(len(res)), [r["cfo"].std() for r in res])
    ax.set_xticks(range(len(res)))
    ax.set_xticklabels(labels, rotation=30, ha="right", fontsize=7)
    ax.set(title="coarse_cfo std (low=stable=real)", ylabel="std")
    fig.tight_layout()
    out = os.path.join(outdir, f"detect_analysis.{fmt}")
    # pdf/svg are vector -> resolution-independent and sharp at any zoom;
    # dpi only matters for the raster png fallback.
    save_kw = {"bbox_inches": "tight"}
    if fmt == "png":
        save_kw["dpi"] = 200
    fig.savefig(out, format=fmt, **save_kw)
    print(f"\n[plots saved -> {out}]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plots", action="store_true", help="save matplotlib figures")
    ap.add_argument("--format", default="pdf", choices=["pdf", "svg", "png"],
                    help="figure format; pdf/svg are vector (sharp at any zoom)")
    ap.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)),
                    help="directory holding the four detect logs")
    args = ap.parse_args()

    print(f"sample rate {WIFI_RX_RATE/1e6:.0f} MHz | frame {WIFI_FRAME_LEN} samples "
          f"| false-retrigger period {MIN_GAP_RETRIGGER}")
    results = [analyze(label, os.path.join(args.dir, fn)) for label, fn in FILES]
    comparison(results)
    if args.plots:
        make_plots(results, args.dir, args.format)


if __name__ == "__main__":
    main()
