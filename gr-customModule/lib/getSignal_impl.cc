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
    getSignal::make(int input_len, size_t item_size, const std::string& signal_filename, const std::string& start_index_filename)
    {
      return gnuradio::make_block_sptr<getSignal_impl>(input_len, item_size, signal_filename, start_index_filename);
    }


    /*
     * The private constructor
     */
    getSignal_impl::getSignal_impl(int input_len, size_t item_size, const std::string& signal_filename, const std::string& start_index_filename)
      : gr::block("getSignal",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, input_len * item_size),
              gr::io_signature::make(0/* min outputs */, 0 /*max outputs */, 0)),
        d_item_size(item_size),
        d_input_len(input_len)
    { 
        std::ofstream(signal_filename).close();
        signal_file.open(signal_filename, std::ios::out | std::ios::app);

        if(!signal_file.is_open()) {
          throw std::runtime_error("Failed to open signal files: " + signal_filename);
        }
        
        std::ofstream(start_index_filename).close();
        start_index_file.open(start_index_filename, std::ios::out | std::ios::app);

        if(!start_index_file.is_open()) {
          throw std::runtime_error("Failed to open start_index_filename files: " + start_index_filename);
        }
    }

    /*
     * Our virtual destructor.
     */
    getSignal_impl::~getSignal_impl()
    {
    }

    int getSignal_impl::general_work(int noutput_items,
                                    gr_vector_int& ninput_items,
                                    gr_vector_const_void_star& input_items,
                                    gr_vector_void_star& output_items)
    {
        const gr_complex* in = (const gr_complex*)input_items[0];
        int nin = ninput_items[0];

        // 1) collect tags that fall within the input window we are about to consume
        std::vector<gr::tag_t> tags;
        const uint64_t abs_start = nitems_read(0);
        const uint64_t abs_end   = abs_start + nin;   // end is exclusive
        get_tags_in_range(tags, 0, abs_start, abs_end);
        // signal_file << "tags_found=" << tags.size() << " abs_start=" << abs_start << " nin=" << nin << "\n";
        // start_index_file  << "tags_found=" << tags.size() << " abs_start=" << abs_start << " nin=" << nin << "\n";
        start_index_file.flush();
        signal_file.flush();
        // start_index_file << "tags_found=" << tags.size()
        //          << " abs_start=" << abs_start
        //          << " nin=" << nin << "\n";
        // 2) write tags to start_index file (filter to the key you want)
        for (const auto& t : tags) {
            const std::string key = pmt::symbol_to_string(t.key);
            if (key == "wifi_start") {
                start_index_file
                  << "offset=" << t.offset
                  << " key=" << key
                  << " coarse_cfo=" << pmt::write_string(t.value)
                  << "\n";

            }
            // if (key == "wifi_start_raw_long") {
            //     start_index_file
            //       << "offset=" << t.offset
            //       << " key=" << key
            //       << " value=" << pmt::write_string(t.value)
            //       << "\n";

            // }
            // if (key == "packet_len") {
            //     start_index_file
            //       << "offset=" << t.offset
            //       << " key=" << key
            //       << " value=" << pmt::write_string(t.value)
            //       << "\n";

            // }


        }
        start_index_file.flush();
        signal_file.flush();


        // here i need to get all the tags in the input and write these tags 
        // to start_index file

        for (int item = 0; item < nin; item++) {
          for (int k = 0; k < d_input_len; k++) {
            signal_file << in[item * d_input_len + k] << "\n";
          }
        }

        signal_file << "xx" << "\n";


        consume_each(nin);
        return 0;   // no output produced
    }


  } /* namespace customModule */
} /* namespace gr */
