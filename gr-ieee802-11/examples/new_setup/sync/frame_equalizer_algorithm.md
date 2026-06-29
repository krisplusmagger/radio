# How `frame_equalizer_impl.cc` works (current algorithm)

This documents the **current** logic in `lib/frame_equalizer_impl.cc` as it stands on
disk — the WiFi-under-ZigBee receiver block. It is the block that turns FFT'd OFDM
symbols into decoded bits, and it carries all the ZigBee-coexistence machinery (the
Layer-2 LTF veto, ZigBee cancellation, and erasure decoding).

> Read alongside `layer2_ltf_veto_explained.md` (the veto) and
> `erasure_decode_implementation.md` (erasure). This file is the **whole-block** view.

---

## 1. Where it sits in the chain

```
USRP source → sync_short → sync_long → fft → ►frame_equalizer◄ → decode_mac → message_debug
                (STF)       (LTF)      (per-symbol 64-FFT)        (bytes)        (print)
```

- **Input** (`io_signature` `64 * sizeof(gr_complex)`): one **frequency-domain OFDM
  symbol** per item — 64 complex FFT bins, already time-synced by `sync_long` and
  FFT'd upstream.
- **Output** (`io_signature` `48`): one byte per **data subcarrier** — 48 bytes per
  decoded OFDM symbol (each byte is the soft/hard bits for that carrier, consumed by
  `decode_mac`).
- **Message ports:** `symbols` (equalized constellation points, for plotting),
  `tx_feedback` (ACK/NACK pmt symbols for the ARQ loop).
- The trigger that starts a frame is the **`wifi_start` stream tag** that `sync_long`
  attaches to the first LTF sample; its value is the coarse CFO estimate.

## 2. The OFDM symbol timeline

The block tracks `d_current_symbol`, the index of the symbol within the current frame:

| `d_current_symbol` | what it is | role |
|---|---|---|
| `0`, `1` | the two **LTF** halves | channel estimate + CFO/veto |
| `2` | the **SIGNAL** field | rate + length (always BPSK rate-1/2) |
| `3 … d_frame_symbols+2` | the **payload** OFDM symbols | data + 32-bit CRC |

`d_frame_symbols` (= `d_frame.n_sym`) is the payload symbol count parsed out of SIGNAL,
so the **last** payload symbol arrives at `d_current_symbol == d_frame_symbols + 2`,
which is the cue to run the full payload decode.

## 3. Per-symbol front-end processing (every symbol)

Each captured symbol goes through the same DSP before any decoding
(`general_work`, lines ~220-294):

1. **Sampling-frequency-offset (SFO) compensation** — a phase *ramp across
   subcarriers* that grows with symbol index:
   ```
   X[i] *= exp( j · 2π · d_current_symbol · 80 · (ε₀ + er) · (i − 32) / 64 )
   ```
   `ε₀` is the coarse offset from the `wifi_start` tag (normalized by `d_freq`); `er`
   is the running residual estimate. `(i−32)` is the bin's distance from DC, `80` is
   the samples-per-symbol (64 + 16 CP).

2. **Common phase error `β`** from the four pilot carriers (bins 11, 25, 39, 53). For
   the LTF (`<2`) the raw pilots are summed; for data symbols the BPSK pilot polarity
   `POLARITY[(sym−2) % 127]` is removed first:
   ```
   β = arg( p·X[11] + p·X[39] + p·X[25] − p·X[53] )     (data)
   ```

3. **Residual CFO `er`** from the pilot phase *change* vs. the previous symbol's
   pilots (`conj(prev)·current`), scaled to a normalized frequency. Smoothed with an
   IIR (`α = 0.1`) for symbols ≥ 2.

4. **Apply** the common phase correction `X[i] *= exp(−jβ)` to every bin.

5. **Buffering:** symbols 0/1 are copied to `d_saved_signal_symbols` (for the LTF dump
   file); **every** symbol is copied to `d_captured_symbols` (the working buffer that
   all the salvage/erasure re-decodes operate on).

6. **Equalizer feed:** for the two LTF symbols, `d_equalizer->equalize(...)` is called
   so the stateful equalizer (LS/LMS/STA/COMB, chosen by the flowgraph) builds the
   WiFi channel estimate **before** SIGNAL is decoded.

## 4. Frame state machine (`general_work`)

```
while (input samples remain):
    if (wifi_start tag here):
        ── Layer 1 reset-guard ──
        in_live_frame = d_signal_valid && d_current_symbol <= d_frame_symbols+2
        if NOT in_live_frame:  reset_frame_capture(); load new ε₀
        else:                  IGNORE this tag   (protect in-flight frame)

    front-end DSP (section 3)

    if d_current_symbol == 1:   d_ltf_clean_ok = ltf_clean_band_ok()   // Layer 2 veto
    if d_current_symbol == 2 && d_ltf_clean_ok:   decode SIGNAL (3 tiers)
    if d_signal_valid && d_current_symbol == d_frame_symbols+2:   decode PAYLOAD
    d_current_symbol++
```

### Layer 1 — reset-guard (`general_work` ~184-211)

Under ZigBee, `sync_short`/`sync_long` fire **many false `wifi_start` tags mid-frame**.
Without protection each one would call `reset_frame_capture()` and wipe a decode in
progress. The guard: **once a frame's SIGNAL has validated (`d_signal_valid`) and its
payload is still arriving, ignore further `wifi_start` tags.** It does *not* tell WiFi
from ZigBee — it only stops an already-validated frame from being clobbered. The
preamble window (before `d_signal_valid`) is still interruptible by design.

## 5. Layer 2 — clean-band LTF veto (`ltf_clean_band_ok`, ~520)

The decisive "is this a real WiFi frame?" test, run once both LTF symbols are captured
(`d_current_symbol == 1`). The two LTF halves are *identical training sequences through
the same channel*, so for a genuine frame they agree; ZigBee/noise false detections do
not. Agreement is measured **only on the ZigBee-free outer subcarriers**:

```
        | Σ conj(X₀[k]) · X₁[k] |
M  =  ─────────────────────────────────      k ∈ [6,58],  k ∉ [28,36]
       √( Σ|X₀[k]|² · Σ|X₁[k]|² )
```

- `M ∈ [0,1]`; **≈ 1** for a real frame, **≈ 1/√N ≈ 0.15** for an uncorrelated false
  trigger (N ≈ 44 clean bins).
- Central bins `[LTF_VETO_BIN_LO..HI] = [28..36]` (DC + ZigBee core) are **excluded**.
- Accept iff `M ≥ LTF_VETO_THRESH = 0.25`. Reject → emit **NACK** (so the timeout-less
  ARQ peer retransmits instead of deadlocking) and skip the frame.
- Every frame's `M` and ACCEPT/REJECT is written to **`ltf_diag.txt`** unconditionally.

## 6. SIGNAL-field decode — three tiers (`general_work` ~305-352)

Only runs if the veto accepted. The SIGNAL field rides the same subcarriers as the LTF,
so it is just as exposed to ZigBee. Tried in order, stop at first success:

1. **Clean** — `decode_signal_field()`: equalize symbol 2 (BPSK), deinterleave,
   Viterbi, `parse_signal()`.
2. **ZigBee-cancel salvage** — `try_decode_signal_with_salvage()`: estimate the ZigBee
   channel `h`, subtract it from LTF+SIGNAL, re-equalize with a fresh equalizer,
   re-decode (see §8).
3. **Erasure** — `decode_signal_field_erased()`: mark the central ZigBee carriers as
   *unknown* (decoder value `2`) and let the code fill them in (see §9). Reference-free,
   last resort.

On success: `d_signal_valid = true`, frame metadata (bytes, encoding, SNR, CFO, CSI) is
packed into `d_pending_meta` for downstream tags. On total failure: **NACK**.

### `parse_signal` (~1068)

Reads the 18 decoded SIGNAL bits: 4-bit RATE → encoding/modulation/`N_DBPS`, 12-bit
LENGTH → `d_frame_bytes`, 1 parity bit (checked). Computes
`d_frame_symbols = ceil((16 + 8·bytes + 6) / N_DBPS)`. Rejects on wrong parity, unknown
rate, or oversize frame.

## 7. Payload decode — four tiers (`try_decode_with_salvage`, ~799)

Fires at the last payload symbol. **Every** accept is gated on the **32-bit CRC**
(`decode_payload`), so no wrong fill can produce a false ACK. Order:

1. **Clean** — equalize the whole captured frame with a fresh equalizer, deinterleave,
   Viterbi, descramble, CRC. → `PAYLOAD … CLEAN`
2. **ZigBee-cancel** — for each alignment candidate (sorted by score), subtract `h·ref`
   from all symbols, re-equalize, CRC. → `PAYLOAD … SALVAGE_CANCEL`
3. **Erasure (clean frame)** — erase central carriers (value 2), decode. →
   `PAYLOAD … ERASURE`
4. **Erasure + cancel** — subtract the best `h`, *then* erase, decode. →
   `PAYLOAD … ERASURE_CANCEL`

All fail → `PAYLOAD … FAIL score=…` + **NACK**. Each outcome line goes to
`ltf_diag.txt`; the running attempt/success/recovery-rate goes to
`zigbee_correction_stats.txt`.

### The CRC gate (`decode_payload`, ~1235)

After deinterleave → Viterbi → descramble, a `boost::crc_32_type` is run over the PSDU
and compared to the standard CRC-32 **residual `558161692`**. Any correctly-decoded
frame (regardless of content) yields this residue; a corrupted one practically never
does. This is the only trustworthy "frame recovered" signal in the block.

## 8. ZigBee cancellation mechanics (§6.2 / §7.2)

This path needs a **stored ZigBee reference waveform**, loaded at construction from
`examples/ofdm_zigbee/{ltf_fft1.csv, ltf_fft2.csv, wifi_rx_from_zigbee.csv}`
(`load_reference_data`). Because the ZigBee timing relative to the WiFi LTF is unknown,
the block searches a **raw-sample alignment**:

- `ZIGBEE_DEFAULT_LTF_START_RAW = 176`, `± ZIGBEE_SEARCH_RADIUS = 144` → up to
  **289 candidate offsets**.
- For each offset, `precompute_zigbee_reference_ffts()` has cached the reference's
  per-symbol 64-FFT (`compute_zigbee_reference_symbol_fft_uncached`, with fftshift).
- `estimate_zigbee_channel_for_offset()` estimates the ZigBee channel from the **DC
  bin only**: `h = (Σ y·conj(z)) / Σ|z|²`, with a normalized correlation **score**.
- Candidates are sorted by score; `subtract_zigbee_interference()` removes `h·ref_fft`
  across **all 64 bins** (so spectral leakage outside the core is cancelled too); the
  frame is re-equalized and re-decoded until the CRC passes.

> Caveat: this depends on a capture-specific stored reference and a DC-bin `h` estimate
> — it is the most fragile path. The erasure path (§9) was added precisely because it
> needs **none** of this.

## 9. Erasure mechanics (§6.3 / §7.3)

ZigBee occupies a *fixed, known* set of central subcarriers, so instead of recovering
them we declare them **erased** and let the rate-1/2 K=7 (`d_free = 10`) convolutional
code + interleaver fill them in. An erasure costs only 1 against `d_free` (a wrong guess
costs 2).

- `compute_erasure_carriers()` (ctor) builds `d_erasure_carriers` = the data-carrier
  indices whose FFT bin ∈ `[28..36]`, using the **same skip pattern as `ls.cc`** so the
  indices line up with the equalizer's output bits → carriers **{20..27}** (8 of 48).
- `deinterleave(erase_central=true)` overwrites those carriers' coded bits with value
  `2` **after** the 0/1 unpack and **before** the permutation (which preserves `2`).
- The Viterbi decoder treats input `2` as a neutral/erased branch (the same mechanism
  it uses for depuncturing). A subtle **both-erased** fix in the butterfly
  (`viterbi_decoder_x86.cc` / `_generic.cc`) keeps adjacent erased pairs neutral instead
  of producing a biased ~255 metric — see `erasure_decode_implementation.md §6`.

## 10. Output, tags, and feedback

- A successful payload decode stores the bits in `d_pending_output_bits` and the
  symbol count in `d_pending_output_items`; `flush_pending_output()` streams 48 bytes
  per symbol to the output over subsequent `general_work` calls, attaching the
  `d_pending_meta` tags (frame bytes, encoding, SNR, CFO, CSI) to the **first** output
  item.
- The equalized constellation points are published on the `symbols` message port
  (`publish_payload_symbols`) for plotting.
- **ACK/NACK** on `tx_feedback`: ACK on any payload CRC pass; NACK on veto reject,
  SIGNAL total failure, or payload total failure. (The ACK feedback into `seq_input`
  was removed in the flowgraph; `tx_feedback` currently drives only `message_debug`.)

## 11. Diagnostics written to disk

| file | written by | contents |
|---|---|---|
| `ltf_diag.txt` | every frame | `FRAME M=… ACCEPT/REJECT`, then `PAYLOAD M=… CLEAN/SALVAGE_CANCEL/ERASURE/ERASURE_CANCEL/FAIL` |
| `zigbee_correction_stats.txt` | each attempt | `correction_attempt_count`, `…_crc_success_count`, `recovery_rate`, last score/offset |
| signal dump (`signal_filename`) | on success | the two raw LTF symbols (64 bins each), `xx`-delimited |

`ltf_diag.txt` is **truncated on every run** (`std::ios::trunc` in the ctor) and lands
in the launch directory — hence running from `cfo_error_estimation/`.

## 12. Tunable constants (top of file)

| constant | value | meaning |
|---|---|---|
| `LTF_VETO_BIN_LO / HI` | `28 / 36` | central bins treated as ZigBee-occupied (excluded from veto, erased) |
| `LTF_VETO_THRESH` | `0.25` | accept frame iff clean-band `M` ≥ this |
| `ZIGBEE_DEFAULT_LTF_START_RAW` | `176` | center of the ZigBee alignment search |
| `ZIGBEE_SEARCH_RADIUS` | `144` | ± samples searched → 289 candidate offsets |
| `ZIGBEE_NFFT` | `64` | reference FFT size |

## 13. End-to-end flow (one frame)

```
wifi_start tag ─► reset, load ε₀
   │
   ├─ sym 0,1 (LTF) ─► SFO/phase correct ─► feed equalizer ─► capture
   │                                        └─ at sym 1: Layer-2 veto (M)
   │                                                       └─ REJECT ─► NACK, drop
   ├─ sym 2 (SIGNAL) ─► clean ─► (fail) cancel ─► (fail) erasure
   │                     └─ any OK ─► parse rate/length ─► d_signal_valid=true
   │                     └─ all fail ─► NACK, drop
   └─ sym 3…N+2 (payload, at last symbol):
        clean ─► cancel ─► erasure ─► erasure+cancel    (each CRC-gated)
            │ any OK ─► ACK ─► stream 48 B/symbol + tags ─► decode_mac
            └ all fail ─► NACK, drop
```

## 14. Known limits

- **ZigBee cancellation** relies on a capture-specific stored reference and a DC-bin
  `h`; it is the fragile path and was the source of earlier regressions.
- **Erasure** assumes ZigBee is confined to `[28..36]`. If ZigBee is wider/stronger and
  hits outer carriers, erasing only the center is insufficient — that is where
  cancellation (reduce errors `e`) and erasure (forgive erasures `f`) are complementary.
- The **SIGNAL** field is the marginal case (one symbol, 8 erasures vs `d_free=10`);
  the **payload** is comfortable (8 *spread* erasures per symbol).
- Only **BPSK rate-1/2** has been exercised on hardware; the erasure injection is
  written generally for `n_bpsc > 1` but higher-order modulation is untested.
```
