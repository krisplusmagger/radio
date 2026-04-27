/*
 * Copyright (C) 2016 Bastian Bloessl <bloessl@ccs-labs.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "equalizer/base.h"
#include "equalizer/comb.h"
#include "equalizer/lms.h"
#include "equalizer/ls.h"
#include "equalizer/sta.h"
#include "frame_equalizer_impl.h"
#include "utils.h"
#include <boost/crc.hpp>
#include <gnuradio/io_signature.h>

namespace gr {
namespace ieee802_11 {

frame_equalizer::sptr
frame_equalizer::make(Equalizer algo, double freq, double bw, bool log, bool debug, const std::string& signal_filename)
{
    return gnuradio::get_initial_sptr(
        new frame_equalizer_impl(algo, freq, bw, log, debug, signal_filename));
}


frame_equalizer_impl::frame_equalizer_impl(
    Equalizer algo, double freq, double bw, bool log, bool debug, const std::string& signal_filename)
    : gr::block("frame_equalizer",
                gr::io_signature::make(1, 1, 64 * sizeof(gr_complex)),
                gr::io_signature::make(1, 1, 48)),
      d_current_symbol(0),
      d_log(log),
      d_debug(debug),
      d_equalizer(NULL),
      d_freq(freq),
      d_bw(bw),
      d_frame_bytes(0),
      d_frame_symbols(0),
      d_ofdm(BPSK_1_2),
      d_frame(d_ofdm, 0),
      d_freq_offset_from_synclong(0.0),
      d_signal_symbols_pending(false)
{


    std::ofstream(signal_filename).close();
    signal_file.open(signal_filename, std::ios::out | std::ios::app);

    if(!signal_file.is_open()) {
        throw std::runtime_error("Failed to open signal files: " + signal_filename);
    }
    message_port_register_out(pmt::mp("symbols"));
    message_port_register_out(pmt::mp("tx_feedback"));

    d_bpsk = constellation_bpsk::make();
    d_qpsk = constellation_qpsk::make();
    d_16qam = constellation_16qam::make();
    d_64qam = constellation_64qam::make();

    d_frame_mod = d_bpsk;

    set_tag_propagation_policy(block::TPP_DONT);
    set_algorithm(algo);
}

frame_equalizer_impl::~frame_equalizer_impl() {}


void frame_equalizer_impl::set_algorithm(Equalizer algo)
{
    gr::thread::scoped_lock lock(d_mutex);
    delete d_equalizer;

    switch (algo) {

    case COMB:
        dout << "Comb" << std::endl;
        d_equalizer = new equalizer::comb();
        break;
    case LS:
        dout << "LS" << std::endl;
        d_equalizer = new equalizer::ls();
        break;
    case LMS:
        dout << "LMS" << std::endl;
        d_equalizer = new equalizer::lms();
        break;
    case STA:
        dout << "STA" << std::endl;
        d_equalizer = new equalizer::sta();
        break;
    default:
        throw std::runtime_error("Algorithm not implemented");
    }
}

void frame_equalizer_impl::set_bandwidth(double bw)
{
    gr::thread::scoped_lock lock(d_mutex);
    d_bw = bw;
}

void frame_equalizer_impl::set_frequency(double freq)
{
    gr::thread::scoped_lock lock(d_mutex);
    d_freq = freq;
}

void frame_equalizer_impl::forecast(int noutput_items,
                                    gr_vector_int& ninput_items_required)
{
    ninput_items_required[0] = noutput_items;
}

int frame_equalizer_impl::general_work(int noutput_items,
                                       gr_vector_int& ninput_items,
                                       gr_vector_const_void_star& input_items,
                                       gr_vector_void_star& output_items)
{

    gr::thread::scoped_lock lock(d_mutex);

    const gr_complex* in = (const gr_complex*)input_items[0];
    uint8_t* out = (uint8_t*)output_items[0];

    int i = 0;
    int o = 0;

    gr_complex symbols[48];
    gr_complex current_symbol[64];

    dout << "FRAME EQUALIZER: input " << ninput_items[0] << "  output " << noutput_items
         << std::endl;

    while ((i < ninput_items[0]) && (o < noutput_items)) {

        get_tags_in_window(tags, 0, i, i + 1, pmt::string_to_symbol("wifi_start"));

        // new frame
        if (tags.size()) {
            d_current_symbol = 0;
            d_frame_symbols = 0;
            d_frame_mod = d_bpsk;
            d_signal_symbols_pending = false;

            d_freq_offset_from_synclong =
                pmt::to_double(tags.front().value) * d_bw / (2 * M_PI);
            d_epsilon0 = pmt::to_double(tags.front().value) * d_bw / (2 * M_PI * d_freq);
            d_er = 0;

            dout << "epsilon: " << d_epsilon0 << std::endl;
        }

        // not interesting -> skip
        if (d_current_symbol > (d_frame_symbols + 2)) {
            i++;
            continue;
        }

        std::memcpy(current_symbol, in + i * 64, 64 * sizeof(gr_complex));

        // compensate sampling offset
        for (int i = 0; i < 64; i++) {
            current_symbol[i] *= exp(gr_complex(0,
                                                2 * M_PI * d_current_symbol * 80 *
                                                    (d_epsilon0 + d_er) * (i - 32) / 64));
        }

        gr_complex p = equalizer::base::POLARITY[(d_current_symbol - 2) % 127];

        double beta;
        if (d_current_symbol < 2) {
            beta = arg(current_symbol[11] - current_symbol[25] + current_symbol[39] +
                       current_symbol[53]);

        } else {
            beta = arg((current_symbol[11] * p) + (current_symbol[39] * p) +
                       (current_symbol[25] * p) + (current_symbol[53] * -p));
        }

        double er = arg((conj(d_prev_pilots[0]) * current_symbol[11] * p) +
                        (conj(d_prev_pilots[1]) * current_symbol[25] * p) +
                        (conj(d_prev_pilots[2]) * current_symbol[39] * p) +
                        (conj(d_prev_pilots[3]) * current_symbol[53] * -p));

        er *= d_bw / (2 * M_PI * d_freq * 80);

        if (d_current_symbol < 2) {
            d_prev_pilots[0] = current_symbol[11];
            d_prev_pilots[1] = -current_symbol[25];
            d_prev_pilots[2] = current_symbol[39];
            d_prev_pilots[3] = current_symbol[53];
        } else {
            d_prev_pilots[0] = current_symbol[11] * p;
            d_prev_pilots[1] = current_symbol[25] * p;
            d_prev_pilots[2] = current_symbol[39] * p;
            d_prev_pilots[3] = current_symbol[53] * -p;
        }

        // compensate residual frequency offset
        for (int i = 0; i < 64; i++) {
            current_symbol[i] *= exp(gr_complex(0, -beta));
        }

        // update estimate of residual frequency offset
        if (d_current_symbol >= 2) {

            double alpha = 0.1;
            d_er = (1 - alpha) * d_er + alpha * er;
        }


 

        // do equalization
        d_equalizer->equalize(
            current_symbol, d_current_symbol, symbols, out + o * 48, d_frame_mod); // write the equalized bits to out

        //  add anohter pointer gr_complex* out_zigbee
        // for i=0...5, out_zigbee[i] = current_symbol[i], then remain the rest, wer got the new array filled with the data from the zigbee //
        //and in frame_equyalizer_impl.cc we can access that array and create another output port
        // here if i change it to 64, does that mean it will copy 64 data at one time?
        // std::memcpy(out_zigbee + d_current_symbol * 64, current_symbol,  64 * sizeof(gr_complex));
        // int o_zigbee = 0;
        if (d_current_symbol < 2) {
            std::memcpy(d_saved_signal_symbols + d_current_symbol * 64,
                        current_symbol,
                        64 * sizeof(gr_complex));
            d_signal_symbols_pending = true;
        }
  
        // signal field
        if (d_current_symbol == 2) {

            if (decode_signal_field(out + o * 48)) {

                pmt::pmt_t dict = pmt::make_dict();
                dict = pmt::dict_add(
                    dict, pmt::mp("frame bytes"), pmt::from_uint64(d_frame_bytes));
                dict = pmt::dict_add(
                    dict, pmt::mp("encoding"), pmt::from_uint64(d_frame_encoding));
                dict = pmt::dict_add(
                    dict, pmt::mp("snr"), pmt::from_double(d_equalizer->get_snr()));
                dict = pmt::dict_add(
                    dict, pmt::mp("nominal frequency"), pmt::from_double(d_freq));
                dict = pmt::dict_add(dict,
                                     pmt::mp("frequency offset"),
                                     pmt::from_double(d_freq_offset_from_synclong));
                dict = pmt::dict_add(dict, pmt::mp("beta"), pmt::from_double(beta));

                std::vector<gr_complex> csi = d_equalizer->get_csi();
                dict = pmt::dict_add(
                    dict, pmt::mp("csi"), pmt::init_c32vector(csi.size(), csi));

                pmt::pmt_t pairs = pmt::dict_items(dict);
                for (int i = 0; i < pmt::length(pairs); i++) {
                    pmt::pmt_t pair = pmt::nth(i, pairs);
                    add_item_tag(0,
                                 nitems_written(0) + o,
                                 pmt::car(pair),
                                 pmt::cdr(pair),
                                 alias_pmt());
                }
            } else if (d_signal_symbols_pending) {
                message_port_pub(pmt::mp("tx_feedback"), pmt::intern("nack"));
                d_signal_symbols_pending = false;
            }
        }

        if (d_current_symbol > 2) {
            std::memcpy(d_rx_symbols + (d_current_symbol - 3) * 48, out + o * 48, 48);

            if (d_current_symbol == d_frame_symbols + 2) {
                if (decode_payload()) {
                    write_signal_symbols();
                } else {
                    d_signal_symbols_pending = false;
                }
            }

            o++;
            pmt::pmt_t pdu = pmt::make_dict();
            message_port_pub(
                pmt::mp("symbols"),
                pmt::cons(pmt::make_dict(), pmt::init_c32vector(48, symbols)));
        }

        i++;
        d_current_symbol++;
    }

    consume(0, i);
    return o;
}

bool frame_equalizer_impl::decode_signal_field(uint8_t* rx_bits)
{

    static ofdm_param ofdm(BPSK_1_2);
    static frame_param frame(ofdm, 0);

    deinterleave(rx_bits);
    uint8_t* decoded_bits = d_decoder.decode(&ofdm, &frame, d_deinterleaved);

    return parse_signal(decoded_bits);
}

void frame_equalizer_impl::deinterleave(uint8_t* rx_bits)
{
    for (int i = 0; i < 48; i++) {
        d_deinterleaved[i] = rx_bits[interleaver_pattern[i]];
    }
}

bool frame_equalizer_impl::parse_signal(uint8_t* decoded_bits)
{

    int r = 0;
    d_frame_bytes = 0;
    bool parity = false;
    for (int i = 0; i < 17; i++) {
        parity ^= decoded_bits[i];

        if ((i < 4) && decoded_bits[i]) {
            r = r | (1 << i);
        }

        if (decoded_bits[i] && (i > 4) && (i < 17)) {
            d_frame_bytes = d_frame_bytes | (1 << (i - 5));
        }
    }

    if (parity != decoded_bits[17]) {
        dout << "SIGNAL: wrong parity" << std::endl;
        return false;
    }

    switch (r) {
    case 11:
        d_frame_encoding = 0;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)24);
        d_frame_mod = d_bpsk;
        dout << "Encoding: 3 Mbit/s   ";
        break;
    case 15:
        d_frame_encoding = 1;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)36);
        d_frame_mod = d_bpsk;
        dout << "Encoding: 4.5 Mbit/s   ";
        break;
    case 10:
        d_frame_encoding = 2;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)48);
        d_frame_mod = d_qpsk;
        dout << "Encoding: 6 Mbit/s   ";
        break;
    case 14:
        d_frame_encoding = 3;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)72);
        d_frame_mod = d_qpsk;
        dout << "Encoding: 9 Mbit/s   ";
        break;
    case 9:
        d_frame_encoding = 4;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)96);
        d_frame_mod = d_16qam;
        dout << "Encoding: 12 Mbit/s   ";
        break;
    case 13:
        d_frame_encoding = 5;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)144);
        d_frame_mod = d_16qam;
        dout << "Encoding: 18 Mbit/s   ";
        break;
    case 8:
        d_frame_encoding = 6;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)192);
        d_frame_mod = d_64qam;
        dout << "Encoding: 24 Mbit/s   ";
        break;
    case 12:
        d_frame_encoding = 7;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)216);
        d_frame_mod = d_64qam;
        dout << "Encoding: 27 Mbit/s   ";
        break;
    default:
        dout << "unknown encoding" << std::endl;
        return false;
    }

    ofdm_param ofdm = ofdm_param((Encoding)d_frame_encoding);
    frame_param frame = frame_param(ofdm, d_frame_bytes);

    if (frame.n_sym > MAX_SYM || frame.psdu_size > MAX_PSDU_SIZE) {
        dout << "Dropping frame which is too large (symbols or bits)" << std::endl;
        d_frame_symbols = 0;
        d_signal_symbols_pending = false;
        return false;
    }

    d_ofdm = ofdm;
    d_frame = frame;
    d_frame_symbols = d_frame.n_sym;

    mylog("encoding: {} - length: {} - symbols: {}",
          d_frame_encoding,
          d_frame_bytes,
          d_frame_symbols);
    return true;
}

void frame_equalizer_impl::deinterleave()
{
    int n_cbps = d_ofdm.n_cbps;
    int first[MAX_BITS_PER_SYM];
    int second[MAX_BITS_PER_SYM];
    int s = std::max(d_ofdm.n_bpsc / 2, 1);

    for (int j = 0; j < n_cbps; j++) {
        first[j] = s * (j / s) + ((j + int(floor(16.0 * j / n_cbps))) % s);
    }

    for (int i = 0; i < n_cbps; i++) {
        second[i] = 16 * i - (n_cbps - 1) * int(floor(16.0 * i / n_cbps));
    }

    for (int i = 0; i < d_frame.n_sym * 48; i++) {
        for (int k = 0; k < d_ofdm.n_bpsc; k++) {
            d_rx_bits[i * d_ofdm.n_bpsc + k] = !!(d_rx_symbols[i] & (1 << k));
        }
    }

    for (int i = 0; i < d_frame.n_sym; i++) {
        for (int k = 0; k < n_cbps; k++) {
            d_deinterleaved_bits[i * n_cbps + second[first[k]]] =
                d_rx_bits[i * n_cbps + k];
        }
    }
}

void frame_equalizer_impl::descramble(uint8_t* decoded_bits)
{
    int state = 0;
    std::memset(out_bytes, 0, d_frame.psdu_size + 2);

    for (int i = 0; i < 7; i++) {
        if (decoded_bits[i]) {
            state |= 1 << (6 - i);
        }
    }
    out_bytes[0] = state;

    int feedback;
    int bit;

    for (int i = 7; i < d_frame.psdu_size * 8 + 16; i++) {
        feedback = ((!!(state & 64))) ^ (!!(state & 8));
        bit = feedback ^ (decoded_bits[i] & 0x1);
        out_bytes[i / 8] |= bit << (i % 8);
        state = ((state << 1) & 0x7e) | feedback;
    }
}

bool frame_equalizer_impl::decode_payload()
{
    deinterleave();
    uint8_t* decoded = d_decoder.decode(&d_ofdm, &d_frame, d_deinterleaved_bits);
    descramble(decoded);

    boost::crc_32_type result;
    result.process_bytes(out_bytes + 2, d_frame.psdu_size);

    if (result.checksum() != 558161692) {
        dout << "payload checksum wrong -- dropping saved signal symbols" << std::endl;
        message_port_pub(pmt::mp("tx_feedback"), pmt::intern("nack"));
        return false;
    }

    message_port_pub(pmt::mp("tx_feedback"), pmt::intern("ack"));
    return true;
}

void frame_equalizer_impl::write_signal_symbols()
{
    if (!d_signal_symbols_pending) {
        return;
    }

    for (int symbol = 0; symbol < 2; symbol++) {
        for (int i = 0; i < 64; i++) {
            signal_file << d_saved_signal_symbols[symbol * 64 + i] << "\n";
        }
        signal_file << "xx"
                    << "\n";
    }
    signal_file.flush();
    d_signal_symbols_pending = false;
}

const int frame_equalizer_impl::interleaver_pattern[48] = {
    0, 3, 6, 9,  12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45,
    1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47
};

} /* namespace ieee802_11 */
} /* namespace gr */
