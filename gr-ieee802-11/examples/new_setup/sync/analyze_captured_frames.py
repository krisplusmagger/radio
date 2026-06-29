#!/usr/bin/env python3
"""Compare raw (pre-equalizer) WiFi frames that PASSED the CRC against those that
reached the salvage/erasure stage but still FAILED.

Reads the two capture files written by frame_equalizer_impl.cc:
    captured_good_frames.txt   (outcome=GOOD)
    captured_fail_frames.txt   (outcome=FAIL)

Each frame block is:
    FRAME idx=.. outcome=.. tier=.. M=.. bytes=.. enc=.. nsym=.. total_sym=.. score=..
    S <sym> re0 im0 re1 im1 ... re63 im63     (one line per OFDM symbol, 64 FFT bins)
    END

The samples are the FFT'd, SFO/common-phase-corrected symbols BEFORE channel
equalization or ZigBee cancellation -- so good and failed frames are on equal footing.

What it answers:
  * Is ZigBee WIDER in the failed frames (corrupts bins beyond [28..36])?
  * Is the OUTER-bin (ZigBee-free) WiFi power / SNR LOWER in the failed frames?
  * i.e. is the failure a coverage problem (erase wider) or an SNR problem (no fix)?

Usage:
    python3 analyze_captured_frames.py [good.txt] [fail.txt]
    (defaults to captured_good_frames.txt / captured_fail_frames.txt in CWD)
"""
import sys
import numpy as np

# Subcarrier layout (matches ls.cc / the veto): DC=32, occupied 6..58, ZigBee core
# treated as [28..36]. "Outer" = occupied minus the ZigBee band minus pilots/DC.
DC = 32
OCC = list(range(6, 59))                      # 6..58 inclusive
ZB_LO, ZB_HI = 28, 36                         # ZigBee band (LTF_VETO_BIN_LO/HI)
PILOTS = {11, 25, 39, 53}
ZB_BINS = [k for k in OCC if ZB_LO <= k <= ZB_HI]
OUTER_BINS = [k for k in OCC if not (ZB_LO <= k <= ZB_HI) and k not in PILOTS and k != DC]


def load_frames(path):
    """Yield dicts: {meta fields..., 'sym': complex ndarray (total_sym, 64)}."""
    frames = []
    hdr, rows = None, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("FRAME"):
                hdr, rows = {}, []
                for tok in line.split()[1:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        hdr[k] = v
            elif line.startswith("S "):
                vals = line.split()[2:]
                c = np.array([float(x) for x in vals], dtype=np.float64)
                rows.append(c[0::2] + 1j * c[1::2])           # 64 complex bins
            elif line == "END" and hdr is not None:
                hdr["sym"] = np.array(rows)
                frames.append(hdr)
                hdr, rows = None, []
    return frames


def power_spectrum(frame):
    """Mean |X[k]|^2 over all symbols of one frame -> 64-vector."""
    return np.mean(np.abs(frame["sym"]) ** 2, axis=0)


def summarize(frames, label):
    if not frames:
        print(f"[{label}] no frames")
        return None
    specs = np.array([power_spectrum(fr) for fr in frames])      # (N, 64)
    mean_spec = specs.mean(axis=0)

    zb = mean_spec[ZB_BINS].mean()
    outer = mean_spec[OUTER_BINS].mean()
    sir_db = 10 * np.log10(outer / zb) if zb > 0 else float("inf")

    # Per-frame ZigBee width: how many occupied bins exceed 3x the per-frame outer
    # median (a rough "is this bin ZigBee-contaminated" test). Tells us if the
    # interference spills past [28..36].
    widths = []
    for s in specs:
        outer_med = np.median(s[OUTER_BINS])
        contaminated = [k for k in OCC if k not in PILOTS and k != DC
                        and s[k] > 3.0 * outer_med]
        widths.append(len(contaminated))
    Ms = np.array([float(fr.get("M", "nan")) for fr in frames])

    print(f"[{label}]  n={len(frames)}")
    print(f"    mean M (outer-bin LTF agreement) : {np.nanmean(Ms):.3f}")
    print(f"    outer-band power (ZigBee-free)   : {outer:.4g}")
    print(f"    central-band power (ZigBee)      : {zb:.4g}")
    print(f"    outer/central  (SIR proxy)       : {sir_db:+.1f} dB")
    print(f"    contaminated-bin count  med/max  : {np.median(widths):.0f} / {np.max(widths):.0f}"
          f"   (>{len(ZB_BINS)} means ZigBee spills past [{ZB_LO}..{ZB_HI}])")
    return mean_spec


def main():
    good_path = sys.argv[1] if len(sys.argv) > 1 else "captured_good_frames.txt"
    fail_path = sys.argv[2] if len(sys.argv) > 2 else "captured_fail_frames.txt"

    good = load_frames(good_path)
    fail = load_frames(fail_path)

    print("=" * 64)
    g = summarize(good, "GOOD (passed CRC)")
    print("-" * 64)
    b = summarize(fail, "FAIL (salvage/erasure, no CRC)")
    print("=" * 64)

    if g is not None and b is not None:
        # The two decisive comparisons.
        g_outer = g[OUTER_BINS].mean()
        b_outer = b[OUTER_BINS].mean()
        print("VERDICT:")
        print(f"  outer-band WiFi power  good vs fail : {g_outer:.4g} vs {b_outer:.4g}"
              f"  ({10*np.log10(g_outer/b_outer):+.1f} dB)")
        if b_outer < 0.5 * g_outer:
            print("  -> failed frames are WEAKER on the clean outer carriers:"
                  " an SNR problem, erasing wider will NOT help.")
        else:
            print("  -> failed frames have comparable outer-band power:"
                  " suspect WIDER ZigBee -- try widening the erase band.")

    # Optional plot of the two average spectra (DC-centered bin index).
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        x = np.arange(64) - DC
        plt.figure(figsize=(9, 4))
        if g is not None:
            plt.semilogy(x, g, label=f"GOOD (n={len(good)})")
        if b is not None:
            plt.semilogy(x, b, label=f"FAIL (n={len(fail)})")
        plt.axvspan(ZB_LO - DC, ZB_HI - DC, color="red", alpha=0.12, label="ZigBee band")
        plt.xlabel("subcarrier (relative to DC)")
        plt.ylabel("mean |X[k]|^2")
        plt.title("Raw pre-equalizer power spectrum: good vs CRC-fail frames")
        plt.legend()
        plt.grid(True, which="both", alpha=0.3)
        plt.tight_layout()
        plt.savefig("captured_frames_spectrum.png", dpi=130)
        print("\nsaved captured_frames_spectrum.png")
    except Exception as e:
        print(f"\n(plot skipped: {e})")


if __name__ == "__main__":
    main()
