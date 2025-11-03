/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getSignal_impl.h"

namespace gr {
  namespace customModule {


    // using input_type = float;

    // using output_type = float;
    getSignal::sptr
    getSignal::make(size_t item_size, const std::string& signal_filename)
    {
      return gnuradio::make_block_sptr<getSignal_impl>(item_size, signal_filename);
    }


    /*
     * The private constructor
     */
    getSignal_impl::getSignal_impl(size_t item_size, const std::string& signal_filename)
      : gr::sync_block("getSignal",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, item_size),
              gr::io_signature::make(0/* min outputs */, 0 /*max outputs */, 0)),
        d_item_size(item_size)
    { 
        std::ofstream(signal_filename).close();
        signal_file.open(signal_filename, std::ios::out | std::ios::app);
        // signal_file << "123123123444";
        if(!signal_file.is_open()) {
          throw std::runtime_error("Failed to open signal files: " + signal_filename);
        }
    }

    /*
     * Our virtual destructor.
     */
    getSignal_impl::~getSignal_impl()
    {
    }

    int
    getSignal_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {


        const unsigned char* in = (const unsigned char*)input_items[0];
        gr_complex* out  = (gr_complex*)output_items[0];
        // std::vector<tag_t> tags;

        // signal_file.write((const char*)in, d_item_size * noutput_items);
        // coz the input is complex value, if there is a way to write the data one by one 
        signal_file << "noutput_items: " << noutput_items << std::endl;
        for (int i = 0; i < noutput_items; ++i) {
            signal_file <<  static_cast<unsigned>  (in[i]) << std::endl;
            // signal_file << noutput_items << std::endl;
        }
        signal_file << "\n";

        return noutput_items;

    }

  } /* namespace customModule */
} /* namespace gr */
