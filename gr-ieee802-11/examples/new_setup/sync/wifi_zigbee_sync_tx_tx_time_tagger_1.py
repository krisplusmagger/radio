import numpy as np
from gnuradio import gr
import pmt
import sys
import types
import time
import threading

# Timed-burst TX with a DETERMINISTIC shared schedule.
#
# One trigger fans out to both the WiFi path (fast, 2 MHz) and the ZigBee path
# (slow, 200 kHz), so the two tagger instances are NOT in lockstep. The old
# "two claims within a time window" pairing therefore misfired: the fast tagger
# stamped two of its own bursts with the same tx_time (collision -> Late) and a
# batch processed in one work() got near-identical timestamps the radio could
# not honor.
#
# Instead, every burst k gets tx_time = base + k*period:
#   * base   : a single shared instant in the RADIO clock domain, set once
#              (device_now + start_lead) on the first burst.
#   * period : the strobe period (1/pkt_rate), shared by both taggers.
#   * k      : a per-tagger burst counter.
# Both paths emit one burst per trigger in order, so tagger_0's burst k and
# tagger_1's burst k are the same logical packet -> identical tx_time ->
# the two radios fire together. Consecutive k differ by exactly period, so
# bursts never collide and batching is harmless. The payload is identical on
# every packet, so even a one-packet counter skew keeps the radios coincident.
#
# tx_time lives in the radio's own clock domain (set_time_source), so there is
# no host/radio drift to fight; start_lead just has to exceed the slow path's
# pipeline latency.
_MOD = "_txsync_shared"
if _MOD in sys.modules:
    _sh = sys.modules[_MOD]
else:
    _sh = types.ModuleType(_MOD)
    _sh.lock = threading.Lock()
    _sh.time_fn = time.time   # replaced by the radio clock via set_time_source
    _sh.base = None           # shared schedule origin (radio-clock seconds)
    _sh.period = 0.025        # seconds between bursts; set via set_schedule
    _sh.start_lead = 1.0      # seconds from first-burst stamp to first tx
    sys.modules[_MOD] = _sh

# A shared module left over from an earlier run in the same interpreter may
# predate these attributes; make sure they always exist.
for _name, _default in (("time_fn", time.time), ("base", None),
                        ("period", 0.025), ("start_lead", 1.0)):
    if not hasattr(_sh, _name):
        setattr(_sh, _name, _default)


class blk(gr.sync_block):
    def __init__(self, lead=0.05, window=0.06):
        gr.sync_block.__init__(self, name="tx_time_tagger",
                               in_sig=[np.complex64], out_sig=[np.complex64])
        # lead/window are accepted for a backward-compatible constructor
        # signature but are no longer used; scheduling is base + k*period.
        self._k = 0
        self.len_key = pmt.intern("packet_len")
        self.time_key = pmt.intern("tx_time")

    def set_time_source(self, fn):
        # Register a callable returning the radio's current time in seconds,
        # e.g. lambda: usrp.get_time_now().get_real_secs().
        with _sh.lock:
            _sh.time_fn = fn

    def set_schedule(self, period, start_lead=1.0):
        # period: seconds between bursts (use the strobe period 1/pkt_rate).
        # start_lead: radio-clock seconds from the first burst to its tx.
        with _sh.lock:
            _sh.period = float(period)
            _sh.start_lead = float(start_lead)

    def work(self, input_items, output_items):
        n = len(input_items[0])
        output_items[0][:] = input_items[0]
        for tg in self.get_tags_in_window(0, 0, n, self.len_key):
            with _sh.lock:
                if _sh.base is None:
                    _sh.base = _sh.time_fn() + _sh.start_lead
                base = _sh.base
                period = _sh.period
            t = base + self._k * period
            self._k += 1
            full = int(t)
            frac = float(t - full)
            val = pmt.make_tuple(pmt.from_uint64(full), pmt.from_double(frac))
            self.add_item_tag(0, tg.offset, self.time_key, val)
        return n
