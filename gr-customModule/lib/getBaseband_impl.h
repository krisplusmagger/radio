/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETBASEBAND_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETBASEBAND_IMPL_H

#include <gnuradio/customModule/getBaseband.h>

namespace gr {
  namespace customModule {

    class getBaseband_impl : public getBaseband
    {
     private:
     std::ofstream baseband_file;
     int d_fft_len;
      
     public:
      getBaseband_impl(int fft_len, const std::string& baseband_filename);
      ~getBaseband_impl();

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETBASEBAND_IMPL_H */
