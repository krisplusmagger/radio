import pmt
import numpy as np
from gnuradio import gr
class blk(gr.basic_block):
    def __init__(self):
        gr.basic_block.__init__(self, name="pdu_to_text", in_sig=None, out_sig=None)
        self.message_port_register_in(pmt.intern("in"))
        self.set_msg_handler(pmt.intern("in"), self.handle)
        self.message_port_register_out(pmt.intern("out"))

    def handle(self, msg):
        # msg is a PDU: (meta . u8vector)
        meta = pmt.car(msg)
        vec  = pmt.cdr(msg)

        # convert u8vector -> Python bytes
        data = bytes(pmt.u8vector_elements(vec))

        # try decode as UTF-8/ASCII
        try:
            text = data.decode("utf-8", errors="replace")
        except Exception:
            text = str(data)

        print("RX TEXT:", text)
        self.message_port_pub(pmt.intern("out"), msg)
