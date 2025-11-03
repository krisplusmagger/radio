/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef __castxml__
#include <math.h>
#endif
#ifndef INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_H
#define INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace customModule {

    /*!
     * \brief <+description of block+>
     * \ingroup customModule
     *
     */
    class CUSTOMMODULE_API getSyncsymbol : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<getSyncsymbol> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of customModule::getSyncsymbol.
       *
       * To avoid accidental use of raw pointers, customModule::getSyncsymbol's
       * constructor is in a private implementation
       * class. customModule::getSyncsymbol::make is the public interface for
       * creating new instances.
       */
      static sptr make(const std::vector<gr_complex>& sync_symbol1,
                        const std::vector<gr_complex>& sync_symbol2,
                        int n_data_symbols,
                        int max_carr_offset,
                        bool force_one_sync_symbol,
                        const std::string& sync_filename1,
                        const std::string& sync_filename2,
                        const std::string& fine_cfo_filename);
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_H */
