/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "Serlizsig_impl.h"

namespace gr {
  namespace customModule {


    Serlizsig::sptr
    Serlizsig::make(
                    int fft_len,
                    const std::vector<std::vector<int>>& occupied_carriers,
                    const std::string& len_tag_key,
                    const std::string& packet_len_tag_key,
                    int symbols_skipped,
                    const std::string& carr_offset_key,
                    bool input_is_shifted,
                    const std::string& signal_filename,
                    const std::string& channel_taps_filename
    )
    {
      return gnuradio::make_block_sptr<Serlizsig_impl>(fft_len,
                                                      occupied_carriers,
                                                      len_tag_key,
                                                      packet_len_tag_key,
                                                      symbols_skipped,
                                                      carr_offset_key,
                                                      input_is_shifted,
                                                      signal_filename,
                                                      channel_taps_filename
        );
    }


    /*
     * The private constructor
     */
    Serlizsig_impl::Serlizsig_impl(    
      int fft_len,
      const std::vector<std::vector<int>>& occupied_carriers,
      const std::string& len_tag_key,
      const std::string& packet_len_tag_key,
      int symbols_skipped,
      const std::string& carr_offset_key,
      bool input_is_shifted,
      const std::string& signal_filename,
      const std::string& channel_taps_filename)
      : gr::tagged_stream_block("Serlizsig",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(gr_complex) * fft_len),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0), len_tag_key),
        d_fft_len(fft_len),
        d_occupied_carriers(occupied_carriers),
        d_packet_len_tag_key(pmt::string_to_symbol(packet_len_tag_key)),
        d_out_len_tag_key(pmt::string_to_symbol(
            (packet_len_tag_key.empty() ? len_tag_key : packet_len_tag_key))),
        d_symbols_skipped(symbols_skipped % occupied_carriers.size()),
        d_carr_offset_key(pmt::string_to_symbol(carr_offset_key)),
        d_curr_set(symbols_skipped % occupied_carriers.size()),
        d_symbols_per_set(0),
        d_channel_state(fft_len, gr_complex(1, 0))
    {
      signal_file.open(signal_filename, std::ios::out | std::ios::app);
      if(!signal_file.is_open()) {
        throw std::runtime_error("Failed to open payload File" + signal_filename);
      }
      taps_file.open(channel_taps_filename, std::ios::out | std::ios::trunc);
      if(!taps_file.is_open()) {
        throw std::runtime_error("Failed to open taps File" + channel_taps_filename);
      }
      for (unsigned i = 0; i < d_occupied_carriers.size(); i++) {
        for (unsigned k = 0; k < d_occupied_carriers[i].size(); k++) {
            if (input_is_shifted) {
                d_occupied_carriers[i][k] += fft_len / 2;
                if (d_occupied_carriers[i][k] > fft_len) {
                    d_occupied_carriers[i][k] -= fft_len;
                }
            } else {
                if (d_occupied_carriers[i][k] < 0) {
                    d_occupied_carriers[i][k] += fft_len;
                }
            }
            if (d_occupied_carriers[i][k] >= fft_len || d_occupied_carriers[i][k] < 0) {
                throw std::invalid_argument("ofdm_serializer_vcc: trying to occupy a "
                                            "carrier outside the fft length.");
            }
        }
      }
      for (unsigned i = 0; i < d_occupied_carriers.size(); i++) {
        d_symbols_per_set += d_occupied_carriers[i].size();
    }
      set_relative_rate((uint64_t)d_symbols_per_set, (uint64_t)d_occupied_carriers.size());
      set_tag_propagation_policy(TPP_DONT);
    }

    /*
     * Our virtual destructor.
     */
    Serlizsig_impl::~Serlizsig_impl()
    {
    }

    int
    Serlizsig_impl::calculate_output_stream_length(const gr_vector_int &ninput_items)
    {
      
      int nout = (ninput_items[0] / d_occupied_carriers.size()) * d_symbols_per_set;
      for (unsigned i = 0; i < ninput_items[0] % d_occupied_carriers.size(); i++) {
          nout += d_occupied_carriers[(i + d_curr_set) % d_occupied_carriers.size()].size();
      }
      return nout;
    }

    int
    Serlizsig_impl::work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      const gr_complex* in = (const gr_complex*)input_items[0];
      // gr_complex* out = (gr_complex*)output_items[0];

      long frame_length = ninput_items[0]; // Input frame
      long packet_length = 0;              // Output frame
      int carr_offset = 0;
      int carrier_offset = 0;
      std::vector<tag_t> tags;
      // Packet mode
      if (!d_length_tag_key_str.empty()) {
          get_tags_in_range(tags, 0, nitems_read(0), nitems_read(0) + 1);
        //   signal_file << "inside " << "\n" << std::endl;
          for (unsigned i = 0; i < tags.size(); i++) {
              if (tags[i].key == d_carr_offset_key) {
                  carr_offset = pmt::to_long(tags[i].value);
              }
            if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_carr_offset") {
                carrier_offset = pmt::to_long(tags[i].value);

            }
            if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_chan_taps") {
                d_channel_state = pmt::c32vector_elements(tags[i].value);// d_channel-state --> channel_taps

            }
              if (tags[i].key == d_packet_len_tag_key) {
                  packet_length = pmt::to_long(tags[i].value);
              }
          }
      } else {
          // recalc frame length from noutput_items
          frame_length = 0;
          int sym_per_frame = 0;
          while ((sym_per_frame +
                  d_occupied_carriers[(frame_length + 1) % d_occupied_carriers.size()]
                      .size()) < (size_t)noutput_items) {
              frame_length++;
              sym_per_frame +=
                  d_occupied_carriers[(frame_length + 1) % d_occupied_carriers.size()]
                      .size();
          }
      }

    std::vector<gr_complex> outBuffer(frame_length * d_fft_len, gr_complex(0, 0));

          // Copy symbols
    int n_out_symbols = 0;


    signal_file << "Fine CFO: "  << carrier_offset << "\n " << std::endl;

    signal_file << "baseband_frame starting:  + frame_legnth"  << frame_length << "\n " << std::endl;
    for (int i = 0; i < frame_length; i++) {

        for (unsigned k = 0; k < d_occupied_carriers[d_curr_set].size(); k++) {

                signal_file << in[i * d_fft_len + d_occupied_carriers[d_curr_set][k] + carr_offset] << std::endl;
                n_out_symbols++;
        }
        if (packet_length && n_out_symbols > packet_length) {
            n_out_symbols = packet_length;
            break;
        }
        d_curr_set = (d_curr_set + 1) % d_occupied_carriers.size();
    }
    signal_file << "baseband_frame end"  << "\n " << std::endl;

    taps_file << "Taps starting: " << std::endl;
    for (const auto& tap: d_channel_state) {
        taps_file << tap << std::endl;
    }
        taps_file << "\n";
        
  

    // Housekeeping
    if (d_length_tag_key_str.empty()) {
        consume_each(frame_length);
    } else {
        d_curr_set = d_symbols_skipped;
    }

    return n_out_symbols;


    }

  } /* namespace customModule */
} /* namespace gr */
