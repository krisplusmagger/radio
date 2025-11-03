/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETEQSIGNAL_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETEQSIGNAL_IMPL_H

#include <gnuradio/customModule/getEqsignal.h>

namespace gr {
  namespace customModule {

    class getEqsignal_impl : public getEqsignal
    {
     private:
      std::ofstream eqsignal_file;
      

     public:
      getEqsignal_impl(int fft_len, const std::string& eqsignal_filename);
      ~getEqsignal_impl();

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETEQSIGNAL_IMPL_H */
