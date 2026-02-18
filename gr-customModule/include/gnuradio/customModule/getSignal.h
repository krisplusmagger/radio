/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETSIGNAL_H
#define INCLUDED_CUSTOMMODULE_GETSIGNAL_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getSignal : virtual public gr::block
    {
     public:
      typedef std::shared_ptr<getSignal> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getSignal.
       *
       * To avoid accidental use of raw pointers, customModule::getSignal's
       * constructor is in a private implementation
       * class. customModule::getSignal::make is the public interface for
       * creating new instances.
       */
      static sptr make(
          size_t item_size, 
          const std::string& signal_filename,
        const std::string& start_index_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETSIGNAL_H */
