/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#ifndef INCLUDED_CUSTOMMODULE_RECORDBASEBAND_IMPL_H
#define INCLUDED_CUSTOMMODULE_RECORDBASEBAND_IMPL_H
#include <gnuradio/io_signature.h>
#include <gnuradio/customModule/recordBaseband.h>

namespace gr {
  namespace customModule {

    class recordBaseband_impl : public recordBaseband
    {
     private:
      int d_header_len;                       //!< Number of bytes per header
      const int d_header_padding_symbols;     //!< Symbols header padding
      const int d_header_padding_items;       //!< Items header padding
      const int d_header_padding_total_items; //!< Items header padding
      int d_items_per_symbol;                 //!< Bytes per symbol
      int d_gi;                               //!< Bytes per guard interval
      pmt::pmt_t d_len_tag_key;               //!< Key of length tag
      pmt::pmt_t d_trigger_tag_key;           //!< Key of trigger tag (if used)
      bool d_output_symbols;                  //!< If true, output is symbols, not items
      size_t d_itemsize;                      //!< Bytes per item
      size_t d_cfo_itemsize;                  //!< Bytes per cfo item
      bool d_uses_trigger_tag;                //!< If a trigger tag is used
      int d_state;                            //!< Current read state
      int d_curr_payload_len;                 //!< Length of the next payload (symbols)
      int d_curr_payload_offset;              //!< Offset of the next payload (symbols)
      std::vector<pmt::pmt_t>
          d_payload_tag_keys; //!< Temporary buffer for PMTs that go on the payload (keys)
      std::vector<pmt::pmt_t> d_payload_tag_values; //!< Temporary buffer for PMTs that go
                                                    //!< on the payload (values)
      bool d_track_time;               //!< Whether or not to keep track of the rx time
      pmt::pmt_t d_timing_key;         //!< Key of the timing tag (usually 'rx_time')
      pmt::pmt_t d_payload_offset_key; //!< Key of payload offset (usually 'payload_offset')
      uint64_t d_last_time_offset;     //!< Item number of the last time tag
      pmt::pmt_t d_last_time;          //!< The actual time that was indicated
      double d_sampling_time;          //!< Inverse sampling rate
      std::vector<pmt::pmt_t> d_special_tags; //!< List of special tags
      std::vector<pmt::pmt_t>
      d_special_tags_last_value; //!< The current value of the special tags
      std::vector<float> d_cfo_buffer;
      std::ofstream payload_file;
      std::ofstream cfo_file;
      static const pmt::pmt_t msg_port_id(); //!< Message Port Id
      void parse_header_data_msg(pmt::pmt_t header_data);

      int find_trigger_signal(int skip_items,
                             int max_rel_offset,
                             uint64_t base_offset,
                             const unsigned char *in_trigger);
      bool check_buffers_ready(int output_symbols_reqd,
                              int extra_output_items_reqd,
                              int noutput_items,
                              int input_items_reqd,
                              gr_vector_int& ninput_items,
                              int n_items_read);
      void update_special_tags(uint64_t range_start, uint64_t range_end);
      void copy_n_symbols(const unsigned char* in,
                          unsigned char* out,
                          int port,
                          const uint64_t n_items_read_base,
                          int n_symbols,
                          int n_padding_items = 0);
      void add_special_tags();                   

     public:
      recordBaseband_impl(int header_len,
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
                          const std::string& cfo_filename);
      ~recordBaseband_impl();

      // Where all the action really happens
      // void forecast (int noutput_items, gr_vector_int &ninput_items_required);
      void forecast(int noutput_items, gr_vector_int& ninput_items_required) override;



      int general_work(int noutput_items,
           gr_vector_int &ninput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);

    };

  } // namespace customModule
} // namespace gr

#endif /* INCLUDED_CUSTOMMODULE_RECORDBASEBAND_IMPL_H */
