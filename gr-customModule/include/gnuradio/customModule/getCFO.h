/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef __castxml__
#include <math.h>
#endif
#ifndef INCLUDED_CUSTOMMODULE_GETCFO_H
#define INCLUDED_CUSTOMMODULE_GETCFO_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getCFO : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<getCFO> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getCFO.
       *
       * To avoid accidental use of raw pointers, customModule::getCFO's
       * constructor is in a private implementation
       * class. customModule::getCFO::make is the public interface for
       * creating new instances.
       */
      static sptr make(
        float sensitivity,
        const std::string& freq_offsets_filename
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETCFO_H */
