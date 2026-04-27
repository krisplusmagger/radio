#!/usr/bin/env python3
"""
Plot two FFT vectors from .npy or .csv files.

CSV format expected from extract_two_ltf_ffts.py:
  bin,real,imag,mag
or simple:
  real,imag
"""

import argparse
import os
import numpy as np
import matplotlib.pyplot as plt


def load_fft(path: str):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".npy":
        x = np.load(path).astype(complex)
        bins = np.arange(len(x), dtype=int)
        return x, bins

    if ext == ".csv":
        data = np.loadtxt(path, delimiter=",", skiprows=1)
        if data.ndim != 2:
            raise ValueError(f"Unexpected CSV shape for {path}: {data.shape}")
        if data.shape[1] >= 3:
            # bin,real,imag,mag
            bins = data[:, 0]
            re = data[:, 1]
            im = data[:, 2]
        elif data.shape[1] >= 2:
            # real,imag
            bins = np.arange(len(data), dtype=int)
            re = data[:, 0]
            im = data[:, 1]
        else:
            raise ValueError(f"CSV must have at least 2 columns for {path}")
        return re + 1j * im, bins

    raise ValueError(f"Unsupported file type: {path}")


def main():
    p = argparse.ArgumentParser(description="Plot two FFT files (.npy or .csv).")
    p.add_argument("--fft1", default="ltf_fft1.npy")
    p.add_argument("--fft2", default="ltf_fft2.npy")
    p.add_argument("--title", default="Two LTF FFTs")
    p.add_argument("--save", default="", help="Optional output image path, e.g. ltf_ffts.png")
    p.add_argument("--db", action="store_true", help="Plot magnitude in dB")
    p.add_argument(
        "--centered-bins",
        action="store_true",
        help="Use centered x-axis bins (-N/2..N/2-1), useful for fftshifted data",
    )
    args = p.parse_args()

    x1, b1 = load_fft(args.fft1)
    x2, b2 = load_fft(args.fft2)

    n = min(len(x1), len(x2))
    x1 = x1[:n]
    x2 = x2[:n]
    b1 = b1[:n]
    b2 = b2[:n]
    bins = b1 if len(b1) == n else np.arange(n)
    if args.centered_bins:
        bins = np.arange(-n // 2, n // 2, dtype=int)

    mag1 = np.abs(x1)
    mag2 = np.abs(x2)
    if args.db:
        eps = 1e-12
        mag1 = 20 * np.log10(mag1 + eps)
        mag2 = 20 * np.log10(mag2 + eps)
        ylab = "Magnitude (dB)"
    else:
        ylab = "Magnitude"

    ph1 = np.unwrap(np.angle(x1))
    ph2 = np.unwrap(np.angle(x2))

    fig, ax = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    fig.suptitle(args.title)

    ax[0].plot(bins, mag1, label="FFT1")
    ax[0].plot(bins, mag2, label="FFT2")
    ax[0].set_ylabel(ylab)
    ax[0].grid(True, alpha=0.3)
    ax[0].legend()

    ax[1].plot(bins, ph1, label="FFT1")
    ax[1].plot(bins, ph2, label="FFT2")
    ax[1].set_xlabel("Bin Index")
    ax[1].set_ylabel("Phase (rad)")
    ax[1].grid(True, alpha=0.3)
    ax[1].legend()

    plt.tight_layout()
    if args.save:
        plt.savefig(args.save, dpi=150)
        print(f"Saved plot: {args.save}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
