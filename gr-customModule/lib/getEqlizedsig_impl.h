/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_IMPL_H

#include <gnuradio/customModule/getEqlizedsig.h>
#include <gnuradio/digital/ofdm_equalizer_base.h>


namespace gr {
  namespace customModule {

    class getEqlizedsig_impl : public getEqlizedsig
    {
     private:
      std::ofstream eqsignal_file;
      gr::digital::ofdm_equalizer_base::sptr d_eq;
      int d_fft_len;
      std::vector<gr_complex> d_channel_state;
      int d_cp_len;
      pmt::pmt_t d_carr_offset_key;

     protected:
        void parse_length_tags(const std::vector<std::vector<tag_t>>& tags,
                              gr_vector_int& n_input_items_reqd) override;
      // int calculate_output_stream_length(const gr_vector_int &ninput_items);

     public:
      getEqlizedsig_impl(gr::digital::ofdm_equalizer_base::sptr equalizer,
                          const std::string& equlizedsig_filename,
                          int fft_len,
                          int cp_len,
                          const std::string& tsb_key
                          
      );
      ~getEqlizedsig_impl();

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_int &ninput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_IMPL_H */
