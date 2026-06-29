# ZigBee TX underflow — investigation handoff

**For the expert.** The ZigBee transmit USRP (`zigbee_tx`, addr 192.168.10.2) underflows
continuously, and the underflow **persists even at a very low packet rate (3 packets/s)**.
The WiFi TX (`wifi_tx`, 192.168.10.6) on the same host does not. We need the mechanism and
a fix. This document is deliberately split into **verified facts**, **ruled-out
explanations** (so you don't repeat them), the **leading hypothesis**, and **what to check
next**. Where something is a guess, it is labelled as such.

> Bigger picture: this flowgraph is a WiFi-under-ZigBee coexistence testbed. The full
> investigation (the WiFi decode side) is in `wifi_zigbee_expert_handoff.md`. **Note: that
> doc's §7 currently blames the WiFi-TX underflow for WiFi decode failures — that device
> attribution is wrong (see §2 here); the underflow is on the ZigBee TX. The causal link
> between this underflow and the WiFi decode failures is UNCONFIRMED (see §6).**

---

## 1. The flowgraph (the file actually run)

Run file: `examples/new_setup/sync/cfo_error_estimation/wifi_zigbee_sync_tx.py`
(launched as `python3 ./wifi_zigbee_sync_tx.py`). Three USRPs, one host:

| role | device | samp_rate | clock/time |
|---|---|---|---|
| ZigBee TX | `zigbee_tx`, 192.168.10.2 | `samp_rate_zig = 2e5` | **internal** (`set_time_unknown_pps(0)`) |
| WiFi TX | `wifi_tx`, 192.168.10.6 | `samp_rate = 2e6` | **internal** |
| RX/monitor | `uhd_usrp_source_0`, 192.168.10.7 | `2e6` | internal (`sync=pc_clock`) |

Both TX sinks are constructed **identically** with the length tag `'packet_len'` (UHD
tagged-stream / burst mode), `cpu_format=fc32`, 1 channel:

```python
self.zigbee_tx = uhd.usrp_sink("addr=192.168.10.2", stream_args(fc32,…), 'packet_len')
self.wifi_tx   = uhd.usrp_sink("addr=192.168.10.6", stream_args(fc32,…), 'packet_len')
```

**Connections (from the run file):**

```
# ZigBee TX (message-driven bursts):
blocks_message_strobe_0 ──strobe──▶ seq_input ──▶ crc16 ──txin(msg)──▶ ieee802_15_4_oqpsk_phy_0
ieee802_15_4_oqpsk_phy_0 ─out0(stream)─▶ zigbee_tx          # the underflowing sink

# WiFi TX (also message-driven bursts):
seq_input ──▶ crc32 ──mac_in(msg)──▶ wifi_phy_hier_0 ─out0(stream)─▶ wifi_tx   # NO underflow

# RX (continuous): uhd_usrp_source_0 ─▶ oqpsk_phy_0(in0) and ─▶ wifi_phy_hier_0(in0)
```

Both TX paths are **message-driven**: the modulator produces a burst of samples only when
a packet message arrives (driven by `blocks_message_strobe_0` at `pkt_rate`). Between
packets the modulator produces nothing, so the stream into each sink is **bursty with
gaps**. The ZigBee modulator hier block (`ieee802_15_4_oqpsk_phy`) contains a
**`foo_packet_pad2_0`** block (gr-foo burst padder); the snippet bumps its buffer
(`foo_packet_pad2_0.set_min_output_buffer(2**13)`), which by itself signals this block has
a history of buffer/burst trouble.

## 2. Verified facts (reproduced)

1. The underflow `U` markers come **only from `zigbee_tx`**: disabling the ZigBee PHY and
   its USRP removes **every** `U` from the log. So `wifi_tx` does not underflow.
2. The log shows e.g. `usrp_sink :error: In the last 751 ms, 22 underflows occurred`, with
   long `UUUU…` streams. No overflow, no late.
3. The underflow **persists at `pkt_rate = 3`** (3 packets/second). It is **not** sensitive
   to the data rate.
4. Both sinks are configured **identically** (`'packet_len'` burst mode, internal clock).
   The only differences are the sample rate (200k vs 2M) and the **TX PHY feeding them**
   (`ieee802_15_4_oqpsk_phy` vs `wifi_phy_hier`).
5. Clock/time is **internal** on all three devices (no `set_clock_source` /
   `set_time_source`; only `set_time_unknown_pps(uhd.time_spec(0))`).
6. No timed-burst tagger is active in the current run (`tx_time_tagger_0/1` resolve to
   `None` via `getattr` in the snippet, so the timed-burst scheduling code is inert).

## 3. Ruled-out explanations (do not re-investigate these)

- **Host can't keep up with the data rate (throughput).** Ruled out by fact 3: dropping
  `pkt_rate` 57→3 is ~19× less data and it still underflows. This is *not* a throughput
  problem.
- **ZigBee running near its max packet rate / no buffer margin.** An earlier hypothesis
  (ZigBee at 97% of `int(samp_rate_zig/3336)=59`); refuted by fact 3 (at `pkt_rate=3` it is
  ~5% capacity and still underflows).
- **External clock/PLL not locking.** The run file uses **internal** clock (fact 5). (The
  parent `.grc` has `clock_source=external`, but the generated run file does **not** apply
  it — check the `.grc` vs run file if you regenerate.)
- **It's the WiFi TX.** Ruled out by fact 1 (disabling ZigBee removes all underflow).

## 4. Leading hypothesis — message-driven burst gaps not cleanly terminated

A USRP sink emits samples to the air **continuously in real time**. When fed a **bursty**
stream (gaps between packets), it underflows during each gap **unless each burst is cleanly
terminated** so the device knows to go idle until the next burst (end-of-burst / EOB,
normally derived by the sink from the `packet_len` length tag).

This predicts **exactly** the observed behaviour:
- Underflow occurs in the inter-packet gaps, independent of the data rate (fact 3).
- **Lower `pkt_rate` → larger gaps → *more* underflow time**, not less. (Worth confirming
  quantitatively — see §5.1 — but it explains "still underflows at 3/s".)

The puzzle to resolve: **both sinks use the same `'packet_len'` burst mode, yet only the
ZigBee path underflows.** That means the **WiFi PHY emits cleanly length-tagged / EOB-
terminated bursts** (so `wifi_tx` idles cleanly in the gaps) while the **ZigBee PHY
(`ieee802_15_4_oqpsk_phy` + `foo_packet_pad2`) does not** (so `zigbee_tx` is left expecting
continuous samples and underflows in the gaps). Prime suspect: the ZigBee TX output's
**tagging** (is there a correct `packet_len` tag, and a terminating EOB, on every burst?)
and the **`foo_packet_pad2`** block.

> This is a hypothesis, not a proven cause. It fits every fact above, but it must be
> confirmed by inspecting the tags on the stream into `zigbee_tx` (§5).

## 5. What to check next (concrete)

### 5.1 Confirm it is gap-driven
Measure the underflow rate at `pkt_rate` = 3, 10, 57. The hypothesis predicts the **fraction
of time underflowing grows as `pkt_rate` drops** (bigger gaps). If instead the underflow is
roughly constant or unrelated to gap size, reconsider §4.

### 5.2 Look at the tags actually reaching `zigbee_tx`
Insert a `blocks.tag_debug` (or message/stream tag probe) on the stream **immediately
before `zigbee_tx`**, and the same before `wifi_tx`, and compare. For each ZigBee burst,
confirm:
- a `packet_len` tag with the correct sample count on the **first** sample of the burst;
- that the burst ends where the tag says (no trailing/continuous samples);
- whether UHD is getting a clean per-burst EOB (the sink derives this from `packet_len`).
If the ZigBee stream lacks a clean `packet_len` per burst (or `foo_packet_pad2` mangles
it), that is the bug.

### 5.3 Interrogate `foo_packet_pad2_0`
It is the ZigBee-path-specific block (inside `ieee802_15_4_oqpsk_phy`) and the snippet
already special-cases its buffer. Check its pad lengths and **tag propagation** (does it
preserve `packet_len`/length on the padded burst?). Try adjusting or bypassing it and see
whether the underflow changes. (This block has been a recurring point of confusion in this
project — "do I still need packet_pad2?".)

### 5.4 A/B the two PHYs
Since `wifi_phy_hier` does **not** underflow with the same sink config, diff the two TX
output stages (tagging, padding, how each terminates a burst). Port whatever the WiFi path
does correctly to the ZigBee path.

### 5.5 Decisive functional test
Feed `zigbee_tx` a **continuous** stream (e.g. a steady signal source / vector source at
`samp_rate_zig`, no message bursts) and confirm the underflow **disappears**. If it does,
the problem is conclusively the **bursting/termination**, not the device or link.

## 6. Why it may (or may not) matter — important caveat

The reason underflow was being chased is a separate finding: ~10% of WiFi frames fail to
decode with their **last 3–4 OFDM symbols faded**, even with ZigBee turned down. We
*hypothesised* the underflow was truncating those tails — but that hypothesis is now shaky,
because the underflow is on the **ZigBee** TX, and the **WiFi** TX does not underflow.

**Unconfirmed and worth settling early:** does this ZigBee-TX underflow actually harm the
experiment at all, or is it mostly log noise?
- The decisive test: **run with ZigBee fully disabled (block + USRP) and re-run the WiFi
  decode analysis** (`analyze_captured_frames.py`). If the ~10% WiFi failures and the
  end-of-frame fade **disappear**, then the ZigBee-TX underflow was indirectly hurting WiFi
  (e.g. via shared-host scheduling) and fixing it matters. If the WiFi failures **persist**,
  the underflow is a ZigBee-side cosmetic/secondary issue and the WiFi failure has another
  cause (investigate the WiFi TX burst tail / `wifi_phy_hier` padding instead).
- Either way: for the coexistence research, ZigBee is only an **interferer**; underflow
  gaps mean ZigBee transmits with glitches at burst boundaries but still interferes. So fix
  the underflow for clean experiments, but first confirm (above) whether it is on the
  critical path for the WiFi-decode question.

## 7. Reproduce / observe

```bash
cd /home/kk/gnuradio/gr-ieee802-11/examples/new_setup/sync/cfo_error_estimation
python3 ./wifi_zigbee_sync_tx.py 2>&1 | tee run.log      # Ctrl-C to stop
grep -c underflow run.log                                # count underflow report lines
grep -m3 underflow run.log                               # sample ("In the last … N underflows")
# vary pkt_rate (set in the flowgraph variable) to 3 / 10 / 57 and compare the rate
```

Key knobs: `pkt_rate` (burst rate, the message-strobe period), `samp_rate_zig` (200k),
and the ZigBee TX chain blocks `ieee802_15_4_oqpsk_phy_0` / `foo_packet_pad2_0`.

## 8. File pointers

- Run file (current state, ZigBee internal clock, both sinks `'packet_len'`):
  `examples/new_setup/sync/cfo_error_estimation/wifi_zigbee_sync_tx.py`
  — sink construction lines ~273–302; connections lines ~338–350; snippet lines ~73–91.
- Source flowgraph (full design, **note: parent `.grc` has external clock that the run file
  does not apply**): `examples/new_setup/sync/wifi_zigbee_sync_tx.grc`.
- ZigBee PHY hier block (TX modulator + `foo_packet_pad2`): the generated
  `ieee802_15_4_oqpsk_phy.py` (GRC hier block on the import path).
- WiFi PHY hier block (the TX path that does NOT underflow, for A/B): `wifi_phy_hier.py`.
- Companion / bigger context: `wifi_zigbee_expert_handoff.md` (WiFi decode side; see the
  correction note in §1 above).

## 9. Author's note on confidence

Several earlier explanations for this underflow were proposed and then disproved by the
data (host-throughput, 97%-rate-margin, external-clock, WiFi-TX). They are recorded in §3
specifically so they are not retried. The §4 hypothesis (bursty stream not cleanly
EOB-terminated on the ZigBee path) is the one consistent with all current facts, but it is
**not yet verified at the tag level** — §5.2 is the experiment that will confirm or refute
it.
```
