#!/usr/bin/env python3
"""
Convert ZigBee TX samples (CSV) to WiFi-receiver-rate samples by interpolation.

Input:
  - zigbee_tx_final.csv (columns: real,imag)
Output:
  - resampled WiFi-rate signal (.npy and/or .csv)
  - printed preamble locations in both ZigBee-rate and WiFi-rate indices
"""

import argparse
from fractions import Fraction

import numpy as np


def load_complex_csv(path: str) -> np.ndarray:
    data = np.loadtxt(path, delimiter=",", skiprows=1)
    if data.ndim != 2 or data.shape[1] < 2:
        raise ValueError("CSV must have at least 2 columns: real,imag")
    return data[:, 0] + 1j * data[:, 1]


def resample_complex_linear(sig: np.ndarray, fs_in: float, fs_out: float) -> np.ndarray:
    if fs_in <= 0 or fs_out <= 0:
        raise ValueError("fs_in and fs_out must be positive.")
    if len(sig) == 0:
        return sig
    if abs(fs_in - fs_out) < 1e-12:
        return sig.copy()

    n_out = int(round(len(sig) * fs_out / fs_in))
    t_in = np.arange(len(sig), dtype=float) / fs_in
    t_out = np.arange(n_out, dtype=float) / fs_out
    i = np.interp(t_out, t_in, np.real(sig))
    q = np.interp(t_out, t_in, np.imag(sig))
    return i + 1j * q


def map_index_range_by_rate(start_incl: int, end_incl: int, fs_in: float, fs_out: float):
    """
    Map an inclusive input index range [a,b] to output index range after resampling grid change.
    Uses time-interval mapping:
      [a/fs_in, (b+1)/fs_in) -> [ceil(a*r), floor((b+1)*r)-1], r=fs_out/fs_in
    """
    r = fs_out / fs_in
    start_out = int(np.ceil(start_incl * r))
    end_out = int(np.floor((end_incl + 1) * r) - 1)
    return start_out, end_out


def main():
    p = argparse.ArgumentParser(description="Interpolate ZigBee TX waveform to WiFi RX sample rate.")
    p.add_argument("--in-csv", type=str, default="zigbee_tx_final.csv")
    p.add_argument("--zigbee-fs", type=float, default=500e3)
    p.add_argument("--wifi-fs", type=float, default=5e6)
    p.add_argument("--out-npy", type=str, default="wifi_rx_from_zigbee.npy")
    p.add_argument("--out-csv", type=str, default="wifi_rx_from_zigbee.csv")
    args = p.parse_args()

    tx = load_complex_csv(args.in_csv)
    rx_wifi = resample_complex_linear(tx, args.zigbee_fs, args.wifi_fs)

    np.save(args.out_npy, rx_wifi)
    rx_ri = np.column_stack((np.real(rx_wifi), np.imag(rx_wifi)))
    np.savetxt(args.out_csv, rx_ri, delimiter=",", header="real,imag", comments="")

    # From your TX chain:
    # samples per framed byte = 2 nibbles * 16 chips * 4 repeat = 128.
    # access_code_prefixer output bytes: [pad][preamble(4 bytes)][len][payload]
    # If using "known sync region" = pad + preamble, range is byte 0..4.
    # Strict preamble field (0x000000A7) is byte 1..4.
    sync_with_pad_z_start, sync_with_pad_z_end = 0, 639
    preamble_z_start, preamble_z_end = 128, 639
    a7_z_start, a7_z_end = 512, 639

    sync_with_pad_w_start, sync_with_pad_w_end = map_index_range_by_rate(
        sync_with_pad_z_start, sync_with_pad_z_end, args.zigbee_fs, args.wifi_fs
    )
    preamble_w_start, preamble_w_end = map_index_range_by_rate(
        preamble_z_start, preamble_z_end, args.zigbee_fs, args.wifi_fs
    )
    a7_w_start, a7_w_end = map_index_range_by_rate(
        a7_z_start, a7_z_end, args.zigbee_fs, args.wifi_fs
    )

    ratio = Fraction(args.wifi_fs / args.zigbee_fs).limit_denominator(1000)
    print(f"Input samples: {len(tx)} at fs={args.zigbee_fs:g} Hz")
    print(f"Output samples: {len(rx_wifi)} at fs={args.wifi_fs:g} Hz")
    print(f"Rate ratio (wifi/zigbee): {ratio.numerator}/{ratio.denominator}")
    print(f"Known sync (pad+preamble, 5 bytes) @ ZigBee-rate indices: [{sync_with_pad_z_start}, {sync_with_pad_z_end}]")
    print(f"Known sync (pad+preamble, 5 bytes) @ WiFi-rate indices:   [{sync_with_pad_w_start}, {sync_with_pad_w_end}]")
    print(f"Strict preamble field (4 bytes) @ ZigBee-rate indices:    [{preamble_z_start}, {preamble_z_end}]")
    print(f"Strict preamble field (4 bytes) @ WiFi-rate indices:      [{preamble_w_start}, {preamble_w_end}]")
    print(f"0xA7 byte @ ZigBee-rate indices:          [{a7_z_start}, {a7_z_end}]")
    print(f"0xA7 byte @ WiFi-rate indices:            [{a7_w_start}, {a7_w_end}]")
    print(f"Saved: {args.out_npy}")
    print(f"Saved: {args.out_csv}")


if __name__ == "__main__":
    main()



# python3 zigbee_tx_to_wifi_rx.py \
#   --in-csv zigbee_tx_final.csv \
#   --zigbee-fs 500e3 \
#   --wifi-fs 5e6 \
#   --out-npy wifi_rx_from_zigbee.npy \
#   --out-csv wifi_rx_from_zigbee.csv
