/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETCFO_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETCFO_IMPL_H

#include <gnuradio/customModule/getCFO.h>

namespace gr {
  namespace customModule {

    class getCFO_impl : public getCFO
    {
     private:
     float d_sensitivity;
     float d_phase;
     std::ofstream freq_offsets_file;

     public:
      getCFO_impl(float sensitivity, 
                  const std::string& freq_offsets_filename);   

      ~getCFO_impl() override;

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      ) override;
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETCFO_IMPL_H */
