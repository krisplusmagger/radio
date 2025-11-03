/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_SERLIZSIG_H
#define INCLUDED_CUSTOMMODULE_SERLIZSIG_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/tagged_stream_block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API Serlizsig : virtual public gr::tagged_stream_block
    {
     public:
      typedef std::shared_ptr<Serlizsig> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::Serlizsig.
       *
       * To avoid accidental use of raw pointers, customModule::Serlizsig's
       * constructor is in a private implementation
       * class. customModule::Serlizsig::make is the public interface for
       * creating new instances.
       */
      static sptr make(int fft_len,
                      const std::vector<std::vector<int>>& occupied_carriers,
                      const std::string& len_tag_key,
                      const std::string& packet_len_tag_key,
                      int symbols_skipped,
                      const std::string& carr_offset_key,
                      bool input_is_shifted,
                      const std::string& signal_filename,
                      const std::string& channel_taps_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_SERLIZSIG_H */
