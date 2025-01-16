/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <cmath>
#include "getCFO_impl.h"
#include <gnuradio/math.h>
#include <gnuradio/fxpt.h>
#include <cmath>
#include <gnuradio/io_signature.h>
#include <gnuradio/customModule/getCFO.h>

#define F_PI ((float)(GR_M_PI))  
namespace gr {
  namespace customModule {

    using input_type = float;
    getCFO::sptr
    getCFO::make(float sensitivity, const std::string& freq_offsets_filename)
    {
      return gnuradio::make_block_sptr<getCFO_impl>(sensitivity, freq_offsets_filename);
    }


    /*
     * The private constructor
     */
    getCFO_impl::getCFO_impl(float sensitivity,
      const std::string& freq_offsets_filename)
      : gr::sync_block("getCFO",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0)),
              d_sensitivity(sensitivity),
              d_phase(0)
    {
           std::ofstream(freq_offsets_filename).close();
          // Open files for writing
          freq_offsets_file.open(freq_offsets_filename, std::ios::out | std::ios::app);
          if (!freq_offsets_file.is_open()) {
              throw std::runtime_error("Failed to open channel taps file: " + freq_offsets_filename);
          }
    }

    /*
     * Our virtual destructor.
     */
    getCFO_impl::~getCFO_impl()
    {
    }

    int
    getCFO_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      const float* in = (const float*)input_items[0];
      gr_complex* out =(gr_complex*)output_items[0];
 
      for (int i = 0; i < noutput_items; i++) {

        d_phase = d_phase + d_sensitivity * in[i];
        // place phase in [-pi, pi]
        // d_phase = (d_phase + F_PI, 2.0f * F_PI) - F_PI;
        d_phase = std::fmod(d_phase + F_PI, 2.0f * F_PI) - F_PI;
        float oi, oq;
        int32_t angle = gr::fxpt::float_to_fixed(d_phase);
        gr::fxpt::sincos(angle, &oq, &oi);
        // freq_offsets_file << "123123123123";
        freq_offsets_file << oi << " " << oq << " ";
        freq_offsets_file << "\n";
        // out[i] = gr_complex(oi, oq);
        }

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace customModule */
} /* namespace gr */
