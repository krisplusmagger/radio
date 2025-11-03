/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_IMPL_H
#define INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_IMPL_H

#include <gnuradio/customModule/getSyncsymbol.h>

namespace gr {
  namespace customModule {

    class getSyncsymbol_impl : public getSyncsymbol
    {
     private:
       int d_fft_len;     //! FFT length
        int d_n_data_syms; //! Number of data symbols following the sync symbol(s)
        int d_n_sync_syms; //! Number of sync symbols (1 or 2)
        //! 0 if no noise reduction is done for the initial channel state estimation.
        //! Otherwise, the maximum length of the channel delay in samples.
        int d_eq_noise_red_len;
        //! Is sync_symbol1 if d_n_sync_syms == 1, otherwise sync_symbol2. Used as a reference
        //! symbol to estimate the channel.
        std::vector<gr_complex> d_ref_sym;
        //! If d_n_sync_syms == 2 this is used as a differential correlation vector (called
        //! 'v' in [1]).
        std::vector<gr_complex> d_corr_v;
        //! If d_n_sync_syms == 1 we use this instead of d_corr_v to estimate the coarse freq.
        //! offset
        std::vector<float> d_known_symbol_diffs;
        //! If d_n_sync_syms == 1 we use this instead of d_corr_v to estimate the coarse freq.
        //! offset (temp. variable)
        std::vector<float> d_new_symbol_diffs;
        //! The index of the first carrier with data (index 0 is not DC here, but the lowest
        //! frequency)
        int d_first_active_carrier;
        //! The index of the last carrier with data
        int d_last_active_carrier;
        //! If true, the channel estimation must be interpolated
        bool d_interpolate;
        //! Maximum carrier offset (negative value!)
        int d_max_neg_carr_offset;
        //! Maximum carrier offset (positive value!)
        int d_max_pos_carr_offset;
        std::ofstream sync_file1;
        std::ofstream sync_file2;
        std::ofstream fine_cfo_file;

        //! Calculate the coarse frequency offset in number of carriers
        int get_carr_offset(const gr_complex* sync_sym1, const gr_complex* sync_sym2);
        //! Estimate the channel (phase and amplitude offset per carrier)
        void get_syncwords(const gr_complex* sync_sym1,
                          const gr_complex* sync_sym2,
                          int carr_offset,
                          std::vector<gr_complex>& taps);


     public:
      getSyncsymbol_impl(const std::vector<gr_complex>& sync_symbol1,
                         const std::vector<gr_complex>& sync_symbol2,
                         int n_data_symbols,
                         int max_carr_offset,
                         bool force_one_sync_symbol,
                         const std::string& sync_filename1,
                         const std::string& sync_filename2,
                         const std::string& fine_cfo_filename);
      ~getSyncsymbol_impl(); 

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_GETSYNCSYMBOL_IMPL_H */
