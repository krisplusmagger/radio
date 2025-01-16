/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <cmath>

#include "getTaps_impl.h"
#include <gnuradio/expj.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/math.h>
#include <gnuradio/digital/ofdm_equalizer_base.h>
#include <gnuradio/customModule/getTaps.h>
namespace gr {
namespace customModule {

using input_type = gr_complex;
getTaps::sptr 
getTaps::make(
     
                int fft_len,
                int cp_len,
                bool propagate_channel_state,
                int fixed_frame_len,
                const std::string& channel_taps_filename,
                const std::string& carrier_offsets_filename
                ) 
{ 
    // std::cout << "working1" <<std::endl;
    return gnuradio::make_block_sptr<getTaps_impl>(fft_len,
        cp_len, propagate_channel_state, fixed_frame_len, 
        channel_taps_filename, carrier_offsets_filename); 
}


/*
 * The private constructor
 */
getTaps_impl::getTaps_impl(

    int fft_len,
    int cp_len,
    bool propagate_channel_state,
    int fixed_frame_len,
    const std::string& channel_taps_filename,
    const std::string& carrier_offsets_filename
    )
    : gr::sync_block("getTaps",
                     gr::io_signature::make(
                         1 /* min inputs */, 1 /* max inputs */, sizeof(gr_complex) * fft_len),
                     gr::io_signature::make(0, 0, 0)),

     d_cp_len(cp_len),

     d_propagate_channel_state(propagate_channel_state),
     d_fixed_frame_len(fixed_frame_len),
     d_channel_state(fft_len, gr_complex(1,0))                
  
{   
    std::cout << "working" <<std::endl;

    if (d_fixed_frame_len < 0) {
        throw std::invalid_argument("Invalid frame length!");
    }
    // Create the files by opening them in "out" mode and immediately closing
    std::ofstream(channel_taps_filename).close();
    std::ofstream(carrier_offsets_filename).close();

    // Open files for writing
    channel_taps_file.open(channel_taps_filename, std::ios::out | std::ios::app);
    if (!channel_taps_file.is_open()) {
        throw std::runtime_error("Failed to open channel taps file: " + channel_taps_filename);
    }

    carrier_offset_file.open(carrier_offsets_filename, std::ios::out | std::ios::app);
    if (!carrier_offset_file.is_open()) {
        throw std::runtime_error("Failed to open carrier offsets file: " + carrier_offsets_filename);
    }
}

/*
 * virtual destructor.
 */
getTaps_impl::~getTaps_impl() {}

int getTaps_impl::work(int noutput_items, // the length in items of all input and outpout buffers
                    gr_vector_const_void_star &input_items,
                    gr_vector_void_star &output_items)
{
    const gr_complex* in = (const gr_complex*)input_items[0];
    gr_complex* out =(gr_complex*)output_items[0];
    int carrier_offset = 0;
    int frame_len = 0;
 
    frame_len = d_fixed_frame_len ? d_fixed_frame_len : noutput_items; // Adjust as needed


    
    std::vector<tag_t> tags;
    get_tags_in_window(tags, 0, 0, 1);
    for (unsigned i = 0; i < tags.size(); i++) {
        if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_chan_taps") {
            d_channel_state = pmt::c32vector_elements(tags[i].value);// d_channel-state --> channel_taps
            
            for (const auto& tap: d_channel_state) {
                channel_taps_file << tap.real() << " " << tap.imag() << " ";
            }
            channel_taps_file << "\n";
        }
        if (pmt::symbol_to_string(tags[i].key) == "ofdm_sync_carr_offset") {
            
            carrier_offset = pmt::to_long(tags[i].value);//print out this value

            carrier_offset_file << carrier_offset << " ";
            carrier_offset_file << "\n";
      
        }
        
    }


    // Tell runtime system how many output items produced.
    return noutput_items;
}

} /* namespace customModule */
} /* namespace gr */
