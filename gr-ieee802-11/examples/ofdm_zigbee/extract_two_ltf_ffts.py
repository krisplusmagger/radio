#!/usr/bin/env python3
"""
Extract two 64-point FFTs from WiFi-rate samples at LTF useful starts.

Default assumptions (your current setup):
- first FFT window starts at index 176 (0-based)
- FFT length = 64
- second FFT is the next consecutive 64 samples
  so start2 = start1 + 64
"""

import argparse
import os
import numpy as np


def load_complex_signal(path: str) -> np.ndarray:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".npy":
        x = np.load(path)
        return x.astype(complex)
    if ext == ".csv":
        d = np.loadtxt(path, delimiter=",", skiprows=1)
        if d.ndim != 2 or d.shape[1] < 2:
            raise ValueError("CSV must have columns: real,imag")
        return d[:, 0] + 1j * d[:, 1]
    raise ValueError("Unsupported input format. Use .npy or .csv")


def save_fft_csv(path: str, X: np.ndarray):
    bins = np.arange(len(X), dtype=int)
    out = np.column_stack((bins, np.real(X), np.imag(X), np.abs(X)))
    np.savetxt(path, out, delimiter=",", header="bin,real,imag,mag", comments="")


def main():
    p = argparse.ArgumentParser(description="Compute two 64-point FFTs for LTF windows.")
    p.add_argument("--in", dest="in_path", default="wifi_rx_from_zigbee.npy", help="Input .npy or .csv")
    p.add_argument("--start1", type=int, default=176, help="First LTF useful FFT start index (0-based)")
    p.add_argument("--nfft", type=int, default=64, help="FFT size")
    p.add_argument("--cp", type=int, default=0, help="Optional extra gap before second FFT (default 0)")
    p.add_argument("--start2", type=int, default=None, help="Optional manual second LTF useful start")
    p.add_argument("--shift", action="store_true", help="Apply fftshift to outputs")
    p.add_argument("--out1-npy", default="ltf_fft1.npy")
    p.add_argument("--out2-npy", default="ltf_fft2.npy")
    p.add_argument("--out1-csv", default="ltf_fft1.csv")
    p.add_argument("--out2-csv", default="ltf_fft2.csv")
    args = p.parse_args()

    x = load_complex_signal(args.in_path)
    start1 = args.start1
    start2 = args.start2 if args.start2 is not None else (start1 + args.nfft + args.cp)

    end2 = start2 + args.nfft
    if start1 < 0 or start2 < 0 or end2 > len(x):
        raise ValueError(
            f"Not enough samples. Need up to index {end2 - 1}, but input length is {len(x)}."
        )

    s1 = x[start1:start1 + args.nfft]
    s2 = x[start2:start2 + args.nfft]

    X1 = np.fft.fft(s1, args.nfft)
    X2 = np.fft.fft(s2, args.nfft)
    if args.shift:
        X1 = np.fft.fftshift(X1)
        X2 = np.fft.fftshift(X2)

    np.save(args.out1_npy, X1)
    np.save(args.out2_npy, X2)
    save_fft_csv(args.out1_csv, X1)
    save_fft_csv(args.out2_csv, X2)

    print(f"Input: {args.in_path} (len={len(x)})")
    print(f"LTF1 useful window: [{start1}, {start1 + args.nfft - 1}]")
    print(f"LTF2 useful window: [{start2}, {start2 + args.nfft - 1}]")
    print(f"Saved: {args.out1_npy}, {args.out2_npy}")
    print(f"Saved: {args.out1_csv}, {args.out2_csv}")
    print("Index note: if useful start is 176 (0-based), index 175 is last CP sample.")


if __name__ == "__main__":
    main()
