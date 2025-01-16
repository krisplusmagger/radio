/* -*- c++ -*- */
/*
 * Copyright 2024 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef __castxml__
#include <math.h>
#endif
#ifndef INCLUDED_CUSTOMMODULE_GETTAPS_H
#define INCLUDED_CUSTOMMODULE_GETTAPS_H

#include <gnuradio/customModule/api.h>
#include <gnuradio/sync_block.h>
#include <gnuradio/digital/ofdm_equalizer_base.h>
namespace gr {
namespace customModule {

/*!
 * \brief <+description of block+>
 * \ingroup customModule
 *
 */
class CUSTOMMODULE_API getTaps : virtual public gr::sync_block
{
public:
    typedef std::shared_ptr<getTaps> sptr;

    /*!
     * \brief Return a shared_ptr to a new instance of customModule::getTaps.
     *
     * To avoid accidental use of raw pointers, customModule::getTaps's
     * constructor is in a private implementation
     * class. customModule::getTaps::make is the public interface for
     * creating new instances.
     */

    static sptr make( 
                       int fft_len,
                       int cp_len,
                       bool propagate_channel_state,
                       int fixed_frame_len,
                       const std::string& channel_taps_filename,
                       const std::string& carrier_offsets_filename
                       );
};

} // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETTAPS_H */
