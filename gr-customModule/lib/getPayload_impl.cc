/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getPayload_impl.h"

namespace gr {
  namespace customModule {

    using input_type = gr_complex;
    // using output_type = float;
    getPayload::sptr
    getPayload::make(int fft_len, const std::string& payload_filename)
    {
      return gnuradio::make_block_sptr<getPayload_impl>(fft_len, payload_filename
        );
    }


    /*
     * The private constructor
     */
    getPayload_impl::getPayload_impl(int fft_len, const std::string& payload_filename)
      : gr::sync_block("getPayload",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type) * fft_len),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0)),
            d_fft_len(fft_len)
    {
        std::ofstream(payload_filename).close();
        payload_file.open(payload_filename, std::ios::out | std::ios::app);
        if(!payload_file.is_open()) {
          throw std::runtime_error("Failed to open payload File" + payload_filename);
        }
    }

    /*
     * Our virtual destructor.
     */
    getPayload_impl::~getPayload_impl()
    {
    }

    int
    getPayload_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {

      const gr_complex* in = (const gr_complex*)input_items[0];
      gr_complex* out = (gr_complex*)output_items[0];
      payload_file << "frame length: " << noutput_items << "starting" << "\n";
      // Do <+signal processing+>
      for (int i = 0 ; i < noutput_items; ++i) {
        for (int k = 0; k < d_fft_len; k++) 
        {
          payload_file << in[i * d_fft_len + k] <<std::endl;

        }
       
        // payload_file << noutput_items << std::endl;
      }
      payload_file << "frame ending" << "\n";

  
 
      return noutput_items;
    }

  } /* namespace customModule */
} /* namespace gr */
