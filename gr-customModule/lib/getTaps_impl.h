/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef __castxml__
#include <math.h>
#endif
#ifndef INCLUDED_CUSTOMMODULE_GETTAPS_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETTAPS_IMPL_H

#include <gnuradio/customModule/getTaps.h>

namespace gr {
namespace customModule {

class getTaps_impl : public getTaps
{
private:
    // const int d_fft_len;
    const int d_cp_len;
    // digital::ofdm_equalizer_base::sptr d_eq;
    bool d_propagate_channel_state;
    const int d_fixed_frame_len;
    // const std::string& chaneel_taps_filename;
    // const std::string& carrier_offsets_filename;
    std::vector<gr_complex>d_channel_state;    
    std::vector<std::vector<gr_complex>> d_all_channel_taps; // For storing all ofdm_sync_chan_taps
    std::vector<int> d_all_carrier_offsets; // For storing all ofdm_sync_carr_offset values
    std::ofstream channel_taps_file;
    std::ofstream carrier_offset_file;


public:
    getTaps_impl(
                int fft_len,
                int cp_len,
                bool propagate_channel_state,
                int fixed_frame_len,
                const std::string& channel_taps_filename,
                const std::string& carrier_offsets_filename
                );
    int work(int noutput_items,
         gr_vector_const_void_star &input_items,
         gr_vector_void_star &output_items) override;

    ~getTaps_impl();


    // Where all the action really happens
    int work(int noutput_items,
            gr_vector_int& ninput_items,
            gr_vector_const_void_star& input_items,
            gr_vector_void_star& output_items);
};

} // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETTAPS_IMPL_H */
