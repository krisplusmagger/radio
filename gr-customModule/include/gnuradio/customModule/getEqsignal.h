/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETEQSIGNAL_H
#define INCLUDED_CUSTOMMODULE_GETEQSIGNAL_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getEqsignal : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<getEqsignal> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getEqsignal.
       *
       * To avoid accidental use of raw pointers, customModule::getEqsignal's
       * constructor is in a private implementation
       * class. customModule::getEqsignal::make is the public interface for
       * creating new instances.
       */
      static sptr make(int fft_len, const std::string& eqsignal_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETEQSIGNAL_H */
