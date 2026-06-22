import numpy as np
from gnuradio import gr
import pmt
import sys
import types
import time
import threading

# Timed-burst TX: stamp an absolute tx_time on the first sample of each
# packet_len burst so the radio fires it at an exact instant. A process-wide
# coincidence scheduler hands the WiFi burst and its ZigBee twin the SAME
# tx_time so the two radios transmit together. LEAD/WINDOW come in as block
# parameters (set them from the tx_lead / tx_window variables); they MUST be
# identical in both tagger instances.
#
# IMPORTANT: tx_time is computed from the RADIO clock, not the host wall clock.
# The USRP enforces tx_time on its own time base (locked to the external
# 10 MHz / PPS reference). Scheduling off host time.time() lets the two clocks
# drift apart; once cumulative drift (or an NTP step) exceeds tx_lead, every
# burst lands in the past -> UHD "cmd time error" / Late ('L') and TX silently
# dies. Reading the radio's own clock keeps the deadline in the exact domain
# the radio checks against, so there is nothing left to drift. The flowgraph
# registers the clock via set_time_source(); until then we fall back to
# time.time() so the block still works standalone.
_MOD = "_txsync_shared"
if _MOD in sys.modules:
    _sh = sys.modules[_MOD]
else:
    _sh = types.ModuleType(_MOD)
    _sh.lock = threading.Lock()
    _sh.tx_time = None
    _sh.claims = 0
    _sh.stamp = 0.0
    _sh.time_fn = time.time
    sys.modules[_MOD] = _sh

# A shared module left over from an earlier run in the same interpreter may
# predate time_fn; make sure it always exists.
if not hasattr(_sh, "time_fn"):
    _sh.time_fn = time.time


def _next_tx_time(lead, window):
    with _sh.lock:
        now = _sh.time_fn()
        if (_sh.tx_time is not None and _sh.claims < 2
                and (now - _sh.stamp) < window):
            _sh.claims += 1
            return _sh.tx_time
        t = now + lead
        _sh.tx_time = t
        _sh.claims = 1
        _sh.stamp = now
        return t


class blk(gr.sync_block):
    def __init__(self, lead=0.05, window=0.06):
        gr.sync_block.__init__(self, name="tx_time_tagger",
                               in_sig=[np.complex64], out_sig=[np.complex64])
        self._lead = lead
        self._window = window
        self.len_key = pmt.intern("packet_len")
        self.time_key = pmt.intern("tx_time")

    def set_time_source(self, fn):
        # Register a callable returning the radio's current time in seconds,
        # e.g. lambda: usrp.get_time_now().get_real_secs(). Stored in the
        # shared module so both tagger instances schedule off one clock domain.
        with _sh.lock:
            _sh.time_fn = fn

    def work(self, input_items, output_items):
        n = len(input_items[0])
        output_items[0][:] = input_items[0]
        for tg in self.get_tags_in_window(0, 0, n, self.len_key):
            t = _next_tx_time(self._lead, self._window)
            full = int(t)
            frac = float(t - full)
            val = pmt.make_tuple(pmt.from_uint64(full), pmt.from_double(frac))
            self.add_item_tag(0, tg.offset, self.time_key, val)
        return n
