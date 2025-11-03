/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "getSyncsymbol_impl.h"

namespace gr {
  namespace customModule {

    using input_type = gr_complex;

    // using output_type = gr_complex;
    getSyncsymbol::sptr
    getSyncsymbol::make(const std::vector<gr_complex>& sync_symbol1,
                        const std::vector<gr_complex>& sync_symbol2,
                        int n_data_symbols,
                        int max_carr_offset,
                        bool force_one_sync_symbol,
                        const std::string& sync_filename1,
                        const std::string& sync_filename2,
                        const std::string& fine_cfo_filename)
    {
      return gnuradio::make_block_sptr<getSyncsymbol_impl>(sync_symbol1,
                                                             sync_symbol2,
                                                             n_data_symbols,
                                                             max_carr_offset,
                                                             force_one_sync_symbol,
                                                             sync_filename1,
                                                             sync_filename2,
                                                             fine_cfo_filename
        );
    }


    /*
     * The private constructor
     */
    getSyncsymbol_impl::getSyncsymbol_impl(
        const std::vector<gr_complex>& sync_symbol1,
        const std::vector<gr_complex>& sync_symbol2,
        int n_data_symbols,
        int max_carr_offset,
        bool force_one_sync_symbol,
        const std::string& sync_filename1,
        const std::string& sync_filename2,
        const std::string& fine_cfo_filename

    )
      : gr::sync_block("getSyncsymbol",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(gr_complex) * sync_symbol1.size()),
              gr::io_signature::make(0 /* min outputs */, 0 /*max outputs */, 0)),
              d_fft_len(sync_symbol1.size()),
              d_n_data_syms(n_data_symbols),
              d_n_sync_syms(1),
              // d_eq_noise_red_len(eq_noise_red_len),
              d_ref_sym((!sync_symbol2.empty() && !force_one_sync_symbol) ? sync_symbol2
                                                                          : sync_symbol1),
              d_corr_v(sync_symbol2),
              d_known_symbol_diffs(0, 0),
              d_new_symbol_diffs(0, 0),
              d_first_active_carrier(0),
              d_last_active_carrier(sync_symbol2.size() - 1),
              d_interpolate(false)
    {   
        std::ofstream(sync_filename1).close();
        std::ofstream(sync_filename2).close();
        std::ofstream(fine_cfo_filename).close();

        //openfiles  for writing
        sync_file1.open(sync_filename1, std::ios::out | std::ios::app);
        if(!sync_file1.is_open()) {
          throw std::runtime_error("Failed to oepn sync1 file: " + sync_filename1);
        }
        sync_file2.open(sync_filename2, std::ios::out | std::ios::app);
        if(!sync_file2.is_open()) {
          throw std::runtime_error("Failed to open sync2 file" + sync_filename2);
        }
        fine_cfo_file.open(fine_cfo_filename, std::ios::out | std::ios::app);
        if(!fine_cfo_file.is_open()) {
          throw std::runtime_error("Failed to open cfo file" + fine_cfo_filename);
        }
        
              // Set index of first and last active carrier
        for (int i = 0; i < d_fft_len; i++) {
            if (d_ref_sym[i] != gr_complex(0, 0)) {
                d_first_active_carrier = i;
                break;
            }
        }
        for (int i = d_fft_len - 1; i >= 0; i--) {
            if (d_ref_sym[i] != gr_complex(0, 0)) {
                d_last_active_carrier = i;
                break;
            }
        }
            // Sanity checks
        if (!sync_symbol2.empty()) {
            if (sync_symbol1.size() != sync_symbol2.size()) {
                throw std::invalid_argument("sync symbols must have equal length.");
            }
            if (!force_one_sync_symbol) {
                d_n_sync_syms = 2;
            }
        } else {
            if (sync_symbol1[d_first_active_carrier + 1] == gr_complex(0, 0)) {
                d_last_active_carrier++;
                d_interpolate = true;
            }
        }

        // Set up coarse freq estimation info
        // Allow all possible values:
        d_max_neg_carr_offset = -d_first_active_carrier;
        d_max_pos_carr_offset = d_fft_len - d_last_active_carrier - 1;
        if (max_carr_offset != -1) {
            d_max_neg_carr_offset = std::max(-max_carr_offset, d_max_neg_carr_offset);
            d_max_pos_carr_offset = std::min(max_carr_offset, d_max_pos_carr_offset);
        }
        // Carrier offsets must be even
        if (d_max_neg_carr_offset % 2)
            d_max_neg_carr_offset++;
        if (d_max_pos_carr_offset % 2)
            d_max_pos_carr_offset--;

        if (d_n_sync_syms == 2) {
            for (int i = 0; i < d_fft_len; i++) {
                if (sync_symbol1[i] == gr_complex(0, 0)) {
                    d_corr_v[i] = gr_complex(0, 0);
                } else {
                    d_corr_v[i] /= sync_symbol1[i];
                }
            }
        } else {
            d_corr_v.resize(0, 0);
            d_known_symbol_diffs.resize(d_fft_len, 0);
            d_new_symbol_diffs.resize(d_fft_len, 0);
            for (int i = d_first_active_carrier;
                i < d_last_active_carrier - 2 && i < d_fft_len - 2;
                i += 2) {
                d_known_symbol_diffs[i] = std::norm(sync_symbol1[i] - sync_symbol1[i + 2]);
            }
        }
    }

    /*
     * Our virtual destructor.
     */
    getSyncsymbol_impl::~getSyncsymbol_impl() {}
    
    int getSyncsymbol_impl::get_carr_offset(const gr_complex* sync_sym1,
                                            const gr_complex* sync_sym2)
    {
        int carr_offset = 0;
        if (!d_corr_v.empty()) {
            // Use Schmidl & Cox method
            float Bg_max = 0;
            // g here is 2g in the paper
            for (int g = d_max_neg_carr_offset; g <= d_max_pos_carr_offset; g += 2) {
                gr_complex tmp = gr_complex(0, 0);
                for (int k = 0; k < d_fft_len; k++) {
                    if (d_corr_v[k] != gr_complex(0, 0)) {
                        tmp += std::conj(sync_sym1[k + g]) * std::conj(d_corr_v[k]) *
                              sync_sym2[k + g];
                    }
                }
                if (std::abs(tmp) > Bg_max) {
                    Bg_max = std::abs(tmp);
                    carr_offset = g;
                }
            }
        } else {
            // Correlate
            std::fill(d_new_symbol_diffs.begin(), d_new_symbol_diffs.end(), 0);
            for (int i = 0; i < d_fft_len - 2; i++) {
                d_new_symbol_diffs[i] = std::norm(sync_sym1[i] - sync_sym1[i + 2]);
            }

            float sum;
            float max = 0;
            for (int g = d_max_neg_carr_offset; g <= d_max_pos_carr_offset; g += 2) {
                sum = 0;
                for (int j = 0; j < d_fft_len; j++) {
                    if (d_known_symbol_diffs[j]) {
                        sum += (d_known_symbol_diffs[j] * d_new_symbol_diffs[j + g]);
                    }
                    if (sum > max) {
                        max = sum;
                        carr_offset = g;
                    }
                }
            }
        }
        return carr_offset;
    } 

    void getSyncsymbol_impl::get_syncwords(const gr_complex* sync_sym1,
                                              const gr_complex* sync_sym2,
                                              int carr_offset,
                                              std::vector<gr_complex>& taps)
    {
        // const gr_complex* sym = ((d_n_sync_syms == 2) ? sync_sym2 : sync_sym1);
        const gr_complex* sym1 = sync_sym1;
        const gr_complex* sym2 = sync_sym2;
        std::vector<gr_complex> sym1_temp(d_fft_len, gr_complex(0, 0));
        std::vector<gr_complex> sym2_temp(d_fft_len, gr_complex(0, 0)); 
        std::fill(taps.begin(), taps.end(), gr_complex(0, 0));
        int loop_start = 0;
        int loop_end = d_fft_len;
        if (carr_offset > 0) {
            loop_start = carr_offset;
        } else if (carr_offset < 0) {
            loop_end = d_fft_len + carr_offset;
        }
        // std::cout << "loop start: " << loop_start << ", loop end: " << loop_end << std::endl;
    
        for (int i = loop_start; i < loop_end; i++) {
            if ((d_ref_sym[i - carr_offset] != gr_complex(0 ,0))) {
              sym1_temp[i - carr_offset] = sym1[i];
              sym2_temp[i - carr_offset] = sym2[i];
            }
        }
        for(int i = 0; i < d_fft_len; i++){
          // store the two received symbols sym1 and sym2 after carrier_offset correction into two files 
          // namely syn1.txt and syn2.txt
            sync_file1 << sym1_temp[i] << std::endl;
            sync_file2 << sym2_temp[i] << std::endl;
        }
        sync_file1 << "\n";
        sync_file2 << "\n";
    }

    int
    getSyncsymbol_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
   
      const gr_complex* in = (const gr_complex*)input_items[0];
      gr_complex* out = (gr_complex*)output_items[0];

      // Channel info estimation
      int carr_offset = get_carr_offset(in, in + d_fft_len);


    //   std::vector<gr_complex> chan_taps(d_fft_len, 0);
    //   get_syncwords(in, in + d_fft_len, carr_offset, chan_taps); //in - > sync1,  in + d_fft_len -> sync2
          //   sync_file2 << "noutput_items: "  << noutput_items;

        //   sync_file1 <<  "Fine grained CFO: starting: " <<  "\n" <<std::endl;
        sync_file1 <<  carr_offset  <<std::endl;
        sync_file1 << "\n" << std::endl;
    //   sync_file1 <<  "Fine grained CFO ending" <<  "\n" <<std::endl;
    //   sync_file1 << "\n";


      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace customModule */
} /* namespace gr */
