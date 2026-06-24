/*
 * Copyright (C) 2013, 2016 Bastian Bloessl <bloessl@ccs-labs.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "utils.h"
#include <gnuradio/io_signature.h>
#include <ieee802_11/sync_short.h>

#include <complex>
#include <deque>
#include <iostream>

using namespace gr::ieee802_11;

static const int MIN_GAP = 480;
static const int MAX_SAMPLES = 540 * 80;

class sync_short_impl : public sync_short
{

public:
    sync_short_impl(double threshold, unsigned int min_plateau, bool log, bool debug)
        : block("sync_short",
                gr::io_signature::make3(
                    3, 3, sizeof(gr_complex), sizeof(gr_complex), sizeof(float)),
                gr::io_signature::make(1, 1, sizeof(gr_complex))),
          d_log(log),
          d_debug(debug),
          d_state(SEARCH),
          d_plateau(0),
          d_freq_offset(0),
          d_copied(0),
          MIN_PLATEAU(min_plateau),
          d_threshold(threshold)
    {

        set_tag_propagation_policy(block::TPP_DONT);
    }

    int general_work(int noutput_items,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items)
    {

        const gr_complex* in = (const gr_complex*)input_items[0];
        const gr_complex* in_abs = (const gr_complex*)input_items[1];
        const float* in_cor = (const float*)input_items[2];
        gr_complex* out = (gr_complex*)output_items[0];

        int noutput = noutput_items;
        int ninput =
            std::min(std::min(ninput_items[0], ninput_items[1]), ninput_items[2]);

        // dout << "SHORT noutput : " << noutput << " ninput: " << ninput_items[0] <<
        // std::endl;

        switch (d_state) {

        case SEARCH: {
            int i;

            for (i = 0; i < ninput; i++) {
                if (in_cor[i] > d_threshold) {
                    if (d_plateau < MIN_PLATEAU) {
                        d_plateau++;

                    } else {
                        d_state = COPY;
                        d_copied = 0;
                        // P0: coarse CFO from the band-stopped signal (ZigBee
                        // removed) instead of the full-band arg(in_abs[i]) / 16.
                        d_freq_offset = cfo_estimate(in_abs[i]);
                        d_plateau = 0;
                        // nitems_written(0) how many items produced so far by this block
                        // nitems_read(0) + i, the absolute index on the input port
                        insert_tag(nitems_written(0), d_freq_offset, nitems_read(0) + i);
                        dout << "SHORT Frame!" << std::endl;
                        break;
                    }
                } else {
                    d_plateau = 0;
                }
                // feed the consumed sample into the band-stop CFO estimator
                cfo_feed(in[i]);
            }

            consume_each(i);
            return 0;
        }

        case COPY: {

            int o = 0;
            while (o < ninput && o < noutput && d_copied < MAX_SAMPLES) {
                if (in_cor[o] > d_threshold) {
                    if (d_plateau < MIN_PLATEAU) {
                        d_plateau++;

                        // there's another frame
                    } else if (d_copied > MIN_GAP) {
                        d_copied = 0;
                        d_plateau = 0;
                        // P0: band-stopped coarse CFO (see SEARCH state).
                        d_freq_offset = cfo_estimate(in_abs[o]);
                        insert_tag(
                            nitems_written(0) + o, d_freq_offset, nitems_read(0) + o);
                        dout << "SHORT Frame!" << std::endl;
                        break;
                    }

                } else {
                    d_plateau = 0;
                }

                out[o] = in[o] * exp(gr_complex(0, -d_freq_offset * d_copied));
                // feed the consumed sample into the band-stop CFO estimator
                cfo_feed(in[o]);
                o++;
                d_copied++;
            }

            if (d_copied == MAX_SAMPLES) {
                d_state = SEARCH;
            }

            dout << "SHORT copied " << o << std::endl;

            consume_each(o);
            return o;
        }
        }

        throw std::runtime_error("sync short: unknown state");
        return 0;
    }

    void insert_tag(uint64_t item, double freq_offset, uint64_t input_item)
    {
        mylog("frame start at in: {} out: {}", input_item, item);

        const pmt::pmt_t key = pmt::string_to_symbol("wifi_start");
        const pmt::pmt_t wifi_start_key = pmt::string_to_symbol("wifi_start_raw");
        const uint64_t raw_item = (input_item >= 16) ? (input_item - 16) : 0;
        const pmt::pmt_t value = pmt::from_double(freq_offset);
        const pmt::pmt_t srcid = pmt::string_to_symbol(name());
        add_item_tag(0, item, key, value, srcid);
        // add new tag wifi start raw
        add_item_tag(0, item, wifi_start_key, pmt::from_uint64(raw_item), srcid); 
    }

private:
    // -------- P0: band-stopped coarse-CFO estimator -------------------------
    // ZigBee is narrowband around DC (the central ~7-8 of the 64 subcarriers).
    // The upstream lag-16 autocorrelation (in_abs) is full-band, so a strong
    // narrowband ZigBee biases arg(in_abs)/16; the resulting coarse CFO can land
    // outside sync_long's fine-correction range (+/-pi/64), leaving an
    // uncorrectable frame-wide offset that destroys OFDM decoding.
    //
    // We re-estimate the coarse CFO from a HIGH-PASSED copy of the signal: a
    // short moving-average (length CFO_HP_MA) is a low-pass whose -3 dB point is
    // ~+/-4 subcarriers, so y = x - movavg(x) removes the central ZigBee bins.
    // The WiFi STF tones sit on every 4th subcarrier, so all of them share the
    // 16-sample period; removing the central +/-4 bins discards ZigBee while
    // keeping the lag-16 periodicity the autocorrelation relies on. We then form
    // the lag-16 product and average it over CFO_WIN samples, exactly mirroring
    // the upstream metric but on the band-stopped signal.
    static constexpr int CFO_LAG = 16;    // STF repetition period (autocorr lag)
    static constexpr int CFO_HP_MA = 8;   // moving-average length -> notch ~ +/-4 bins
    static constexpr int CFO_WIN = 48;    // autocorrelation averaging window

    // Push one (consumed) input sample into the streaming band-stop estimator.
    void cfo_feed(gr_complex x)
    {
        // 1) high-pass: subtract a short moving average (removes DC + ZigBee core)
        d_cfo_raw.push_back(x);
        d_cfo_raw_sum += x;
        if ((int)d_cfo_raw.size() > CFO_HP_MA) {
            d_cfo_raw_sum -= d_cfo_raw.front();
            d_cfo_raw.pop_front();
        }
        const gr_complex hp =
            x - d_cfo_raw_sum / gr_complex((float)d_cfo_raw.size(), 0.0f);

        // 2) lag-16 product hp[n] * conj(hp[n-16])
        d_cfo_hp.push_back(hp);
        gr_complex prod(0.0f, 0.0f);
        if ((int)d_cfo_hp.size() > CFO_LAG) {
            prod = d_cfo_hp.back() * std::conj(d_cfo_hp.front());
            d_cfo_hp.pop_front();
        }

        // 3) moving-average the product over CFO_WIN samples
        d_cfo_prod.push_back(prod);
        d_cfo_auto += prod;
        if ((int)d_cfo_prod.size() > CFO_WIN) {
            d_cfo_auto -= d_cfo_prod.front();
            d_cfo_prod.pop_front();
        }
    }

    // Band-stopped coarse CFO (rad/sample). Falls back to the full-band estimate
    // until the estimator window has warmed up.
    float cfo_estimate(gr_complex fullband_in_abs) const
    {
        if (std::abs(d_cfo_auto) < 1e-6f) {
            return arg(fullband_in_abs) / CFO_LAG;
        }
        return arg(d_cfo_auto) / CFO_LAG;
    }

    std::deque<gr_complex> d_cfo_raw;
    std::deque<gr_complex> d_cfo_hp;
    std::deque<gr_complex> d_cfo_prod;
    gr_complex d_cfo_raw_sum{ 0.0f, 0.0f };
    gr_complex d_cfo_auto{ 0.0f, 0.0f };

    enum { SEARCH, COPY } d_state;
    int d_copied;
    int d_plateau;
    float d_freq_offset;
    const double d_threshold;
    const bool d_log;
    const bool d_debug;
    const unsigned int MIN_PLATEAU;
};

sync_short::sptr
sync_short::make(double threshold, unsigned int min_plateau, bool log, bool debug)
{
    return gnuradio::get_initial_sptr(
        new sync_short_impl(threshold, min_plateau, log, debug));
}
