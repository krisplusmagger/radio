# WiFi-under-ZigBee: capture-based findings

What we measured from the raw-frame captures, what we first concluded, what corrected
that conclusion, and where it leaves the decode problem. This is a running record — read
top to bottom; the last section is the current best understanding.

Tools used (both committed):
- **Raw-frame capture** in `frame_equalizer_impl.cc` — dumps the pre-equalizer frame for
  two buckets: `captured_good_frames.txt` (passed the 32-bit CRC) and
  `captured_fail_frames.txt` (reached salvage/erasure but failed the CRC). See
  `frame_equalizer_algorithm.md` §11 and the capture commit.
- **`analyze_captured_frames.py`** — compares the two buckets: power spectrum, the
  Layer-2 agreement `M`, and a per-bin **split-symbol SNR** `|X0+X1|²/|X0−X1|²` (the two
  LTF copies differ only by noise, so this is a true per-bin SNR).

---

## 1. Setup

The receiver decodes 802.11 OFDM (2 MHz, 52 subcarriers) while a narrowband 802.15.4
ZigBee (~200 kHz) transmits on the same band, **unsynchronized**. ZigBee occupies the
central ~9 FFT bins (`[28..36]`, DC=32); the other 44 occupied bins are nominally
ZigBee-free. The decode strategy (Layer-2 veto + center erasure + ZigBee cancellation)
assumes **the outer bins stay clean**, so they can carry the frame while the center is
erased.

Two capture sessions, at different gains:

| session | `rx_gain` | `tx_gain_zig` | `tx_gain` (WiFi) |
|---|---|---|---|
| 1 | 0.9 | 1.0 | 0.8 |
| 2 | 0.5 | 0.8 | 0.9 |

In **both** sessions, with ZigBee on, **zero frames passed the CRC** — the good bucket is
empty under ZigBee. So every `captured_good_frames.txt` here is a **ZigBee-off** capture
(clean reference), and `captured_fail_frames.txt` is the **ZigBee-on** failing set.
Recovery under ZigBee = **0%**.

## 2. Raw measurements

`M` = outer-bin LTF agreement (≈1 clean). Outer-bin SNR from the split-symbol estimate.

| metric | S1 GOOD (ZB off) | S1 FAIL (ZB on) | S2 GOOD (ZB off) | S2 FAIL all | **S2 FAIL real** |
|---|---|---|---|---|---|
| frames | 400 | 124 | 400 | 185 | **66** |
| mean M | 0.999 | 0.737 | 0.998 | 0.756 | **0.984** |
| outer-bin SNR | 32.0 dB | 8.4 dB | 31.0 dB | 9.0 dB | **17.1 dB** |
| near-center SNR | — | 6.4 dB | — | 7.4 dB | **13.4 dB** |
| far-edge SNR | — | 9.0 dB | — | 9.2 dB | **18.0 dB** |
| central SIR | −0.2 dB | −16.9 dB | −0.2 dB | −11.3 dB | **−9.9 dB** |
| contaminated bins med/max | 0/0 | 7/12 | 0/0 | 4/19 | **3/6** |

"S2 FAIL real" = the S2 fail bucket filtered to the **transmitted** frame
(`bytes==22 → nsym==9`, BPSK rate-1/2). See §4 for why that filter matters.

## 3. First conclusion (later corrected): "broadband collapse"

The unfiltered FAIL frames showed outer-bin SNR crashing from ~31 dB (clean) to ~9 dB,
and — critically — the **band edges** (25 subcarriers from ZigBee) were just as bad as
the near-center bins (S1: 9.0 vs 6.4 dB). A narrowband interferer cannot lower the SNR
that far from its own band over the air, so this looked like **receiver/RF
desensitization** affecting the whole band, with three candidate mechanisms: ADC
clipping, ZigBee TX broadband noise, or RX reciprocal mixing.

**ADC clipping was ruled out by the session-2 change.** Going S1→S2 we *lowered*
`rx_gain` 0.9→0.5 (more headroom), *lowered* ZigBee TX, and *raised* WiFi TX — every
change should have raised the WiFi SNR if clipping were the cause. Outer-bin SNR moved
8.4 → 9.0 dB, i.e. **not at all**. A floor that is independent of `rx_gain` is not made
in the ADC; it is referred to the antenna. So the conclusion at this stage was: the
strong ZigBee is filling the **entire** band with junk (TX broadband noise / reciprocal
mixing), the "use the clean outer bins" premise is violated, and the fix is RF-level
(cleaner / weaker ZigBee TX, more antenna isolation) — no DSP could help.

## 4. The correction: 64% of the "FAIL" frames were not WiFi

The FAIL bucket contains every frame that passed the LTF veto and decoded a SIGNAL field
but then failed the payload CRC — **including false detections** whose SIGNAL decoded to
garbage. The real transmitted frame is a fixed test payload: `bytes==22`, which gives
`nsym = ceil((16 + 8·22 + 6)/24) = 9`. Filtering the S2 FAIL bucket to `bytes==22 &
nsym==9` kept **66 of 185** frames and dropped **119 (64%) false detections**.

Those 119 noise frames have low SNR *because they are noise*, and they had been dragging
every average down. The 66 genuine WiFi failures look completely different:

- outer-bin SNR **17.1 dB** (not 9 dB), mean **M 0.984** (not 0.756),
- ZigBee **confined to its band** (contaminated med 3 / max 6, no spillover),
- and the per-bin SNR is a **gradient, not a flat floor**: far edges **18.0 dB** vs
  near-center **13.4 dB** (edges 4.6 dB cleaner).

So the "edges are as bad as the center" evidence that drove the broadband-collapse story
was an artifact of the noise frames. With them removed, the edges are clearly cleaner
than the center.

## 5. Current understanding (best as of now)

For genuine WiFi-under-ZigBee frames that fail to decode:

1. **The far band is usable.** Far-edge SNR is ~18 dB (down from 31 dB clean — a real but
   modest ~13 dB broadband component, not a collapse). 18 dB is workable for BPSK r=1/2.
2. **ZigBee leaks just past the erase band.** The carriers immediately outside `[28..36]`
   (bins ~24–27 and 37–38) sit at ~13 dB — 4.6 dB worse than the edges — and are **not
   erased**. They feed wrong bits into the decoder.
3. **That ring of leaked-but-not-erased carriers is the dominant failure cause.** The
   center is erased and the far band is fine, but this ring carries enough errors to blow
   the convolutional-code budget `2e + f < d_free = 10` → CRC fails.

This is a **DSP-addressable** problem, not the RF dead-end concluded in §3. Converting a
wrong bit (costs 2 in the budget) into an erasure (costs 1) is net-positive when the bin
is bad, so the indicated fix is to **widen the erase band** to cover the leaked ring
(e.g. `[26..38]` or `[24..40]`), made an independent tunable so it does not also widen the
Layer-2 veto exclusion. A width sweep is warranted; `[24..40]` erases ~14/48 carriers
(29%), which is still decodable for rate-1/2 but should not be overshot.

The earlier RF concerns are **not fully gone** — the ~13 dB broadband drop at the edges is
real and means the ZigBee TX is dirtier / closer than a real-world narrowband interferer
would be — but they are no longer the thing blocking decode of these frames.

## 6. Caveats on the data

- **Confounded comparison.** GOOD = ZigBee-off, FAIL = ZigBee-on, recorded at different
  times. Absolute-power comparisons across the two are not apples-to-apples. The `M` and
  split-symbol SNR are **within-frame** and normalized, so those (the numbers we rely on)
  are robust to the gain/time difference.
- **Selection bias.** FAIL is the *failing tail*; under ZigBee nothing decoded, so there
  is no "good under ZigBee" set to compare against yet. Once some frames decode (after the
  erase-band widening and/or RF cleanup), capture good+fail in **one** ZigBee-on run for a
  clean within-run comparison.
- **Capture caps.** Each bucket is capped at `CAPTURE_MAX_PER_BUCKET = 400` frames/run.

## 7. Next steps

1. Widen the erase band (independent tunable) and sweep its width; re-run and check
   whether `bytes==22/nsym==9` frames start passing CRC.
2. Directly measure the broadband floor: **ZigBee on, WiFi off**, look at the RX spectrum
   (`uhd_fft` or a file capture). Quantify how far the broadband shelf sits below the
   ZigBee peak — that bounds how much of the §5 ~13 dB edge drop is the ZigBee TX vs the
   receiver, and how clean a real interferer would need to be.
3. Change one knob at a time from here on (S1→S2 changed three at once, which muddied the
   isolation).
```
