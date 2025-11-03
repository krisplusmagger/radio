/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETBASEBAND_H
#define INCLUDED_CUSTOMMODULE_GETBASEBAND_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getBaseband : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<getBaseband> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getBaseband.
       *
       * To avoid accidental use of raw pointers, customModule::getBaseband's
       * constructor is in a private implementation
       * class. customModule::getBaseband::make is the public interface for
       * creating new instances.
       */
      static sptr make(int fft_len, const std::string& baseband_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETBASEBAND_H */
