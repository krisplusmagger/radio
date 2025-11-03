/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getEqlizedsig_impl.h"
#include <gnuradio/digital/ofdm_equalizer_base.h>
#include <gnuradio/expj.h>
#include <gnuradio/math.h>

static const pmt::pmt_t CARR_OFFSET_KEY = pmt::mp("ofdm_sync_carr_offset");
static const pmt::pmt_t CHAN_TAPS_KEY = pmt::mp("ofdm_sync_chan_taps");

namespace gr {
  namespace customModule {

    getEqlizedsig::sptr
    getEqlizedsig::make(gr::digital::ofdm_equalizer_base::sptr equalizer,
                        const std::string& equlizedsig_filename,
                        int fft_len,
                        int cp_len,
                        const std::string& tsb_key)
    {
      return gnuradio::make_block_sptr<getEqlizedsig_impl>( 
        equalizer, equlizedsig_filename, fft_len, cp_len, tsb_key);
    }


    /*
     * The private constructor
     */
    getEqlizedsig_impl::getEqlizedsig_impl(
      gr::digital::ofdm_equalizer_base::sptr equalizer,
      const std::string& equlizedsig_filename,
      int fft_len,
      int cp_len,
      const std::string& tsb_key)
      : gr::tagged_stream_block("getEqlizedsig",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(gr_complex) * equalizer->fft_len()),
              gr::io_signature::make(1/* min outputs */, 1 /*max outputs */, sizeof(gr_complex) * equalizer->fft_len() ),
              tsb_key),
        d_fft_len(fft_len),
        d_cp_len(cp_len),
        d_eq(equalizer),
        d_channel_state(equalizer->fft_len(), gr_complex(1, 0))

    {
        std::ofstream(equlizedsig_filename).close();
        eqsignal_file.open(equlizedsig_filename, std::ios::out | std::ios::app);
        if(!eqsignal_file.is_open()) {
          throw std::runtime_error("Faield to open eqsignal Files" + equlizedsig_filename);
        }

        // eqsignal_file << "111" <<std::endl;
        set_relative_rate(1, 1);
        // Really, we have TPP_ONE_TO_ONE, but the channel state is not propagated
        set_tag_propagation_policy(TPP_DONT);
    }

    /*
     * Our virtual destructor.
     */
    getEqlizedsig_impl::~getEqlizedsig_impl()
    {
    }

    void getEqlizedsig_impl::parse_length_tags(
      const std::vector<std::vector<tag_t>>& tags, gr_vector_int& n_input_items_reqd)
  {

        for (unsigned k = 0; k < tags[0].size(); k++) {
            if (tags[0][k].key == pmt::string_to_symbol(d_length_tag_key_str)) {
                n_input_items_reqd[0] = pmt::to_long(tags[0][k].value);
            }
        }
      
  }

    int
    getEqlizedsig_impl::work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {

      const gr_complex* in = (const gr_complex*)input_items[0];
      gr_complex* out = (gr_complex*)output_items[0];
      int carrier_offset = 0;
      int frame_len = 0;
      frame_len = ninput_items[0];
      // std::vector<gr_complex> temp_buff(d_fft_len * frame_len);

      std::vector<tag_t> tags;
      get_tags_in_window(tags, 0, 0, 1);
      for (unsigned i = 0; i < tags.size(); i++) {
          if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_chan_taps") {
              d_channel_state = pmt::c32vector_elements(tags[i].value);
          }
          if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_carr_offset") {
              carrier_offset = pmt::to_long(tags[i].value);
          }
      }
      // Copy the frame and the channel state vector such that the symbols are shifted to
      // the correct position
      if (carrier_offset < 0) {
        memset((void*)out, 0x00, sizeof(gr_complex) * (-carrier_offset));
        memcpy((void*)&out[-carrier_offset],
              (const void*)in,
              sizeof(gr_complex) * (d_fft_len * frame_len + carrier_offset));
    } else {
        memset((void*)(out + d_fft_len * frame_len - carrier_offset),
              0x00,
              sizeof(gr_complex) * carrier_offset);
        memcpy((void*)out,
              (const void*)(in + carrier_offset),
              sizeof(gr_complex) * (d_fft_len * frame_len - carrier_offset));
            }
      // Correct the frequency shift on the symbols
      gr_complex phase_correction;
      for (int i = 0; i < frame_len; i++) {
          phase_correction =
              gr_expj(-(2.0 * GR_M_PI) * carrier_offset * d_cp_len / d_fft_len * (i + 1));
          for (int k = 0; k < d_fft_len; k++) {
              out[i * d_fft_len + k] *= phase_correction;
          }
      }
      // Do the equalizing
      d_eq->reset();
      // d_eq->equalize(out, frame_len, d_channel_state, tags);
      d_eq->get_channel_state(d_channel_state);
      // Update the channel state regarding the frequency offset
      phase_correction =
          gr_expj((2.0 * GR_M_PI) * carrier_offset * d_cp_len / d_fft_len * frame_len);
      for (int k = 0; k < d_fft_len; k++) {
          d_channel_state[k] *= phase_correction;
      }

      // Propagate tags (except for the channel state and the TSB tag)
      get_tags_in_window(tags, 0, 0, frame_len);
      for (size_t i = 0; i < tags.size(); i++) {
          if (tags[i].key != CHAN_TAPS_KEY &&
              tags[i].key != pmt::mp(d_length_tag_key_str)) {
              add_item_tag(0, tags[i]);
          }
      }

      // Housekeeping
      add_item_tag(0,
                  nitems_written(0),
                  pmt::string_to_symbol("ofdm_sync_chan_taps"),
                  pmt::init_c32vector(d_fft_len, d_channel_state));
      



      // Tell runtime system how many output items we produced.
      return frame_len;
    }

  } /* namespace customModule */
  } /* namespace gr */

