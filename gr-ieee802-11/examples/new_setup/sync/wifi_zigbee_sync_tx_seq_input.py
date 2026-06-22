"""
Embedded Python Blocks:

Each time this file is saved, GRC will instantiate the first class it finds
to get ports and parameters of your block. The arguments to __init__  will
be the parameters. All of them are required to have default values!
"""

import numpy as np
from gnuradio import gr
import pmt

class blk(gr.basic_block):  # other base classes are basic_block, decim_block, interp_block
    """Embedded Python Block example - a simple multiply const"""

    def __init__(self):  # only default arguments here
        """arguments to this function show up as parameters in GRC"""
        gr.basic_block.__init__(
            self,
            name='seq_pdu',   # will show up in GRC
            in_sig=None,
            out_sig=None
        )
        # if an attribute with the same name as a parameter is found,
        # a callback is registered (properties work, too).
        self.message_port_register_in(pmt.intern("in"))
        self.set_msg_handler(pmt.intern("in"), self.handle)
        self.message_port_register_out(pmt.intern("out"))
        self.seq = 0


    def handle(self, msg):
        self.seq += 1
        s = f"{self.seq:06d} HELLO WORLD"
        payload = pmt.init_u8vector(len(s.encode()), list(s.encode()))
        pdu = pmt.cons(pmt.make_dict(), payload)
        self.message_port_pub(pmt.intern("out"), pdu)
