# WiFi-under-ZigBee receiver — expert handoff

**Audience.** An engineer picking up this work cold. This document is self-contained:
it states the goal, the hardware/software setup, the receiver algorithm, every
diagnostic we built, the full measured data, the conclusions (including ones that were
later corrected), and the current open problem with recommended next steps.

**One-line status.** The WiFi link decodes fine with ZigBee off (≈90% of frames), but a
baseline ≈10% of frames fail **even with ZigBee physically disconnected**, and that
failure is caused by **TX underflows truncating the tail of the WiFi burst** — a
flow-control problem, not interference. Under ZigBee this compounds with real
interference and recovery collapses to ~0%. **Fix the TX underflow first**; everything
downstream depends on a clean WiFi link.

---

## 1. Research goal

802.11 OFDM (WiFi) and 802.15.4 O-QPSK (ZigBee) share the same RF band. The receiver must
detect and decode WiFi despite a co-channel ZigBee interferer. ZigBee is **narrowband**
(~200 kHz) and sits in the **center** of the 2 MHz WiFi channel, so the original thesis is:
the outer WiFi subcarriers are ZigBee-free and can carry the frame while the corrupted
center is recovered or erased. The longer-term research aim is to use the ZigBee-corrupted
center subcarriers to estimate the ZigBee channel and cancel it. **This handoff is about a
blocker discovered underneath that goal.**

## 2. Hardware & flowgraph

Three Ettus USRPs (X300-class, 192.168.10.x, 10 GbE), one GNU Radio flowgraph that both
transmits and monitors:

| role | device | key settings |
|---|---|---|
| **ZigBee TX** | `zigbee_tx`, addr **192.168.10.2** | `samp_rate_zig=2e5`, gain `tx_gain_zig` |
| **WiFi TX** | `wifi_tx`, addr **192.168.10.6** | `samp_rate=2e6`, gain `tx_gain` (ch0); `tx_gain_zig` also drives ch1 |
| **RX/monitor** | `uhd_usrp_source_0`, addr **192.168.10.7** | `samp_rate=2e6`, gain `rx_gain` |

Common: `freq=5.92 GHz`, `lo_offset=0`, BPSK rate-1/2 (`encoding=0`), `pkt_rate=57`,
`tx_window=0.06`, `tx_lead=0.25`. The RX (10.7) feeds **both** the WiFi receiver
(`wifi_phy_hier_0`) and the ZigBee receiver (`ieee802_15_4_oqpsk_phy_0`). WiFi is sent as
**timed bursts** (`tx_time` tags, PPS/clock discipline).

> Note `tx_gain_zig` controls **both** ZigBee paths (`zigbee_tx` ch0 and `wifi_tx` ch1);
> `tx_gain` controls only WiFi (`wifi_tx` ch0). So changing `tx_gain_zig` is a clean
> single-variable knob on ZigBee power.

**Flowgraph files** (run from `examples/new_setup/sync/`):
`wifi_zigbee_sync_tx.grc` (source) → `wifi_zigbee_sync_tx.py` (generated). A working copy
is also kept in `cfo_error_estimation/` and is the one actually run during these tests
(`python3 ./wifi_zigbee_sync_tx.py`). **GRC regeneration overwrites `.py` edits**, so
durable changes belong in the `.grc` (via a Python Snippet for wiring).

**The fixed test frame:** the TX always sends the same payload, **22 bytes**. For BPSK
rate-1/2, `N_DBPS=24`, so `nsym = ceil((16 + 8·22 + 6)/24) = 9` payload OFDM symbols. A
captured frame is therefore **12 OFDM symbols**: 2 LTF + 1 SIGNAL + 9 payload (each symbol
= 64-FFT + 16 CP = 80 samples).

## 3. WiFi receiver pipeline

```
USRP src (10.7) → sync_short → sync_long → fft(64) → frame_equalizer → decode_mac → message_debug
                  (STF auto-   (LTF cross-  (per-      (▼ all the      (PSDU,       (RX TEXT
                   correlation) correlation) symbol     coexistence     CRC)         print)
                                             FFT)       logic)
```

- `sync_short` triggers on the STF lag-16 autocorrelation (re-trigger gap 480).
- `sync_long` finds the LTF by cross-correlation and emits a **`wifi_start`** stream tag
  (value = coarse CFO). **It has no rejection threshold**, so under ZigBee it emits many
  false `wifi_start` tags.
- `frame_equalizer` (the file of interest, `lib/frame_equalizer_impl.cc`) does
  CFO/phase tracking, channel equalization, SIGNAL decode, payload decode, and **all the
  ZigBee-coexistence machinery**. Full walkthrough in
  **`frame_equalizer_algorithm.md`** (committed alongside this doc).

## 4. frame_equalizer: features relevant here

See `frame_equalizer_algorithm.md` for the complete description. Summary of the
coexistence features and their tunables (top-of-file constants in
`lib/frame_equalizer_impl.cc`):

1. **Layer-1 reset guard** — once SIGNAL has validated and payload is in flight, ignore
   further `wifi_start` tags (stops ZigBee false triggers from wiping an in-flight decode).
2. **Layer-2 clean-band LTF veto** (`ltf_clean_band_ok`) — the two LTF copies must agree on
   the ZigBee-free outer subcarriers:
   `M = |Σ conj(X0[k])·X1[k]| / sqrt(Σ|X0|²·Σ|X1|²)`, k in `[6,58]\[28,36]`.
   M≈1 real frame, ≈0.15 noise. Accept iff `M ≥ LTF_VETO_THRESH = 0.25`.
   Constants `LTF_VETO_BIN_LO/HI = 28/36`.
3. **3-tier SIGNAL decode** — clean → ZigBee-cancel salvage → center-erasure.
4. **4-tier payload decode** (`try_decode_with_salvage`) — clean → ZigBee-cancel →
   erasure → erasure+cancel. **Every accept is gated on the 32-bit CRC**, so nothing
   downstream can false-ACK.
5. **ZigBee cancellation** — stored reference (`examples/ofdm_zigbee/*.csv`), searches a
   raw-sample alignment `ZIGBEE_DEFAULT_LTF_START_RAW=176 ± ZIGBEE_SEARCH_RADIUS=144`
   (**up to 289 decode attempts per frame** — expensive, see §9).
6. **Center erasure** — erase data carriers whose FFT bin ∈ `[ERASE_BIN_LO..HI]` (decoder
   value 2). Now an **independent, wider** band `[26..38]` (was coupled to the veto’s
   `[28..36]`); widened to catch ZigBee leakage just past the veto band.
7. **Known-SIGNAL knowledge** (new) — the TX frame is fixed (`bytes==22`, BPSK):
   - `KNOWN_SIGNAL_VALIDATE=true`: reject a decoded SIGNAL that doesn’t match (must be
     wrong) → cuts false detections.
   - `KNOWN_SIGNAL_FALLBACK=true`: if SIGNAL can’t be decoded, assume the known frame and
     proceed to payload (CRC stays the gate). Recovers ZigBee-destroyed SIGNALs **but
     forces every veto-passing frame — including noise — through full payload decode**
     (relevant to §9).

All committed. Build/install in §10.

## 5. Diagnostic instrumentation we added

- **Per-frame log `ltf_diag.txt`** (always on, truncated each run): one
  `FRAME M=… ACCEPT/REJECT` line per detected frame; a `SIGNAL DECODED|KNOWN_FALLBACK|FAIL`
  line; and a `PAYLOAD M=… CLEAN|SALVAGE_CANCEL|ERASURE|ERASURE_CANCEL|FAIL` line.
- **Raw-frame capture** — dumps the **pre-equalizer** frame (FFT’d, CFO/phase-corrected,
  *before* channel equalization or ZigBee cancellation) for two buckets:
  - `captured_good_frames.txt` — passed the CRC.
  - `captured_fail_frames.txt` — reached salvage/erasure but failed the CRC.
  Header per frame: `outcome tier M bytes enc nsym total_sym score sigsrc`, then one line
  per OFDM symbol (`S <i> re0 im0 … re63 im63`). Capped `CAPTURE_MAX_PER_BUCKET=400`/run.
- **`zigbee_correction_stats.txt`** — running attempt/success/`recovery_rate`.
- **`analyze_captured_frames.py`** — loads both buckets and reports: power spectrum, the
  veto `M`, a per-bin **split-symbol SNR** (`|X0+X1|²/|X0−X1|²` — the two LTF copies differ
  only by noise, so this is a true per-bin SNR), and a **per-symbol power profile**
  (outer-band WiFi power per OFDM symbol). It filters the FAIL bucket to real WiFi
  (`bytes==22 & nsym==9 & M≥0.8`) because with `KNOWN_SIGNAL_FALLBACK` the SIGNAL fields
  are forced, so `bytes==22` alone no longer proves a frame is real.

## 6. The investigation, with data

Read this as a narrative — two conclusions were drawn and then **corrected** by better
controls. The final conclusion is §7.

### 6.1 Session 1 — strong ZigBee (rx_gain 0.9, tx_gain_zig 1.0, tx_gain 0.8)

| | GOOD (ZB off) | FAIL (ZB on) |
|---|---|---|
| frames | 400 | 124 |
| mean M | 0.999 | 0.737 |
| outer-bin SNR | 32.0 dB | 8.4 dB |
| near / far-edge SNR | — | 6.4 / 9.0 dB |
| central SIR | −0.2 dB | −16.9 dB |
| contaminated bins (med/max) | 0/0 | 7/12 |

Outer SNR crashed 32→8 dB and the **band edges were as bad as the center** → looked like
**receiver/RF desensitization** of the whole band. Candidate mechanisms: ADC clipping,
ZigBee TX broadband noise, RX reciprocal mixing.

### 6.2 ADC clipping ruled out

Going to Session 2 we **lowered rx_gain 0.9→0.5** (more headroom), lowered ZigBee, raised
WiFi — all of which should raise WiFi SNR if clipping were the cause. Outer SNR moved
8.4→9.0 dB, i.e. **unchanged**. A floor independent of rx_gain is **not** ADC clipping.

### 6.3 The FAIL bucket was 60–97% false detections

`sync_long` has no threshold, so under ZigBee the FAIL bucket is full of **false
detections** that pass the veto, decode SIGNAL to garbage, and fail CRC. Filtering to the
real frame (`bytes==22 → nsym==9`) changed the picture completely (Session 2):

| | FAIL unfiltered (185) | **FAIL real WiFi (66)** |
|---|---|---|
| mean M | 0.756 | **0.984** |
| outer-bin SNR | 9.0 dB | **17.1 dB** |
| near / far-edge | 7.4 / 9.2 dB | **13.4 / 18.0 dB** |
| contaminated (med/max) | 4/19 | **3/6** (no spillover) |

The genuine failures are **not** a collapsed band: outer 17 dB, clean LTF, ZigBee confined
to its slot, but a **leakage gradient** — near-center (just outside the erase band) 4.6 dB
worse than the edges → ZigBee leaking just past `[28..36]`. **This corrected the
"broadband collapse" story** and motivated the wider erase band (§4.6) and known-SIGNAL
features (§4.7).

### 6.4 After implementing wider erase + known-SIGNAL: still 0 recovery, and a new clue

Session 3 (strong ZigBee again, SIR −17.9 dB, only 6 real frames). The new build is
confirmed running (`sigsrc` present; 238 KNOWN_FALLBACK vs 3 DECODED). **0 frames passed
CRC.** The decisive new tool was the **per-symbol power profile**:

```
ZigBee (central): ~0.08, CONSTANT across all 12 symbols
WiFi   (outer)  : LTF 0.0026 → SIGNAL 0.0018 → … → last payload 0.0004  (fades ~8 dB)
per-symbol SIR  : LTF −15 dB → last payload −23 dB
```

ZigBee is steady; **WiFi fades across the frame**. The LTF (where M/SNR are measured) is
the *cleanest* part — the payload is much worse. This pointed (wrongly, see §6.6) at
ZigBee corrupting sync/CFO so WiFi drifts out of the FFT window over the frame.

### 6.5 Weak/balanced ZigBee — still 0 recovery

Session 4 (tx_gain_zig low, SIR −0.4 dB, **contaminated med 0** — ZigBee barely touching
any bin). **Still 0 frames passed CRC.** Per-symbol showed **both** central and outer
fading together over the last symbols (central now ≈ WiFi because ZigBee is negligible).
Key realization: we were essentially already near a ZigBee-free condition and it *still*
failed → the failure is **not** ZigBee.

### 6.6 ZigBee fully OFF (gain 0 **and antenna physically detached**) — the answer

Session 5. The WiFi link recovers (**400 GOOD frames pass CRC**), but **49 real frames
still FAIL with no ZigBee in the system at all.** The single-run good-vs-fail per-symbol
profile is conclusive:

| symbol | GOOD (passed) | FAIL (failed) |
|---|---|---|
| pay0–4 | −0.7 dB (flat) | −0.7 dB (flat) |
| pay5 | −0.7 dB | −1.1 dB |
| pay6 | −0.7 dB | **−3.4 dB** |
| pay7 | −0.8 dB | **−6.9 dB** |
| pay8 (last) | −2.2 dB | **−7.8 dB** |

(dB relative to each frame's own LTF.) GOOD frames stay flat to the end; **FAIL frames
collapse over the last 3–4 OFDM symbols** — and this is **with ZigBee disconnected.** The
fade is the entire difference between a frame that decodes and one that doesn't.

Other Session-5 numbers: GOOD outer SNR 31.8 dB (M 0.999); FAIL outer SNR 21.0 dB
(M 0.991) — the failing frames are also ~10 dB weaker overall *and* fade at the end.

## 7. Root cause (current best conclusion)

> **CORRECTION (later finding).** The device attribution below is **wrong**: the
> underflowing sink is the **ZigBee TX** (192.168.10.2), **not** the WiFi TX — disabling
> the ZigBee path removes every underflow, and the WiFi TX has ~30× the per-burst slack and
> does not starve. The underflow is also **rate-independent** (persists at `pkt_rate=3`),
> so the "burst-rate margin" reasoning here is also wrong. See
> **`zigbee_tx_underflow_investigation.md`** for the corrected analysis. **The causal link
> between the ZigBee-TX underflow and the WiFi decode failures (§6.6) is UNCONFIRMED** —
> re-run `analyze_captured_frames.py` with ZigBee fully disabled to test whether the WiFi
> failures actually disappear. The §6 *observations* (per-symbol tail fade, etc.) stand;
> only the §7 underflow *cause/device* is corrected.

**TX underflows are truncating the tail of the WiFi burst.** The run log shows the WiFi
`usrp_sink` chronically starving:

```
usrp_sink :error: In the last 751 ms, 22 underflows occurred.
… 142 "underflow" error lines; long "UUUU…" marker streams; no overflow, no late.
```

Mechanism: a TX underflow injects a gap (zeros) into the outgoing burst. If it lands near
the end of a WiFi burst, the last few OFDM symbols are lost/attenuated → their bits become
noise → the 32-bit CRC fails. ~10% of frames coincide with an underflow and fail; the rest
decode. This is **TX-side and ZigBee-independent**, which matches every observation:

- It persists with ZigBee gain 0 **and antenna removed**.
- The failing frames have **clean LTFs** (M 0.99) — sync locked fine — then a **cliff at
  the end**, which is a missing transmit tail, not an RX windowing drift (a drift would be
  gradual and would corrupt the LTF too).
- GOOD and FAIL differ only in the burst tail.

Under ZigBee, this baseline truncation **compounds** with real interference (the leakage
gradient of §6.3 and the strong-ZigBee desensitization of §6.1), which is why recovery
collapses to ~0% there.

### 7.1 Suspected contributing cause — RX load starving the TX

The TX and RX share one host. The RX-side `frame_equalizer` is **very heavy**: up to **289
ZigBee-cancellation decode attempts per frame**, and `KNOWN_SIGNAL_FALLBACK` now forces
**every veto-passing frame — including hundreds of noise frames per run — through full
payload decode**. This can saturate the CPU and starve the TX thread → underflow. So the
coexistence processing we added to *rescue* frames may be *causing* the underflows that
*break* them. This is a hypothesis to test (§9), not yet proven.

## 8. What is NOT the problem (so the expert doesn’t re-investigate)

- **Not ADC clipping** — lowering rx_gain 0.9→0.5 didn’t change the floor (§6.2).
- **Not (primarily) ZigBee interference** for the baseline 10% — it persists with ZigBee
  physically disconnected (§6.6).
- **Not the LTF / sync detection** — failing frames have clean LTFs (M≈0.99).
- **Not a wrong known-frame length** — `nsym=9` is correct (400 frames decode with it).
- **Not RX overflow** — the logs show **no** overflow/late, only TX underflow.

## 9. Open questions / recommended next steps (priority order)

1. **Fix the TX underflow — prerequisite for everything.**
   - Confirm the §7.1 hypothesis: set `KNOWN_SIGNAL_FALLBACK=false` and reduce
     `ZIGBEE_SEARCH_RADIUS` (144 → e.g. 8 or 0), rebuild, run **ZigBee-off**, and check
     whether `underflow` counts and the 10% fail rate drop. If they do, the RX load is
     starving the TX.
   - Reduce RX cost structurally: gate the 289-candidate ZigBee salvage so it runs **only
     when ZigBee is actually detected**, not on every frame; don’t force noise frames
     through payload.
   - Check the TX pipeline independent of RX: buffering on `wifi_tx`, the burst scheduler
     (`tx_time`, `tx_lead=0.25`, `tx_window=0.06`), and whether the packet source
     (`seq_input` embedded block) feeds the timed burst without stalling. Consider whether
     the host can sustain 2 Msps TX + 2 Msps RX + this processing; if not, lower the rate
     or move RX processing off the TX host.
   - Verify the fix by **per-symbol flatness**: a healthy run has GOOD-style flat profiles
     for ~all frames and `recovery_rate` → ~1.0 with ZigBee off.
2. **Re-establish a clean WiFi baseline** (≈100% decode, ZigBee off) before any further
   coexistence work. Until then, ZigBee results are confounded by burst truncation.
3. **Only then** re-introduce ZigBee and measure the *pure* interference effect, using a
   single ZigBee-on run so GOOD and FAIL come from the same conditions (no off-vs-on
   confound). Sweep `tx_gain_zig` one step at a time.
4. **Quantify ZigBee directly** at some point: capture **ZigBee-on / WiFi-off** spectrum
   (`uhd_fft` or a file sink) to measure how far ZigBee’s broadband shelf sits below its
   peak — this bounds how much of the Session-1 broadband effect is the ZigBee TX vs the
   receiver, and how clean a real interferer would need to be.
5. **CFO/timing robustness** (deferred): §6.4 suggested ZigBee may also pull the CFO so
   WiFi drifts across the frame. Revisit only after the underflow is fixed; if the fade
   persists under ZigBee on otherwise-clean bursts, add ZigBee-robust coarse CFO/timing in
   sync (a prior "clean-band CFO / P0" attempt was reverted because it regressed on
   false-detection-polluted data — redo it on real frames only).

## 10. Build / install / run / analyze

```bash
# Build + install the OOT module (needed after any lib/*.cc|*.h change)
cd /home/kk/gnuradio/gr-ieee802-11/build
make gnuradio-ieee802_11 && sudo make install && sudo ldconfig

# Run the flowgraph (from the working copy dir, so logs/captures land together)
cd /home/kk/gnuradio/gr-ieee802-11/examples/new_setup/sync/cfo_error_estimation
python3 ./wifi_zigbee_sync_tx.py 2>&1 | tee run.log        # Ctrl-C to stop

# Analyze
python3 ./analyze_captured_frames.py                       # good vs fail, per-symbol, SNR

# Inspect flow-control health
grep -c underflow run.log                                  # TX starvation
grep -m3 underflow run.log
```

Outputs land in the launch dir (truncated each run): `ltf_diag.txt`,
`captured_good_frames.txt`, `captured_fail_frames.txt`, `zigbee_correction_stats.txt`,
`captured_frames_spectrum.png`.

## 11. Key tunables (top of `lib/frame_equalizer_impl.cc`)

| constant | value | meaning |
|---|---|---|
| `LTF_VETO_BIN_LO/HI` | 28 / 36 | veto exclusion (ZigBee core + DC) |
| `LTF_VETO_THRESH` | 0.25 | accept frame iff clean-band M ≥ this |
| `ERASE_BIN_LO/HI` | 26 / 38 | **independent** erasure band (wider than veto) |
| `KNOWN_SIGNAL_VALIDATE` | true | reject SIGNAL that ≠ known frame |
| `KNOWN_SIGNAL_FALLBACK` | true | force known frame when SIGNAL undecodable (heavy — see §9) |
| `KNOWN_FRAME_BYTES / ENCODING` | 22 / 0 | the fixed test frame (BPSK r-1/2) |
| `ZIGBEE_DEFAULT_LTF_START_RAW` | 176 | center of ZigBee alignment search |
| `ZIGBEE_SEARCH_RADIUS` | 144 | ± samples → **289 decode attempts/frame** (heavy — see §9) |
| `CAPTURE_MAX_PER_BUCKET` | 400 | raw-frame capture cap per run |

Analysis-side (`analyze_captured_frames.py`): `EXPECTED_BYTES/NSYM = 22/9`,
`REAL_M_MIN = 0.8`, `ERASE/ZB_LO/HI`, `OUTER_BINS`.

## 12. Companion documents (same folder)

- `frame_equalizer_algorithm.md` — full block walkthrough (the receiver internals).
- `wifi_zigbee_capture_findings.md` — the capture-based findings (sessions 1–2, the
  broadband-collapse correction).
- `layer2_ltf_veto_explained.md` — the veto in plain terms.
- `erasure_decode_implementation.md` — center erasure + the Viterbi both-erased fix.
- `wifi_zigbee_decode_design.md` — the original decode design (veto + erasure + cancel).

## 13. Data-integrity caveats (so results aren’t over-read)

- In sessions 1–4, **GOOD = ZigBee-off, FAIL = ZigBee-on, captured at different times** →
  absolute-power comparisons across them are confounded. The **within-frame** metrics
  (M, split-symbol SNR, per-symbol profile) are normalized and robust to this. Session 5
  is a single run (both buckets, ZigBee off) and is the clean comparison.
- The FAIL bucket needs the `bytes==22 & nsym==9 & M≥0.8` filter to remove false
  detections (and, with fallback on, forced-bytes noise). Unfiltered FAIL stats are
  noise-dominated and misleading (this caused the first wrong conclusion).
- Capture buckets are capped at 400/run; with fallback on, the FAIL bucket fills mostly
  with noise, so a long run can hit the cap before capturing many real failures — raise
  the cap or filter by `sigsrc=DECODED` / high M.
- `git log` for this work: search commits touching `lib/frame_equalizer_impl.*`,
  `examples/new_setup/sync/analyze_captured_frames.py`, and the `*_findings/handoff.md`
  docs for the full change history and rationale.
```
