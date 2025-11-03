/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_H
#define INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/tagged_stream_block.h>
#include <gnuradio/digital/ofdm_equalizer_base.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getEqlizedsig : virtual public gr::tagged_stream_block
    {
     public:
      typedef std::shared_ptr<getEqlizedsig> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getEqlizedsig.
       *
       * To avoid accidental use of raw pointers, customModule::getEqlizedsig's
       * constructor is in a private implementation
       * class. customModule::getEqlizedsig::make is the public interface for
       * creating new instances.      getEqlizedsig_impl(
                         int fft_len,
                         const std::string& equlizedsig_filename,
                         const std::string& carr_offset_key
      );
       */
      static sptr make( gr::digital::ofdm_equalizer_base::sptr equalizer,
                        const std::string& equlizedsig_filename,
                        int fft_len,
                        int cp_len,
                        const std::string& tsb_key);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETEQLIZEDSIG_H */
