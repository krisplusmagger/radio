/* -*- c++ -*- */
/*
 * Copyright 2025 xinyu.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "recordBaseband_impl.h"
#include <climits>
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------------
// New: Define input port indices for the four input ports:
// - Port 0: inputdata (original data input)
// - Port 1: trigger (original trigger input)
// - Port 2: (unused or additional port, remains unchanged)
// - Port 3: cfo (new input port carrying CFO values from Frequency Mod)
#define IN_PORT_INPUTDATA 0
#define IN_PORT_TRIGGER   1
#define IN_PORT_UNUSED    2
#define IN_PORT_CFO       3

namespace gr {
  namespace customModule {

const pmt::pmt_t recordBaseband_impl::msg_port_id()
{
    static const pmt::pmt_t msg_port_id = pmt::mp("header_data");
    return msg_port_id;
}


//! Returns a PMT time tuple (uint seconds, double fraction) as the sum of
//  another PMT time tuple and a time diff in seconds.
pmt::pmt_t _update_pmt_time(pmt::pmt_t old_time, double time_difference)
{
    double diff_seconds;
    double diff_frac = modf(time_difference, &diff_seconds);
    uint64_t seconds =
        pmt::to_uint64(pmt::tuple_ref(old_time, 0)) + (uint64_t)diff_seconds;
    double frac = pmt::to_double(pmt::tuple_ref(old_time, 1)) + diff_frac;
    return pmt::make_tuple(pmt::from_uint64(seconds), pmt::from_double(frac));
}


enum demux_states_t {
  STATE_FIND_TRIGGER,      // "Idle" state (waiting for burst)
  STATE_HEADER,            // Copy header
  STATE_WAIT_FOR_MSG,      // Null state (wait until msg from header demod)
  STATE_HEADER_RX_SUCCESS, // Header processing
  STATE_HEADER_RX_FAIL,    //       "
  STATE_PAYLOAD            // Copy payload
};
    
    // enum in_port_indexes_t {
    //   PORT_INPUTDATA = 0,
    //   PORT_TRIGGER = 1
    // };
    
    enum out_port_indexes_t {
      PORT_HEADER = 0,
      PORT_PAYLOAD = 1,
      // Note: The following names were previously used for input port indices.
      // They remain here for legacy references in functions like copy_n_symbols().
      PORT_INPUTDATA = 0,
      PORT_TRIGGER = 1
  };
  

    recordBaseband::sptr
    recordBaseband::make(int header_len,
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
                        const std::string& cfo_filename)
    {
      return gnuradio::make_block_sptr<recordBaseband_impl>(
                                                            header_len,
                                                            items_per_symbol,
                                                            guard_interval,
                                                            length_tag_key,
                                                            trigger_tag_key,
                                                            output_symbols,
                                                            itemsize,
                                                            cfo_itemsize,
                                                            timing_tag_key,
                                                            samp_rate,
                                                            special_tags,
                                                            header_padding,
                                                            payload_filename,
                                                            cfo_filename);
    }


    /*
     * The private constructor
     */
    recordBaseband_impl::recordBaseband_impl(
      int header_len,
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
      const std::string& cfo_filename)
      : gr::block("recordBaseband",
            // Create a vector of 4 input ports:
            // Port 0: inputdata (itemsize), Port 1: trigger (sizeof(char)),
            // Port 2: unchanged (itemsize), Port 3: cfo (sizeof(float))
            io_signature::makev(4, 4,
              std::vector<int>{ (int)itemsize, (int)sizeof(char), (int)itemsize, (int)sizeof(float) }),
            io_signature::make(2, 2, (output_symbols ? itemsize * items_per_symbol : itemsize))),
              d_itemsize(itemsize),
              d_cfo_itemsize(cfo_itemsize),
              d_header_len(header_len),
              d_header_padding_symbols(header_padding / items_per_symbol),
              d_header_padding_items(header_padding % items_per_symbol),
              d_header_padding_total_items(header_padding),
              d_items_per_symbol(items_per_symbol),
              d_gi(guard_interval),
              d_len_tag_key(pmt::string_to_symbol(length_tag_key)),
              d_trigger_tag_key(pmt::string_to_symbol(trigger_tag_key)),
              d_output_symbols(output_symbols),
              d_uses_trigger_tag(!trigger_tag_key.empty()),
              d_state(STATE_FIND_TRIGGER),
              d_curr_payload_len(0),
              d_curr_payload_offset(0),
              d_payload_tag_keys(0),
              d_payload_tag_values(0),
              d_track_time(!timing_tag_key.empty()),
              d_timing_key(pmt::intern(timing_tag_key)),
              d_payload_offset_key(pmt::intern("payload_offset")),
              d_last_time_offset(0),
              d_last_time(pmt::make_tuple(pmt::from_uint64(0L), pmt::from_double(0.0))),
              d_sampling_time(1.0 / samp_rate)
    {
            std::ofstream(payload_filename).close();
            std::ofstream(cfo_filename).close();
            //openfiles  for writing
            payload_file.open(payload_filename, std::ios::out | std::ios::app);
            if(!payload_file.is_open()) {
            throw std::runtime_error("Failed to oepn payload_file file: " + payload_filename);
            }
            cfo_file.open(cfo_filename, std::ios::out | std::ios::app);
            if(!cfo_file.is_open()) {
            throw std::runtime_error("Failed to open cfo_file file" + cfo_filename);
            }
          // Initialize the CFO buffer.
          d_cfo_buffer.clear();

          if (d_header_len < 1) {
              throw std::invalid_argument("Header length must be at least 1 symbol.");
          }
          if (d_items_per_symbol < 1 || d_gi < 0 || d_itemsize < 1) {
              throw std::invalid_argument("Items and symbol sizes must be at least 1.");
          }
          if (d_output_symbols) {
              set_relative_rate(1, (uint64_t)(d_items_per_symbol + d_gi));
          } else {
              set_relative_rate((uint64_t)d_items_per_symbol,
                                (uint64_t)(d_items_per_symbol + d_gi));
              set_output_multiple(d_items_per_symbol);
          }
          if ((d_output_symbols || d_gi) && d_header_padding_items) {
              throw std::invalid_argument(
                  "If output_symbols is true or a guard interval is given, padding must be a "
                  "multiple of items_per_symbol!");
          }
          set_tag_propagation_policy(TPP_DONT);
          message_port_register_in(msg_port_id());
          set_msg_handler(msg_port_id(),
                          [this](pmt::pmt_t msg) { this->parse_header_data_msg(msg); });
          for (size_t i = 0; i < special_tags.size(); i++) {
              d_special_tags.push_back(pmt::string_to_symbol(special_tags[i]));
              d_special_tags_last_value.push_back(pmt::PMT_NIL);
          }
      }
    /*
     * Our virtual destructor.
     */
    recordBaseband_impl::~recordBaseband_impl()
    {
    }

    // forecast() now sets different requirements for the new CFO port.
    void recordBaseband_impl::forecast(int noutput_items, 
                                       gr_vector_int& ninput_items_required)
    {
      int n_items_reqd = 0;
      if (d_state == STATE_HEADER) {
      n_items_reqd =
      d_header_len * (d_items_per_symbol + d_gi) + 2 * d_header_padding_total_items;
      } else if (d_state == STATE_PAYLOAD) {
      n_items_reqd = d_curr_payload_len * (d_items_per_symbol + d_gi);
      } else {
      n_items_reqd = noutput_items * (d_items_per_symbol + d_gi);
      if (!d_output_symbols) {
      n_items_reqd /= d_items_per_symbol;
      }
      }
      // Set requirements for ports 0, 1 and 2 (synchronous ports).
      ninput_items_required[IN_PORT_INPUTDATA] = n_items_reqd;
      ninput_items_required[IN_PORT_TRIGGER] = n_items_reqd;
      ninput_items_required[IN_PORT_UNUSED] = n_items_reqd;
      // For CFO port (port 3): require d_curr_payload_len items when in STATE_PAYLOAD, else 0.
    //   ninput_items_required[IN_PORT_CFO] = (d_state == STATE_PAYLOAD) ? d_curr_payload_len : 0;
      ninput_items_required[IN_PORT_CFO] = n_items_reqd;
    }

    bool recordBaseband_impl::check_buffers_ready(int output_symbols_reqd,
                                                    int extra_output_items_reqd,
                                                    int noutput_items,
                                                    int input_items_reqd,
                                                    gr_vector_int& ninput_items,
                                                    int n_items_read)
{
    // Check there's enough space on the output buffer

    if (noutput_items < output_symbols_reqd + extra_output_items_reqd) {
        return false;
    }
    // Check there's enough items on the input
    if (input_items_reqd > (ninput_items[0] - n_items_read) ||
        (ninput_items.size() == 2 &&
         (input_items_reqd > (ninput_items[1] - n_items_read)))) {
        return false;
    }

    // All good
    return true;
}

    int
    recordBaseband_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      const unsigned char *in = (const unsigned char *) input_items[IN_PORT_INPUTDATA];
      const unsigned char *in_cfo = (const unsigned char *) input_items[IN_PORT_CFO];
      const float *in_cfo_1 = (const float *) input_items[IN_PORT_CFO];
      unsigned char* out_header = (unsigned char*)output_items[PORT_HEADER];
      unsigned char* out_payload = (unsigned char*)output_items[PORT_PAYLOAD];


      const int n_input_items = ninput_items[PORT_INPUTDATA];
      // debug_file << "num of input items: " << n_input_items << std::endl;
      // debug_file << "\n";
      // Base index for the input stream.
      const uint64_t n_items_read_base = nitems_read(PORT_INPUTDATA);
      int n_items_read = 0;
      // A helper macro to update tags and consume input.
      // Modified macro: consume from ports 0,1,2 only (CFO port is handled separately).
      #define CONSUME_ITEMS(items_to_consume)                                         \
      update_special_tags(n_items_read_base + n_items_read,                       \
                        n_items_read_base + n_items_read + (items_to_consume)); \
      consume(IN_PORT_INPUTDATA, items_to_consume);                               \
      consume(IN_PORT_TRIGGER, items_to_consume);                                 \
      consume(IN_PORT_UNUSED, items_to_consume);                                  \
      consume(IN_PORT_CFO, items_to_consume);                                     \
      n_items_read += (items_to_consume);                                         \
      in += (items_to_consume)*d_itemsize;                                        \
      in_cfo += (items_to_consume)*d_cfo_itemsize;                                \
      in_cfo_1 += (items_to_consume);                                                                 

      switch (d_state) {
        case STATE_WAIT_FOR_MSG:
            // In an ideal world, this would never be called;
            // parse_header_data_msg() is the only place that can kick us out
            // of this state.
            return 0;
    
        case STATE_HEADER_RX_FAIL:
            // Consume a single item to keep stream alignment.
            CONSUME_ITEMS(1);
            d_state = STATE_FIND_TRIGGER;
            break;
    
        case STATE_FIND_TRIGGER: {
            // Look for the trigger signal after header padding.
            const int max_rel_offset = n_input_items - n_items_read;
            const int trigger_offset = find_trigger_signal(
                d_header_padding_total_items,
                max_rel_offset,
                n_items_read_base + n_items_read,
                ((input_items.size() > IN_PORT_TRIGGER) ?
                 ((const unsigned char*)input_items[IN_PORT_TRIGGER]) + n_items_read : NULL));
            if (trigger_offset < max_rel_offset) {
                d_state = STATE_HEADER;
            }
            // Consume items up to the trigger (leaving the header padding intact).
            const int items_to_consume = trigger_offset - d_header_padding_total_items;
            CONSUME_ITEMS(items_to_consume);
            break;
        } /* case STATE_FIND_TRIGGER */
    
        case STATE_HEADER: 
            // Copy header samples (including padding) to the header output port.
            if (check_buffers_ready(d_header_len + 2 * d_header_padding_symbols,
                                    d_header_padding_items,
                                    noutput_items,
                                    d_header_len * (d_items_per_symbol + d_gi) +
                                        2 * d_header_padding_total_items,
                                    ninput_items,
                                    n_items_read)) {
                add_special_tags();
                copy_n_symbols(in,
                               out_header,
                               PORT_HEADER,
                               n_items_read_base + n_items_read,
                               d_header_len +
                                   2 * d_header_padding_symbols, // Number of symbols to copy
                               2 * d_header_padding_items);
                d_state = STATE_WAIT_FOR_MSG;
            }
            break;
        
    
        case STATE_HEADER_RX_SUCCESS:
            // Propagate tags from header to payload.
            for (size_t i = 0; i < d_payload_tag_keys.size(); i++) {
                add_item_tag(PORT_PAYLOAD,
                             nitems_written(PORT_PAYLOAD),
                             d_payload_tag_keys[i],
                             d_payload_tag_values[i]);
            }
            {
                // Consume header (including one part of the padding).
                const int items_to_consume = d_header_len * (d_items_per_symbol + d_gi) +
                                             d_header_padding_total_items +
                                             d_curr_payload_offset;
                CONSUME_ITEMS(items_to_consume);
                d_curr_payload_offset = 0;
                d_state = STATE_PAYLOAD;
            }
            break;
    
        case STATE_PAYLOAD:
            // For payload processing, also check that the CFO port has enough items.
            if (ninput_items[IN_PORT_CFO] < d_curr_payload_len) {
                return 0;
            }
            if (check_buffers_ready(d_curr_payload_len,
                                    0,
                                    noutput_items,
                                    d_curr_payload_len * (d_items_per_symbol + d_gi),
                                    ninput_items,
                                    n_items_read)) {
                // Write payload from main data input.
                copy_n_symbols(in,
                               out_payload,
                               PORT_PAYLOAD,
                               n_items_read_base + n_items_read,
                               d_curr_payload_len);              
                // Record CFO values from the CFO port.
                // const float* in_cfo_1 = (const float*)input_items[IN_PORT_CFO];
                cfo_file << "CFO value: " << "\n" << std::endl;
                for (int i = 0; i < d_curr_payload_len; i++) {
                    //start at i = i + d_gi, then do the loop item_per_symbol times, then start at i + dgi + items_per_symbol
                    for (int j = 0; j < d_items_per_symbol; j++) {           
                        // d_cfo_buffer.push_back(in_cfo[i]);
                        //write the cfo value into the files here
                        cfo_file << in_cfo_1[j + (i * d_items_per_symbol + d_gi)]  << std::endl; // how to 
                    }
    
                }
                //TODO 
                // Consume payload items from the other three input ports.
                const int items_padding = std::max(d_header_padding_total_items, 1);
                const int items_to_consume =
                    d_curr_payload_len * (d_items_per_symbol + d_gi) - items_padding;
                CONSUME_ITEMS(items_to_consume); 
                set_min_noutput_items(d_output_symbols ? 1 : (d_items_per_symbol + d_gi));
                d_state = STATE_FIND_TRIGGER;
            }
            
            break;
    
        default:
            throw std::runtime_error("invalid state");
        } /* switch */
    
        return WORK_CALLED_PRODUCE;

    }

        // This function searches for a trigger signal. It works much like before,
    // except that here we start searching at index zero (i.e. no header padding).
    int recordBaseband_impl::find_trigger_signal(int skip_items,
                                                      int max_rel_offset,
                                                      uint64_t base_offset,
                                                      const unsigned char *in_trigger)
    {
        int rel_offset = max_rel_offset;
        if (max_rel_offset < skip_items) {
            return rel_offset;
        }
        if (in_trigger) {
            for (int i = skip_items; i < max_rel_offset; i++) {
                if (in_trigger[i]) {
                    rel_offset = i;
                    break;
                }
            }
        }

        return rel_offset;
    } /* find_trigger_signal*/

    void recordBaseband_impl::parse_header_data_msg(pmt::pmt_t header_data)
{
    d_payload_tag_keys.clear();
    d_payload_tag_values.clear();
    d_state = STATE_HEADER_RX_FAIL;

    if (pmt::is_integer(header_data)) {
        d_curr_payload_len = pmt::to_long(header_data);
        d_payload_tag_keys.push_back(d_len_tag_key);
        d_payload_tag_values.push_back(header_data);
        d_state = STATE_HEADER_RX_SUCCESS;
    } else if (pmt::is_dict(header_data)) {
        pmt::pmt_t dict_items(pmt::dict_items(header_data));
        while (!pmt::is_null(dict_items)) {
            pmt::pmt_t this_item(pmt::car(dict_items));
            d_payload_tag_keys.push_back(pmt::car(this_item));
            d_payload_tag_values.push_back(pmt::cdr(this_item));
            if (pmt::equal(pmt::car(this_item), d_len_tag_key)) {
                d_curr_payload_len = pmt::to_long(pmt::cdr(this_item));
                d_state = STATE_HEADER_RX_SUCCESS;
            }
            if (pmt::equal(pmt::car(this_item), d_payload_offset_key)) {
                d_curr_payload_offset = pmt::to_long(pmt::cdr(this_item));
                if (std::abs(d_curr_payload_offset) > d_header_padding_total_items) {
                    d_logger->crit("Payload offset exceeds padding");
                    d_state = STATE_HEADER_RX_FAIL;
                    return;
                }
            }
            dict_items = pmt::cdr(dict_items);
        }
        if (d_state == STATE_HEADER_RX_FAIL) {
            d_logger->crit("no payload length passed from header data");
        }
    } else if (header_data == pmt::PMT_F || pmt::is_null(header_data)) {
        d_logger->info("Parser returned {:s}", pmt::write_string(header_data));
    } else {
        d_logger->alert("Received illegal header data ({:s})",
                        pmt::write_string(header_data));
    }
    if (d_state == STATE_HEADER_RX_SUCCESS) {
        if (d_curr_payload_len < 0) {
            d_logger->warn("Received negative payload length: ({:d} symbols)",
                           d_curr_payload_len);
            d_curr_payload_len = 0;
            d_state = STATE_HEADER_RX_FAIL;
        }
        // Note: check for d_curr_payload_len too large requires a max len to
        // be set in the block, and for the block to set its min output buffer
        // size accordingly. There is currently no "max payload len" param.
        set_min_noutput_items(d_curr_payload_len *
                              (d_output_symbols ? 1 : d_items_per_symbol));
    }
} /* parse_header_data_msg() */
  void recordBaseband_impl::copy_n_symbols(const unsigned char* in,
                                               unsigned char* out,
                                               int port,
                                               const uint64_t n_items_read_base,
                                               int n_symbols,
                                               int n_padding_items)
{
    // Copy samples
    if (d_gi) {
        // Here we know n_padding_items must be 0 (see contract),
        // because all padding items will be part of n_symbols
        for (int i = 0; i < n_symbols; i++) {
            memcpy((void*)out,
                   (const void*)(in + d_gi * d_itemsize), // cp is skipped here
                   d_items_per_symbol * d_itemsize);
            in += d_itemsize * (d_items_per_symbol + d_gi);
            out += d_itemsize * d_items_per_symbol;
        }
    } else {
        memcpy((void*)out,
               (const void*)in,
               (n_symbols * d_items_per_symbol + n_padding_items) * d_itemsize);
    }
    // Copy tags
    std::vector<tag_t> tags;
    get_tags_in_range(tags,
                      PORT_INPUTDATA,
                      n_items_read_base,
                      n_items_read_base + n_symbols * (d_items_per_symbol + d_gi) +
                          n_padding_items);
    for (size_t t = 0; t < tags.size(); t++) {
        // The trigger tag is *not* propagated
        if (tags[t].key == d_trigger_tag_key) {
            continue;
        }
        int new_offset = tags[t].offset - n_items_read_base;
        if (d_output_symbols) {
            new_offset /= (d_items_per_symbol + d_gi);
        } else if (d_gi) {
            int pos_on_symbol = (new_offset % (d_items_per_symbol + d_gi)) - d_gi;
            if (pos_on_symbol < 0) {
                pos_on_symbol = 0;
            }
            new_offset = (new_offset / (d_items_per_symbol + d_gi)) + pos_on_symbol;
        }
        add_item_tag(port, nitems_written(port) + new_offset, tags[t].key, tags[t].value);
    }
    // Advance write pointers
    // Items to produce might actually be symbols
    const int items_to_produce =
        d_output_symbols ? n_symbols : (n_symbols * d_items_per_symbol + n_padding_items);
    produce(port, items_to_produce);
} /* copy_n_symbols() */



void recordBaseband_impl::update_special_tags(uint64_t range_start,
                                                    uint64_t range_end)
{
    if (d_track_time) {
        std::vector<tag_t> tags;
        get_tags_in_range(tags, PORT_INPUTDATA, range_start, range_end, d_timing_key);
        if (!tags.empty()) {
            d_last_time = tags.back().value;
            d_last_time_offset = tags.back().offset;
        }
    }

    std::vector<tag_t> tags;
    for (size_t i = 0; i < d_special_tags.size(); i++) {
        // Get tags individually for each special tag.
        get_tags_in_range(tags,
                          PORT_INPUTDATA, // Read from port 0
                          range_start,
                          range_end,
                          d_special_tags[i]);
        for (size_t t = 0; t < tags.size(); t++) {
            d_special_tags_last_value[i] = tags[t].value;
        }
    }
} /* update_special_tags() */

void recordBaseband_impl::add_special_tags()
{
    if (d_track_time) {
        add_item_tag(PORT_HEADER,
                     nitems_written(PORT_HEADER),
                     d_timing_key,
                     _update_pmt_time(d_last_time,
                                      d_sampling_time * (nitems_read(PORT_INPUTDATA) -
                                                         d_last_time_offset)));
    }

    for (unsigned i = 0; i < d_special_tags.size(); i++) {
        add_item_tag(PORT_HEADER,
                     nitems_written(PORT_HEADER),
                     d_special_tags[i],
                     d_special_tags_last_value[i]);
    }
} /* add_special_tags() */



  } /* namespace customModule */
} /* namespace gr */
