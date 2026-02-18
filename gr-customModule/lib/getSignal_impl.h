/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETSIGNAL_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETSIGNAL_IMPL_H

#include <gnuradio/customModule/getSignal.h>

namespace gr {
  namespace customModule {

    class getSignal_impl : public getSignal
    {
     private:

       const size_t d_item_size;
       std::ofstream signal_file;
       std::ofstream start_index_file;

     public:
      getSignal_impl(size_t d_item_size, 
                     const std::string& signal_filename,
                    const std::string& start_index_filename);
      ~getSignal_impl();

      // Where all the action really happens
    int general_work(int noutput_items,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items) override;
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETSIGNAL_IMPL_H */
