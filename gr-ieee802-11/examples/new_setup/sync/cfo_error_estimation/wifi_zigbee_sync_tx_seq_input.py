from gnuradio import gr
import pmt
import time


class blk(gr.basic_block):
    def __init__(self):
        gr.basic_block.__init__(self, name="seq_pdu", in_sig=None, out_sig=None)

        self.message_port_register_in(pmt.intern("in"))
        self.set_msg_handler(pmt.intern("in"), self.handle_trigger)
        self.message_port_register_out(pmt.intern("out"))

        # Fixed payload, sent identically on every trigger. No ACK/NACK and no
        # sequence increment: this flowgraph only verifies that the recovery
        # algorithm decodes the packet, so one constant known payload is enough.
        # Keep the original payload format ("NNNNNN TEXT").
        self.payload = b"000001 HELLO WORLD"
        self.total_tx_count = 0
        self.start_time = None
        self.stats_filename = "seq_pdu_stats.txt"
        # Write the stats file at most once per this many triggers. Writing it
        # on every trigger means synchronous disk I/O ~40x/s, which adds timing
        # jitter that can push timed bursts past their tx_time (Late bursts).
        self.stats_every = 40

    def handle_trigger(self, msg):
        if self.start_time is None:
            self.start_time = time.monotonic()

        payload = pmt.init_u8vector(len(self.payload), list(self.payload))
        pdu = pmt.cons(pmt.make_dict(), payload)
        self.message_port_pub(pmt.intern("out"), pdu)

        self.total_tx_count += 1
        if self.total_tx_count % self.stats_every == 0:
            self.write_stats()

    def write_stats(self):
        elapsed = 0.0 if self.start_time is None else time.monotonic() - self.start_time
        with open(self.stats_filename, "w", encoding="ascii") as f:
            f.write(f"payload={self.payload.decode('ascii')}\n")
            f.write(f"total_tx_count={self.total_tx_count}\n")
            f.write(f"elapsed_seconds={elapsed:.6f}\n")
