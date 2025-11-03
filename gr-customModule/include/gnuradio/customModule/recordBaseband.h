/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_RECORDBASEBAND_H
#define INCLUDED_CUSTOMMODULE_RECORDBASEBAND_H
#include <gnuradio/io_signature.h>
#include <gnuradio/customModule/api.h>
#include <gnuradio/block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API recordBaseband : virtual public gr::block
    {
     public:
      typedef std::shared_ptr<recordBaseband> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::recordBaseband.
       *
       * To avoid accidental use of raw pointers, customModule::recordBaseband's
       * constructor is in a private implementation
       * class. customModule::recordBaseband::make is the public interface for
       * creating new instances.
       */
      static sptr make(int header_len,
                        int items_per_symbol,
                        int guard_interval,
                        const std::string& length_tag_key,
                        const std::string& trigger_tag_key,
                        bool output_symbols,
                        size_t itemsize,
                        size_t cfo_itemsize,
                        const std::string& timing_tag_key,
                        const double samp_rate,
                        const std::vector<std::string>& special_tags,
                        const size_t header_padding,
                        const std::string& payload_filename,
                        const std::string& cfo_filename,
                        const std::string& rawiq_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_RECORDBASEBAND_H */
