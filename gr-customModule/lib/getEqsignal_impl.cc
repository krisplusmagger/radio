/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getEqsignal_impl.h"
namespace gr {
  namespace customModule {


    using input_type = gr_complex;

    getEqsignal::sptr
    getEqsignal::make(
        int fft_len,
        const std::string& eqsignal_filename)
    {
      return gnuradio::make_block_sptr<getEqsignal_impl>(fft_len,     
                                                         eqsignal_filename);
    }


    /*
     * The private constructor
     */
    getEqsignal_impl::getEqsignal_impl(int fft_len, const std::string& eqsignal_filename)
      : gr::sync_block("getEqsignal",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0))
    {
        std::ofstream(eqsignal_filename).close();
        eqsignal_file.open(eqsignal_filename, std::ios::out | std::ios::app);
        if(!eqsignal_file.is_open()) {
          throw std::runtime_error("Faield to open eqsignal Files" + eqsignal_filename);
        }
    }

    /*
     * Our virtual destructor.
     */
    getEqsignal_impl::~getEqsignal_impl()
    {
    }

    int
    getEqsignal_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {

      
      const gr_complex* in = (const gr_complex*)input_items[0];
      gr_complex* out = (gr_complex*)output_items[0];
      

          // Do <+signal processing+>
      for (int i = 0 ; i < noutput_items; ++i) {
        eqsignal_file << in[i] <<std::endl;
      }
      eqsignal_file << "\n";
      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace customModule */
} /* namespace gr */
