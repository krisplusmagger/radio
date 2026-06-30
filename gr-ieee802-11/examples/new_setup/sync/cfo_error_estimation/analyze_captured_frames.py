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

# The transmitted WiFi frame is a fixed test payload: bytes==22 -> nsym==9 (BPSK
# rate-1/2: nsym = ceil((16 + 8*22 + 6)/24) = 9). FAIL captures also contain false
# detections whose SIGNAL field decoded to garbage (other bytes/nsym); filter the FAIL
# bucket to these expected values so the analysis reflects REAL WiFi that failed, not
# noise that slipped past the veto. Set EXPECTED_BYTES/NSYM to None to disable.
#
# NOTE: with known-SIGNAL fallback enabled in the block, bytes/nsym are FORCED to the
# known values even on noise frames (sigsrc=KNOWN_FALLBACK in the header), so bytes==22
# alone no longer proves "real WiFi". Also require M >= REAL_M_MIN: real frames keep a
# high outer-bin LTF agreement (~0.98) while noise sits near the veto floor.
EXPECTED_BYTES = 22
EXPECTED_NSYM = 9
REAL_M_MIN = 0.8

# Restrict the GOOD bucket to frames that passed CRC via a specific decode tier (the
# `tier=` header field): CLEAN / SALVAGE_CANCEL / ERASURE / ERASURE_CANCEL. Set to
# "SALVAGE_CANCEL" to compare frames the ZigBee-cancellation rescued against the frames
# nothing could rescue (FAIL) -- this isolates what makes a frame cancellable vs not.
# Set to None to use all good frames.
GOOD_TIER = "SALVAGE_CANCEL"


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


def per_bin_snr(frames):
    """Per-bin SNR (linear) from the two LTF copies, averaged over frames.

    The two captured LTF symbols (rows S 0, S 1) are the SAME training sequence through
    the SAME channel, so signal = (X0+X1)/2 and noise = (X0-X1)/2. Hence
        SNR[k] = |X0[k]+X1[k]|^2 / |X0[k]-X1[k]|^2   (split-symbol estimate).
    This IS per-bin meaningful (unlike a normalized correlation of two scalars, which is
    trivially 1) and localizes WHERE the band loses SNR -- broadband dip = desensitization
    (AGC/clipping); a dip only near the center = ZigBee leakage spilling outward.
    """
    sig = np.zeros(64)
    noi = np.zeros(64)
    cnt = 0
    for fr in frames:
        s = fr["sym"]
        if s.shape[0] < 2:
            continue
        x0, x1 = s[0], s[1]
        sig += np.abs(x0 + x1) ** 2
        noi += np.abs(x0 - x1) ** 2
        cnt += 1
    if not cnt:
        return np.zeros(64)
    return (sig / cnt) / np.maximum(noi / cnt, 1e-12)


def per_symbol_power(frames, bins):
    """Mean power in `bins` per OFDM-symbol index, averaged over frames (common length).
    Reveals whether ZigBee (central bins) is constant across the frame or a burst that
    only overlaps part of it, and whether the outer band degrades over the payload."""
    n = min(fr["sym"].shape[0] for fr in frames)
    acc = np.zeros(n)
    for fr in frames:
        s = fr["sym"][:n]
        acc += np.mean(np.abs(s[:, bins]) ** 2, axis=1)
    return acc / len(frames)


def aggregate_M(frames):
    """Recompute the C++ outer-bin M from captures, to cross-check the header value."""
    vals = []
    for fr in frames:
        s = fr["sym"]
        if s.shape[0] < 2:
            continue
        x0, x1 = s[0][OUTER_BINS], s[1][OUTER_BINS]
        num = np.abs(np.sum(np.conj(x0) * x1))
        den = np.sqrt(np.sum(np.abs(x0) ** 2) * np.sum(np.abs(x1) ** 2))
        vals.append(num / den if den > 1e-12 else 0.0)
    return float(np.mean(vals)) if vals else float("nan")


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

    # Keep only genuine WiFi frames in the FAIL bucket (drop garbage-SIGNAL false
    # detections). Header fields are strings; compare as ints/floats.
    def _int(fr, key):
        try:
            return int(fr.get(key))
        except (ValueError, TypeError):
            return None

    def _float(fr, key):
        try:
            return float(fr.get(key))
        except (ValueError, TypeError):
            return None

    if EXPECTED_BYTES is not None and EXPECTED_NSYM is not None:
        fail_all = fail
        fail = [fr for fr in fail
                if _int(fr, "bytes") == EXPECTED_BYTES and _int(fr, "nsym") == EXPECTED_NSYM
                and (_float(fr, "M") is None or _float(fr, "M") >= REAL_M_MIN)]
        print(f"FAIL filter  bytes=={EXPECTED_BYTES} & nsym=={EXPECTED_NSYM} & M>={REAL_M_MIN}: "
              f"kept {len(fail)} of {len(fail_all)} captured fail frames "
              f"(dropped {len(fail_all) - len(fail)} false detections)")

    # Restrict the GOOD bucket to one decode tier (e.g. SALVAGE_CANCEL) if requested.
    good_label = "GOOD (passed CRC)"
    if GOOD_TIER is not None:
        good_all = good
        good = [fr for fr in good if fr.get("tier") == GOOD_TIER]
        good_label = f"GOOD tier={GOOD_TIER} (rescued)"
        print(f"GOOD filter  tier=={GOOD_TIER}: kept {len(good)} of {len(good_all)} good frames")

    print("=" * 64)
    g = summarize(good, good_label)
    print("-" * 64)
    b = summarize(fail, "FAIL (no tier passed CRC)")
    print("=" * 64)

    # Per-bin split-symbol SNR: the decisive within-frame diagnostic (a true per-bin
    # SNR, immune to the off-vs-on absolute-gain confound).
    gsnr = per_bin_snr(good) if good else None
    bsnr = per_bin_snr(fail) if fail else None
    to_db = lambda lin: 10 * np.log10(np.maximum(lin, 1e-12))
    near = [k for k in OUTER_BINS if abs(k - DC) <= 8]   # carriers just outside ZigBee
    far = [k for k in OUTER_BINS if abs(k - DC) >= 16]   # band edges

    # Each bucket's outer-bin SNR is reported independently, so the FAIL diagnostic
    # still shows when the GOOD bucket is empty (e.g. nothing decoded under ZigBee).
    print("OUTER-BIN SNR (ZigBee-free carriers, split-symbol estimate):")
    if gsnr is not None:
        print(f"  GOOD outer : {to_db(gsnr[OUTER_BINS].mean()):.1f} dB"
              f"   (aggregate M {aggregate_M(good):.3f})")
    if bsnr is not None:
        b_outer_snr = to_db(bsnr[OUTER_BINS].mean())
        print(f"  FAIL outer : {b_outer_snr:.1f} dB"
              f"   near-center {to_db(bsnr[near].mean()):.1f} dB / far-edge {to_db(bsnr[far].mean()):.1f} dB"
              f"   (aggregate M {aggregate_M(fail):.3f})")
        # Leakage gradient (far cleaner than near) vs broadband floor.
        far_minus_near = to_db(bsnr[far].mean()) - to_db(bsnr[near].mean())
        if far_minus_near > 4:
            print(f"  -> FAIL dip concentrated NEAR the center (edges {far_minus_near:.1f} dB"
                  " cleaner) => ZigBee leakage just past the erase band; widening it may help.")
        elif b_outer_snr < 15:
            print("  -> FAIL dip is BROADBAND (edges hurt too) => ZigBee desensitizes the"
                  " whole chain; widening the erase band will NOT help, reduce ZigBee/RX power.")
    if gsnr is not None and bsnr is not None:
        print(f"  drop GOOD->FAIL outer SNR             : "
              f"{to_db(gsnr[OUTER_BINS].mean()) - b_outer_snr:.1f} dB")

    # Per-symbol profile: does the WiFi (outer) power hold up across the payload, or
    # fade toward the end? Compare GOOD (passed) vs FAIL (failed) -- in a single run this
    # isolates what distinguishes a decodable frame from a failed one.
    # Symbols 0,1=LTF  2=SIGNAL  3+=payload.
    def print_per_symbol(frames, label):
        if not frames:
            return
        out = per_symbol_power(frames, OUTER_BINS)
        ref = out[:2].mean()  # LTF power, for a normalized (fade) view
        print(f"PER-SYMBOL ({label}): outer(WiFi) power, and dB relative to LTF")
        for i in range(len(out)):
            role = "LTF" if i < 2 else ("SIG" if i == 2 else f"pay{i-3}")
            rel = 10 * np.log10(out[i] / ref) if ref > 0 else 0.0
            print(f"    sym {i:2d} {role:5s} out={out[i]:.4g}   {rel:+.1f} dB vs LTF")
    good_tag = GOOD_TIER if GOOD_TIER is not None else "GOOD"
    print_per_symbol(good, good_tag)
    print_per_symbol(fail, "FAIL")

    if g is not None and b is not None:
        # Absolute-power comparison (note: confounded if good/fail are different runs).
        g_outer = g[OUTER_BINS].mean()
        b_outer = b[OUTER_BINS].mean()
        print("OUTER-BAND POWER (confounded if good=ZigBee-off, fail=ZigBee-on):")
        print(f"  good vs fail : {g_outer:.4g} vs {b_outer:.4g}"
              f"  ({10*np.log10(g_outer/b_outer):+.1f} dB)")

    # Optional plot of the two average spectra (DC-centered bin index).
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        x = np.arange(64) - DC
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
        if g is not None:
            ax1.semilogy(x, g, label=f"{good_tag} (n={len(good)})")
        if b is not None:
            ax1.semilogy(x, b, label=f"FAIL (n={len(fail)})")
        ax1.axvspan(ZB_LO - DC, ZB_HI - DC, color="red", alpha=0.12, label="ZigBee band")
        ax1.set_ylabel("mean |X[k]|^2")
        ax1.set_title("Raw pre-equalizer power spectrum: good vs CRC-fail frames")
        ax1.legend(); ax1.grid(True, which="both", alpha=0.3)

        occ = np.array(OCC)
        if gsnr is not None:
            ax2.plot(occ - DC, to_db(gsnr[occ]), label=f"{good_tag} SNR")
        if bsnr is not None:
            ax2.plot(occ - DC, to_db(bsnr[occ]), label="FAIL SNR")
        ax2.axvspan(ZB_LO - DC, ZB_HI - DC, color="red", alpha=0.12)
        ax2.set_xlabel("subcarrier (relative to DC)")
        ax2.set_ylabel("per-bin SNR (dB)")
        ax2.set_title("Per-bin split-symbol SNR: shows WHERE the outer band is hurt")
        ax2.legend(); ax2.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig("captured_frames_spectrum.png", dpi=130)
        print("\nsaved captured_frames_spectrum.png")
    except Exception as e:
        print(f"\n(plot skipped: {e})")


if __name__ == "__main__":
    main()
