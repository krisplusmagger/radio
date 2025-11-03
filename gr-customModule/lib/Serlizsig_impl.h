/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_SERLIZSIG_IMPL_H
#define INCLUDED_CUSTOMMODULE_SERLIZSIG_IMPL_H

#include <gnuradio/customModule/Serlizsig.h>

namespace gr {
  namespace customModule {

    class Serlizsig_impl : public Serlizsig
    {
     private:
      int d_fft_len; //!< FFT length
      std::vector<std::vector<int>>
          d_occupied_carriers;         //!< Which carriers/symbols carry data
      pmt::pmt_t d_packet_len_tag_key; //!< Key of the length tag
      pmt::pmt_t d_out_len_tag_key;    //!< Key of the length tag
      const int d_symbols_skipped;     //!< Start position in d_occupied_carriers
      pmt::pmt_t d_carr_offset_key;    //!< Key of the carrier offset tag
      int d_curr_set;                  //!< Current position in d_occupied_carriers
      int d_symbols_per_set;
      std::ofstream signal_file;
      std::ofstream taps_file;
      std::vector<gr_complex>d_channel_state;    
     protected:
          /*!
          * Calculate the number of scalar complex symbols given a number of
          * OFDM symbols.
          */
          int calculate_output_stream_length(const gr_vector_int& ninput_items) override;
          // void update_length_tags(int n_produced, int n_ports) override;

     public:
      Serlizsig_impl(int fft_len,
                    const std::vector<std::vector<int>>& occupied_carriers,
                    const std::string& len_tag_key,
                    const std::string& packet_len_tag_key,
                    int symbols_skipped,
                    const std::string& carr_offset_key,
                    bool input_is_shifted,
                    const std::string& signal_filename,
                    const std::string& channel_taps_filename);
      ~Serlizsig_impl();

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

#endif /* INCLUDED_CUSTOMMODULE_SERLIZSIG_IMPL_H */
