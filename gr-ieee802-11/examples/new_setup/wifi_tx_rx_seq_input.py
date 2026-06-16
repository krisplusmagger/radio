from gnuradio import gr
import pmt
import time


class blk(gr.basic_block):
    def __init__(self):
        gr.basic_block.__init__(self, name="seq_pdu", in_sig=None, out_sig=None)

        self.message_port_register_in(pmt.intern("in"))
        self.set_msg_handler(pmt.intern("in"), self.handle_trigger)
        self.message_port_register_in(pmt.intern("feedback"))
        self.set_msg_handler(pmt.intern("feedback"), self.handle_feedback)
        self.message_port_register_out(pmt.intern("out"))

        self.max_seq = 10000
        self.seq = 0
        self.current_payload = None  # current_payload will set after first message from in port
        self.stopped = False
        self.total_tx_count = 0
        self.ack_count = 0
        self.nack_count = 0
        self.tx_payload_bytes = 0
        self.acked_payload_bytes = 0
        self.start_time = None
        self.end_time = None
        self.stats_filename = "seq_pdu_stats.txt"

    def handle_trigger(self, msg):
        if self.stopped or self.current_payload is not None:
            return

        self.seq += 1
        self.current_payload = self.build_payload(self.seq)
        self.publish_current_payload()

    def handle_feedback(self, msg):
        if self.current_payload is None:
            return

        if not pmt.is_symbol(msg):
            return

        feedback = pmt.symbol_to_string(msg)

        if feedback == "ack":
            self.ack_count += 1
            self.acked_payload_bytes += len(self.current_payload)
            if self.seq >= self.max_seq:
                self.current_payload = None
                self.stopped = True
                self.end_time = time.monotonic()
                self.write_stats()
                return
            self.seq += 1
            self.current_payload = self.build_payload(self.seq)
            self.publish_current_payload()
        elif feedback == "nack":
            self.nack_count += 1
            self.publish_current_payload()

    def build_payload(self, seq):
        return f"{seq:06d} HELLO WORLD".encode()

    def publish_current_payload(self):
        if self.start_time is None:
            self.start_time = time.monotonic()

        payload = pmt.init_u8vector(len(self.current_payload), list(self.current_payload))
        pdu = pmt.cons(pmt.make_dict(), payload)
        self.message_port_pub(pmt.intern("out"), pdu)

        self.total_tx_count += 1
        self.tx_payload_bytes += len(self.current_payload)
        self.write_stats()

    def write_stats(self):
        now = self.end_time if self.end_time is not None else time.monotonic()
        elapsed_seconds = 0.0 if self.start_time is None else now - self.start_time
        throughput_bps = (
            self.acked_payload_bytes * 8 / elapsed_seconds
            if elapsed_seconds > 0
            else 0.0
        )
        tx_rate_bps = (
            self.tx_payload_bytes * 8 / elapsed_seconds
            if elapsed_seconds > 0
            else 0.0
        )

        with open(self.stats_filename, "w", encoding="ascii") as stats_file:
            stats_file.write(f"current_seq={self.seq:06d}\n")
            stats_file.write(f"max_seq={self.max_seq:06d}\n")
            stats_file.write(f"stopped={int(self.stopped)}\n")
            stats_file.write(f"total_tx_count={self.total_tx_count}\n")
            stats_file.write(f"ack_count={self.ack_count}\n")
            stats_file.write(f"nack_count={self.nack_count}\n")
            stats_file.write(f"tx_payload_bytes={self.tx_payload_bytes}\n")
            stats_file.write(f"acked_payload_bytes={self.acked_payload_bytes}\n")
            stats_file.write(f"elapsed_seconds={elapsed_seconds:.6f}\n")
            stats_file.write(f"throughput_bps={throughput_bps:.3f}\n")
            stats_file.write(f"tx_rate_bps={tx_rate_bps:.3f}\n")
