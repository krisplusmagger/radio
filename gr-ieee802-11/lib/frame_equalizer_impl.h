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

#ifndef INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H
#define INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H

#include "equalizer/base.h"
#include "utils.h"
#include "viterbi_decoder/viterbi_decoder.h"
#include <ieee802_11/constellations.h>
#include <ieee802_11/frame_equalizer.h>
#include <array>
#include <string>
#include <vector>

namespace gr {
namespace ieee802_11 {

class frame_equalizer_impl : virtual public frame_equalizer
{

public:
    frame_equalizer_impl(Equalizer algo, double freq, double bw, bool log, bool debug, const std::string& signal_filename);
    ~frame_equalizer_impl();

    void set_algorithm(Equalizer algo);
    void set_bandwidth(double bw);
    void set_frequency(double freq);

    void forecast(int noutput_items, gr_vector_int& ninput_items_required);
    int general_work(int noutput_items,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items);

private:
    bool parse_signal(uint8_t* signal);
    bool decode_signal_field(uint8_t* rx_bits);
    bool decode_signal_field_erased(const uint8_t* rx_bits);
    bool signal_matches_known() const;   // decoded SIGNAL equals the known fixed frame?
    bool set_known_frame_params();       // force the known frame params (knowledge-aided)
    void deinterleave(uint8_t* rx_bits);
    void deinterleave(bool erase_central = false);
    void descramble(uint8_t* decoded_bits);
    bool decode_payload(const uint8_t* rx_symbols,
                        bool publish_feedback,
                        bool erase_central = false);
    void compute_erasure_carriers();
    void write_signal_symbols();
    equalizer::base* create_equalizer(Equalizer algo) const;
    bool load_complex_csv_skip_header(const std::string& path,
                                      int real_col,
                                      int imag_col,
                                      std::vector<gr_complex>& out);
    bool load_reference_data();
    void precompute_zigbee_reference_ffts();
    void reset_frame_capture();
    bool run_equalizer_attempt(gr_complex* frame_symbols,
                               uint8_t* out_bits,
                               std::vector<gr_complex>& out_symbols);
    int raw_fft_start_from_symbol_idx(int ltf_start_raw, int symbol_idx) const;
    bool estimate_zigbee_channel_for_offset(int ltf_start_raw,
                                            gr_complex& h,
                                            double& score) const;
    bool get_zigbee_reference_symbol_fft(int symbol_idx, gr_complex* fft_symbol) const;
    bool get_zigbee_reference_symbol_fft(int symbol_idx,
                                         int ltf_start_raw,
                                         gr_complex* fft_symbol) const;
    bool compute_zigbee_reference_symbol_fft_uncached(
        int symbol_idx,
        int ltf_start_raw,
        std::array<gr_complex, 64>& fft_symbol) const;
    void subtract_zigbee_interference(gr_complex h,
                                      gr_complex* frame_symbols,
                                      int total_symbols) const;
    void subtract_zigbee_interference(gr_complex h,
                                      int ltf_start_raw,
                                      gr_complex* frame_symbols,
                                      int total_symbols) const;
    bool ltf_clean_band_ok();
    bool try_decode_signal_with_salvage();
    bool try_decode_with_salvage(uint8_t* final_bits,
                                 std::vector<gr_complex>& final_symbols,
                                 bool& salvaged);
    int flush_pending_output(uint8_t* out, int noutput_items);
    void publish_payload_symbols(const std::vector<gr_complex>& payload_symbols);
    void write_correction_stats();
    void capture_raw_frame(std::ofstream& file,
                           int& counter,
                           const gr_complex* raw,
                           int total_symbols,
                           const char* outcome,
                           const char* tier,
                           double score);

    equalizer::base* d_equalizer;
    gr::thread::mutex d_mutex;
    std::vector<gr::tag_t> tags;
    bool d_debug;
    bool d_log;
    Equalizer d_algorithm;
    int d_current_symbol;
    std::ofstream signal_file;
    std::ofstream d_diag_file;       // unconditional per-frame diagnostics (M + outcome)
    float d_diag_m = 0.0f;           // clean-band LTF agreement M of the current frame
    std::ofstream d_good_frames_file; // raw (pre-equalizer) frames that passed CRC
    std::ofstream d_fail_frames_file; // raw frames that reached salvage/erasure but failed CRC
    int d_good_capture_count = 0;
    int d_fail_capture_count = 0;
    viterbi_decoder d_decoder;

    // freq offset
    double d_freq;                      // Hz
    double d_freq_offset_from_synclong; // Hz, estimation from "sync_long" block
    double d_bw;                        // Hz
    double d_er;
    double d_epsilon0;
    gr_complex d_prev_pilots[4];

    int d_frame_bytes;
    int d_frame_symbols;
    int d_frame_encoding;
    ofdm_param d_ofdm;
    frame_param d_frame;

    uint8_t d_deinterleaved[48];
    gr_complex symbols[48];
    uint8_t d_rx_symbols[48 * MAX_SYM];
    uint8_t d_rx_bits[MAX_ENCODED_BITS];
    uint8_t d_deinterleaved_bits[MAX_ENCODED_BITS];
    uint8_t out_bytes[MAX_PSDU_SIZE + 2];
    gr_complex d_saved_signal_symbols[2 * 64];
    gr_complex d_captured_symbols[(MAX_SYM + 3) * 64];
    gr_complex d_raw_snapshot[(MAX_SYM + 3) * 64]; // pre-equalizer copy for frame capture
    uint8_t d_pending_output_bits[48 * MAX_SYM];
    std::vector<gr_complex> d_pending_payload_symbols;
    bool d_signal_symbols_pending;
    bool d_signal_valid;
    bool d_signal_was_known_fallback = false; // SIGNAL came from known-frame fallback
    bool d_ltf_clean_ok = true; // Layer 2 clean-band LTF veto result for current frame
    std::vector<int> d_erasure_carriers; // data-carrier indices ZigBee corrupts (erasures)
    int d_captured_symbol_count;
    int d_pending_output_items;
    int d_pending_output_offset;
    pmt::pmt_t d_pending_meta;
    std::string d_correction_stats_filename;
    uint64_t d_correction_attempt_count;
    uint64_t d_correction_crc_success_count;
    double d_last_correlation_score;
    int d_last_zigbee_ltf_start_raw;
    std::vector<gr_complex> d_ref_ltf1;
    std::vector<gr_complex> d_ref_ltf2;
    std::vector<gr_complex> d_ref_wifi_rx_from_zigbee;
    std::vector<std::array<gr_complex, 64>> d_zigbee_ref_fft_cache;
    std::vector<uint8_t> d_zigbee_ref_fft_cache_valid;
    int d_zigbee_ref_fft_cache_min_ltf_start_raw;
    int d_zigbee_ref_fft_cache_max_ltf_start_raw;
    int d_zigbee_ref_fft_cache_symbol_count;
    bool d_zigbee_ref_fft_cache_ready;
    bool d_reference_ready;

    std::shared_ptr<gr::digital::constellation> d_frame_mod;
    constellation_bpsk::sptr d_bpsk;
    constellation_qpsk::sptr d_qpsk;
    constellation_16qam::sptr d_16qam;
    constellation_64qam::sptr d_64qam;

    static const int interleaver_pattern[48];
};

} // namespace ieee802_11
} // namespace gr

#endif /* INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H */
