/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getBaseband_impl.h"

namespace gr {
  namespace customModule {


    using input_type = gr_complex;

    // using output_type = float;
    getBaseband::sptr
    getBaseband::make(int fft_len, const std::string& baseband_filename)
    {
      return gnuradio::make_block_sptr<getBaseband_impl>(fft_len, baseband_filename
        );
    }


    /*
     * The private constructor
     */
    getBaseband_impl::getBaseband_impl(int fft_len, const std::string& baseband_filename)
      : gr::sync_block("getBaseband",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type) * fft_len),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */,0 )),
              d_fft_len(fft_len)
    {
      std::ofstream(baseband_filename).close();
      baseband_file.open(baseband_filename, std::ios::out | std::ios::app);
      if(!baseband_file.is_open()) {
        throw std::runtime_error("Failed to open payload File" + baseband_filename);
      }
    }

    /*
     * Our virtual destructor.
     */
    getBaseband_impl::~getBaseband_impl()
    {
    }

    int
    getBaseband_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      const gr_complex* in = (const gr_complex*)input_items[0];
      gr_complex* out = (gr_complex*)output_items[0];

      for (int i = 0; i < noutput_items; i++) {
        baseband_file << in[i] << std::endl;
      }
      baseband_file << "\n";

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace customModule */
} /* namespace gr */
