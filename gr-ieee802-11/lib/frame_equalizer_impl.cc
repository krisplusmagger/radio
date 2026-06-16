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
#include <algorithm>
#include <boost/crc.hpp>
#include <fstream>
#include <gnuradio/io_signature.h>
#include <memory>
#include <sstream>

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
      d_algorithm(algo),
      d_freq(freq),
      d_bw(bw),
      d_frame_bytes(0),
      d_frame_symbols(0),
      d_ofdm(BPSK_1_2),
      d_frame(d_ofdm, 0),
      d_freq_offset_from_synclong(0.0),
      d_signal_symbols_pending(false),
      d_signal_valid(false),
      d_captured_symbol_count(0),
      d_pending_output_items(0),
      d_pending_output_offset(0),
      d_pending_meta(pmt::make_dict()),
      d_correction_stats_filename("zigbee_correction_stats.txt"),
      d_correction_attempt_count(0),
      d_correction_crc_success_count(0),
      d_last_correlation_score(0.0),
      d_last_zigbee_ltf_start_raw(176),
      d_reference_ready(false)
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
    d_reference_ready = load_reference_data();
    reset_frame_capture();
    write_correction_stats();
    set_algorithm(algo);
}

frame_equalizer_impl::~frame_equalizer_impl() {}


void frame_equalizer_impl::set_algorithm(Equalizer algo)
{
    gr::thread::scoped_lock lock(d_mutex);
    d_algorithm = algo;
    delete d_equalizer;
    d_equalizer = create_equalizer(algo);
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

    if (d_pending_output_items > d_pending_output_offset) {
        return flush_pending_output(out, noutput_items);
    }

    int i = 0;
    gr_complex current_symbol[64];

    dout << "FRAME EQUALIZER: input " << ninput_items[0] << "  output " << noutput_items
         << std::endl;

    while (i < ninput_items[0]) {

        get_tags_in_window(tags, 0, i, i + 1, pmt::string_to_symbol("wifi_start"));

        // new frame
        if (tags.size()) {
            reset_frame_capture();

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

        if (d_current_symbol < 2) {
            std::memcpy(d_saved_signal_symbols + d_current_symbol * 64,
                        current_symbol,
                        64 * sizeof(gr_complex));
            d_signal_symbols_pending = true;
        }

        if (d_current_symbol < MAX_SYM + 3) {
            std::memcpy(d_captured_symbols + d_current_symbol * 64,
                        current_symbol,
                        64 * sizeof(gr_complex));
            d_captured_symbol_count = std::max(d_captured_symbol_count, d_current_symbol + 1);
        }
  
        // signal field
        if (d_current_symbol == 2) {
            uint8_t signal_bits[48];
            gr_complex signal_symbols[48];
            d_equalizer->equalize(
                current_symbol, d_current_symbol, signal_symbols, signal_bits, d_frame_mod);

            if (decode_signal_field(signal_bits)) {
                d_signal_valid = true;
                d_pending_meta = pmt::make_dict();

                d_pending_meta = pmt::dict_add(
                    d_pending_meta, pmt::mp("frame bytes"), pmt::from_uint64(d_frame_bytes));
                d_pending_meta = pmt::dict_add(
                    d_pending_meta, pmt::mp("encoding"), pmt::from_uint64(d_frame_encoding));
                d_pending_meta = pmt::dict_add(
                    d_pending_meta, pmt::mp("snr"), pmt::from_double(d_equalizer->get_snr()));
                d_pending_meta = pmt::dict_add(
                    d_pending_meta, pmt::mp("nominal frequency"), pmt::from_double(d_freq));
                d_pending_meta = pmt::dict_add(d_pending_meta,
                                               pmt::mp("frequency offset"),
                                               pmt::from_double(d_freq_offset_from_synclong));
                d_pending_meta =
                    pmt::dict_add(d_pending_meta, pmt::mp("beta"), pmt::from_double(beta));

                std::vector<gr_complex> csi = d_equalizer->get_csi();
                d_pending_meta = pmt::dict_add(
                    d_pending_meta, pmt::mp("csi"), pmt::init_c32vector(csi.size(), csi));
            } else if (d_signal_symbols_pending) {
                message_port_pub(pmt::mp("tx_feedback"), pmt::intern("nack"));
                d_signal_symbols_pending = false;
            }
        }

        if (d_signal_valid && d_current_symbol == d_frame_symbols + 2) {
            std::vector<gr_complex> final_symbols;
            uint8_t final_bits[48 * MAX_SYM];
            bool salvaged = false;

            if (try_decode_with_salvage(final_bits, final_symbols, salvaged)) {
                std::memcpy(d_pending_output_bits, final_bits, d_frame.n_sym * 48);
                d_pending_payload_symbols = final_symbols;
                d_pending_output_items = d_frame.n_sym;
                d_pending_output_offset = 0;
                publish_payload_symbols(final_symbols);
                write_signal_symbols();
            } else {
                d_signal_symbols_pending = false;
            }
            i++;
            d_current_symbol++;
            break;
        }

        i++;
        d_current_symbol++;
    }

    consume(0, i);
    if (d_pending_output_items > d_pending_output_offset) {
        return flush_pending_output(out, noutput_items);
    }
    return 0;
}

equalizer::base* frame_equalizer_impl::create_equalizer(Equalizer algo) const
{
    switch (algo) {
    case COMB:
        return new equalizer::comb();
    case LS:
        return new equalizer::ls();
    case LMS:
        return new equalizer::lms();
    case STA:
        return new equalizer::sta();
    default:
        throw std::runtime_error("Algorithm not implemented");
    }
}

bool frame_equalizer_impl::load_complex_csv_skip_header(const std::string& path,
                                                        int real_col,
                                                        int imag_col,
                                                        std::vector<gr_complex>& out)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    out.clear();
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (first) {
            first = false;
            continue;
        }

        std::stringstream ss(line);
        std::string item;
        std::vector<std::string> cols;
        while (std::getline(ss, item, ',')) {
            cols.push_back(item);
        }
        if ((int)cols.size() <= std::max(real_col, imag_col)) {
            continue;
        }
        out.emplace_back(std::stof(cols[real_col]), std::stof(cols[imag_col]));
    }
    return !out.empty();
}


bool frame_equalizer_impl::load_reference_data()
{
    const std::string base = "/home/kk/gnuradio/gr-ieee802-11/examples/ofdm_zigbee/";
    return load_complex_csv_skip_header(base + "ltf_fft1.csv", 1, 2, d_ref_ltf1) &&
           load_complex_csv_skip_header(base + "ltf_fft2.csv", 1, 2, d_ref_ltf2) &&
           load_complex_csv_skip_header(base + "wifi_rx_from_zigbee.csv",
                                        0,
                                        1,
                                        d_ref_wifi_rx_from_zigbee);
}

void frame_equalizer_impl::reset_frame_capture()
{
    d_current_symbol = 0;
    d_frame_bytes = 0;
    d_frame_symbols = 0;
    d_frame_encoding = 0;
    d_frame_mod = d_bpsk;
    d_signal_symbols_pending = false;
    d_signal_valid = false;
    d_captured_symbol_count = 0;
    d_pending_meta = pmt::make_dict();
    d_last_correlation_score = 0.0;
    d_last_zigbee_ltf_start_raw = 176;
}

bool frame_equalizer_impl::run_equalizer_attempt(gr_complex* frame_symbols,
                                                 uint8_t* out_bits,
                                                 std::vector<gr_complex>& out_symbols)
{
    std::unique_ptr<equalizer::base> eq(create_equalizer(d_algorithm));
    uint8_t scratch_bits[48];
    gr_complex scratch_symbols[48];

    eq->equalize(frame_symbols, 0, scratch_symbols, scratch_bits, d_bpsk);
    eq->equalize(frame_symbols + 64, 1, scratch_symbols, scratch_bits, d_bpsk);

    out_symbols.clear();
    out_symbols.reserve(d_frame.n_sym * 48);

    for (int sym = 0; sym < d_frame.n_sym; sym++) {
        eq->equalize(frame_symbols + (sym + 3) * 64,
                     sym + 3,
                     scratch_symbols,
                     out_bits + sym * 48,
                     d_frame_mod);
        for (int k = 0; k < 48; k++) {
            out_symbols.push_back(scratch_symbols[k]);
        }
    }

    return true;
}

int frame_equalizer_impl::raw_fft_start_from_symbol_idx(int ltf_start_raw,
                                                        int symbol_idx) const
{
    if (symbol_idx < 2) {
        return ltf_start_raw + symbol_idx * 64;
    }

    return ltf_start_raw + 128 + 16 + (symbol_idx - 2) * 80;
}

bool frame_equalizer_impl::estimate_zigbee_channel_for_offset(int ltf_start_raw,
                                                              gr_complex& h,
                                                              double& score) const
{
    if (!d_reference_ready || d_captured_symbol_count < 2) {
        h = gr_complex(0, 0);
        score = 0.0;
        return false;
    }

    const int dc_bin = 32;
    const double eps = 1e-12;
    gr_complex ref_fft[64];
    gr_complex numerator(0, 0);
    double rx_energy = 0.0;
    double ref_energy = 0.0;
    int used_symbols = 0;

    for (int sym = 0; sym < 2; sym++) {
        if (!get_zigbee_reference_symbol_fft(sym, ltf_start_raw, ref_fft)) {
            continue;
        }

        const gr_complex y_dc = d_captured_symbols[sym * 64 + dc_bin];
        const gr_complex z_dc = ref_fft[dc_bin];
        numerator += y_dc * conj(z_dc);
        rx_energy += std::norm(y_dc);
        ref_energy += std::norm(z_dc);
        used_symbols++;
    }

    if (used_symbols == 0 || rx_energy <= eps || ref_energy <= eps) {
        h = gr_complex(0, 0);
        score = 0.0;
        return false;
    }

    h = numerator / static_cast<float>(ref_energy + eps);
    score = std::abs(numerator) / std::sqrt(rx_energy * ref_energy);
    return true;
}

bool frame_equalizer_impl::get_zigbee_reference_symbol_fft(int symbol_idx,
                                                           gr_complex* fft_symbol) const
{
    const int ltf_start_raw = 176;
    return get_zigbee_reference_symbol_fft(symbol_idx, ltf_start_raw, fft_symbol);
}

bool frame_equalizer_impl::get_zigbee_reference_symbol_fft(int symbol_idx,
                                                           int ltf_start_raw,
                                                           gr_complex* fft_symbol) const
{
    if (!d_reference_ready) {
        return false;
    }

    const int nfft = 64;
    const int start = raw_fft_start_from_symbol_idx(ltf_start_raw, symbol_idx);
    if (start < 0 || start + nfft > (int)d_ref_wifi_rx_from_zigbee.size()) {
        return false;
    }

    gr_complex unshifted_fft[64];
    for (int k = 0; k < nfft; k++) {
        fft_symbol[k] = gr_complex(0, 0);
        unshifted_fft[k] = gr_complex(0, 0);
    }

    for (int bin = 0; bin < nfft; bin++) {
        gr_complex acc(0, 0);
        for (int n = 0; n < nfft; n++) {
            const float angle = -2 * M_PI * bin * n / nfft;
            acc += d_ref_wifi_rx_from_zigbee[start + n] * exp(gr_complex(0, angle));
        }
        unshifted_fft[bin] = acc;
    }

    for (int bin = 0; bin < nfft; bin++) {
        fft_symbol[bin] = unshifted_fft[(bin + nfft / 2) % nfft];
    }
    return true;
}

void frame_equalizer_impl::subtract_zigbee_interference(gr_complex h,
                                                        gr_complex* frame_symbols,
                                                        int total_symbols) const
{
    const int ltf_start_raw = 176;
    subtract_zigbee_interference(h, ltf_start_raw, frame_symbols, total_symbols);
}

void frame_equalizer_impl::subtract_zigbee_interference(gr_complex h,
                                                        int ltf_start_raw,
                                                        gr_complex* frame_symbols,
                                                        int total_symbols) const
{
    if (std::abs(h) < 1e-9f) {
        return;
    }

    gr_complex ref_fft[64];
    for (int sym = 0; sym < total_symbols; sym++) {
        if (!get_zigbee_reference_symbol_fft(sym, ltf_start_raw, ref_fft)) {
            break;
        }
        for (int k = 0; k < 64; k++) {
            frame_symbols[sym * 64 + k] -= h * ref_fft[k];
        }
    }
}

bool frame_equalizer_impl::try_decode_with_salvage(uint8_t* final_bits,
                                                   std::vector<gr_complex>& final_symbols,
                                                   bool& salvaged)
{
    uint8_t attempt_bits[48 * MAX_SYM];
    salvaged = false;

    run_equalizer_attempt(d_captured_symbols, attempt_bits, final_symbols);
    if (decode_payload(attempt_bits, false)) {
        std::memcpy(final_bits, attempt_bits, d_frame.n_sym * 48);
        message_port_pub(pmt::mp("tx_feedback"), pmt::intern("ack"));
        return true;
    }

    std::vector<gr_complex> salvaged_symbols;
    gr_complex salvaged_frame[(MAX_SYM + 3) * 64];
    const int default_ltf_start_raw = 176;
    const int search_radius = 170;
    const int max_salvage_decode_attempts = 12;
    double best_failed_score = -1.0;
    int best_failed_ltf_start_raw = default_ltf_start_raw;
    d_correction_attempt_count++;
    write_correction_stats();

    struct ZigbeeCandidate {
        int ltf_start_raw;
        gr_complex h;
        double score;
    };
    std::vector<ZigbeeCandidate> candidates;
    candidates.reserve(2 * search_radius + 1);

    for (int candidate = default_ltf_start_raw - search_radius;
         candidate <= default_ltf_start_raw + search_radius;
         candidate++) {
        gr_complex zigbee_h(0, 0);
        double zigbee_score = 0.0;
        if (!estimate_zigbee_channel_for_offset(candidate, zigbee_h, zigbee_score)) {
            continue;
        }

        if (zigbee_score > best_failed_score) {
            best_failed_score = zigbee_score;
            best_failed_ltf_start_raw = candidate;
        }

        candidates.push_back({ candidate, zigbee_h, zigbee_score });
    }

    if (!candidates.empty()) {
        std::sort(candidates.begin(),
                  candidates.end(),
                  [](const ZigbeeCandidate& a, const ZigbeeCandidate& b) {
                      return a.score > b.score;
                  });
    }

    const int decode_attempts =
        std::min(max_salvage_decode_attempts, static_cast<int>(candidates.size()));
    for (int idx = 0; idx < decode_attempts; idx++) {
        const ZigbeeCandidate& candidate = candidates[idx];
        d_last_zigbee_ltf_start_raw = candidate.ltf_start_raw;
        d_last_correlation_score = candidate.score;

        std::memcpy(salvaged_frame,
                    d_captured_symbols,
                    d_captured_symbol_count * 64 * sizeof(gr_complex));
        subtract_zigbee_interference(
            candidate.h, candidate.ltf_start_raw, salvaged_frame, d_frame.n_sym + 3);

        salvaged_symbols.clear();
        run_equalizer_attempt(salvaged_frame, attempt_bits, salvaged_symbols);
        if (decode_payload(attempt_bits, false)) {
            std::memcpy(final_bits, attempt_bits, d_frame.n_sym * 48);
            final_symbols.swap(salvaged_symbols);
            salvaged = true;
            d_correction_crc_success_count++;
            write_correction_stats();
            message_port_pub(pmt::mp("tx_feedback"), pmt::intern("ack"));
            return true;
        }
    }

    if (best_failed_score >= 0.0) {
        d_last_zigbee_ltf_start_raw = best_failed_ltf_start_raw;
        d_last_correlation_score = best_failed_score;
        write_correction_stats();
    }

    message_port_pub(pmt::mp("tx_feedback"), pmt::intern("nack"));
    return false;
}

int frame_equalizer_impl::flush_pending_output(uint8_t* out, int noutput_items)
{
    const int available = d_pending_output_items - d_pending_output_offset;
    const int produced = std::min(noutput_items, available);
    if (produced <= 0) {
        return 0;
    }

    std::memcpy(out,
                d_pending_output_bits + d_pending_output_offset * 48,
                produced * 48);

    if (d_pending_output_offset == 0) {
        pmt::pmt_t pairs = pmt::dict_items(d_pending_meta);
        for (int idx = 0; idx < pmt::length(pairs); idx++) {
            pmt::pmt_t pair = pmt::nth(idx, pairs);
            add_item_tag(0,
                         nitems_written(0),
                         pmt::car(pair),
                         pmt::cdr(pair),
                         alias_pmt());
        }
    }

    d_pending_output_offset += produced;
    if (d_pending_output_offset >= d_pending_output_items) {
        d_pending_output_items = 0;
        d_pending_output_offset = 0;
        d_pending_payload_symbols.clear();
    }

    return produced;
}

void frame_equalizer_impl::publish_payload_symbols(
    const std::vector<gr_complex>& payload_symbols)
{
    for (int sym = 0; sym < d_frame.n_sym; sym++) {
        std::vector<gr_complex> one_symbol(payload_symbols.begin() + sym * 48,
                                           payload_symbols.begin() + (sym + 1) * 48);
        message_port_pub(
            pmt::mp("symbols"),
            pmt::cons(pmt::make_dict(), pmt::init_c32vector(one_symbol.size(), one_symbol)));
    }
}

void frame_equalizer_impl::write_correction_stats()
{
    std::ofstream stats_file(d_correction_stats_filename, std::ios::out | std::ios::trunc);
    if (!stats_file.is_open()) {
        return;
    }

    stats_file << "correction_attempt_count=" << d_correction_attempt_count << "\n";
    stats_file << "correction_crc_success_count=" << d_correction_crc_success_count
               << "\n";
    stats_file << "last_correlation_score=" << d_last_correlation_score << "\n";
    stats_file << "last_ltf_start_raw=" << d_last_zigbee_ltf_start_raw << "\n";
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

bool frame_equalizer_impl::decode_payload(const uint8_t* rx_symbols, bool publish_feedback)
{
    std::memcpy(d_rx_symbols, rx_symbols, d_frame.n_sym * 48);
    deinterleave();
    uint8_t* decoded = d_decoder.decode(&d_ofdm, &d_frame, d_deinterleaved_bits);
    descramble(decoded);

    boost::crc_32_type result;
    result.process_bytes(out_bytes + 2, d_frame.psdu_size);

    if (result.checksum() != 558161692) {
        dout << "payload checksum wrong -- dropping saved signal symbols" << std::endl;
        if (publish_feedback) {
            message_port_pub(pmt::mp("tx_feedback"), pmt::intern("nack"));
        }
        return false;
    }

    if (publish_feedback) {
        message_port_pub(pmt::mp("tx_feedback"), pmt::intern("ack"));
    }
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
