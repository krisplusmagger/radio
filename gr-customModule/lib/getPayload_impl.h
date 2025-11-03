/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETPAYLOAD_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETPAYLOAD_IMPL_H

#include <gnuradio/customModule/getPayload.h>

namespace gr {
  namespace customModule {

    class getPayload_impl : public getPayload
    {
     private:
      std::ofstream payload_file;
      int d_fft_len;

     public:
      getPayload_impl(int fft_len, const std::string& payload_filename);
      ~getPayload_impl();

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETPAYLOAD_IMPL_H */
