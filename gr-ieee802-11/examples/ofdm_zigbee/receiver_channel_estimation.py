#!/usr/bin/env python3
"""
Simple receiver-side channel estimation using two LTF symbols and fixed known x.

Model on selected bins k:
    Y(k) = H(k) * X(k) + W(k)

Estimate (LS over two LTF symbols):
    H_hat(k) = sum_i Y_i(k) * conj(X(k)) / sum_i |X(k)|^2
"""

import argparse
import numpy as np
from fractions import Fraction


def shifted_bins_to_fftshift_indices(shifted_bins, nfft=64):
    """
    Convert shifted OFDM bins (e.g., -32..31) to fftshifted array indices [0..nfft-1].
    """
    idx = np.asarray(shifted_bins, dtype=int) + nfft // 2
    if np.any(idx < 0) or np.any(idx >= nfft):
        raise ValueError(f"Bin out of range for nfft={nfft}: {shifted_bins}")
    return idx


def estimate_h_from_two_ltf(rx, ltf_start, x_ref_bins, empty_bins_shifted, nfft=64, cp=16, eps=1e-12):
    """
    rx: complex stream containing at least two LTF symbols (with CP).
    ltf_start: sample index where first LTF CP starts.
    x_ref_bins: known fixed X(k) on selected bins (length K).
    empty_bins_shifted: selected bins in shifted indexing (-32..31 for nfft=64).
    """
    if len(x_ref_bins) != len(empty_bins_shifted):
        raise ValueError("x_ref_bins and empty_bins_shifted must have the same length.")

    needed = ltf_start + (cp + nfft) + (cp + nfft)
    if len(rx) < needed:
        raise ValueError(f"rx too short. Need at least {needed} samples, got {len(rx)}.")

    # Two consecutive LTF symbols: [CP(16) + 64] + [CP(16) + 64]
    s1 = rx[ltf_start + cp: ltf_start + cp + nfft]
    s2 = rx[ltf_start + cp + nfft + cp: ltf_start + cp + nfft + cp + nfft]

    y1 = np.fft.fftshift(np.fft.fft(s1, nfft))
    y2 = np.fft.fftshift(np.fft.fft(s2, nfft))

    idx = shifted_bins_to_fftshift_indices(empty_bins_shifted, nfft=nfft)
    Y = np.vstack([y1[idx], y2[idx]])  # shape: (2, K)
    X = np.asarray(x_ref_bins, dtype=complex)[None, :]  # shape: (1, K)

    num = np.sum(Y * np.conj(X), axis=0)
    den = Y.shape[0] * (np.abs(X[0]) ** 2) + eps
    h_hat = num / den
    h_scalar = np.mean(h_hat)

    return h_hat, h_scalar


def _resample_complex_to_rate(sig, fs_in, fs_out):
    """
    Resample complex baseband to target rate.
    Tries scipy polyphase resampling first, then falls back to linear interpolation.
    """
    if fs_in <= 0 or fs_out <= 0:
        raise ValueError("fs_in and fs_out must be positive.")

    if abs(fs_in - fs_out) < 1e-9:
        return sig

    frac = Fraction(fs_out / fs_in).limit_denominator(2000)
    up, down = frac.numerator, frac.denominator

    try:
        from scipy.signal import resample_poly  # type: ignore
        i = resample_poly(np.real(sig), up, down)
        q = resample_poly(np.imag(sig), up, down)
        return i + 1j * q
    except Exception:
        t_in = np.arange(len(sig), dtype=float) / fs_in
        n_out = int(np.floor(len(sig) * fs_out / fs_in))
        t_out = np.arange(n_out, dtype=float) / fs_out
        i = np.interp(t_out, t_in, np.real(sig))
        q = np.interp(t_out, t_in, np.imag(sig))
        return i + 1j * q


def extract_x_from_zigbee(
    zigbee_tx,
    ltf_start_wifi,
    empty_bins_shifted,
    fs_zigbee,
    fs_wifi,
    nfft=64,
    cp=16,
):
    """
    Build the small x(k) from ZigBee-only waveform:
    1) resample ZigBee waveform to WiFi receiver sample rate
    2) slice two LTF windows (same indices used for WiFi receiver)
    3) FFT and keep selected bins
    4) average two LTF observations
    """
    zigbee_wifi_rate = _resample_complex_to_rate(zigbee_tx, fs_zigbee, fs_wifi)

    needed = ltf_start_wifi + (cp + nfft) + (cp + nfft)
    if len(zigbee_wifi_rate) < needed:
        raise ValueError(
            f"Resampled ZigBee too short. Need {needed} samples, got {len(zigbee_wifi_rate)}."
        )

    s1 = zigbee_wifi_rate[ltf_start_wifi + cp: ltf_start_wifi + cp + nfft]
    s2 = zigbee_wifi_rate[
        ltf_start_wifi + cp + nfft + cp: ltf_start_wifi + cp + nfft + cp + nfft
    ]

    X1 = np.fft.fftshift(np.fft.fft(s1, nfft))
    X2 = np.fft.fftshift(np.fft.fft(s2, nfft))
    idx = shifted_bins_to_fftshift_indices(empty_bins_shifted, nfft=nfft)

    x_small = 0.5 * (X1[idx] + X2[idx])
    return x_small, zigbee_wifi_rate


def _build_demo_rx(ltf_start, empty_bins_shifted, x_ref_bins, nfft=64, cp=16, noise_std=0.01):
    """
    Build a small synthetic rx for quick sanity check.
    """
    idx = shifted_bins_to_fftshift_indices(empty_bins_shifted, nfft=nfft)
    K = len(idx)

    # True channel on selected bins
    h_true = (0.8 + 0.2j) + 0.05 * (np.random.randn(K) + 1j * np.random.randn(K))

    # Frequency-domain LTF observation contains only the selected bins
    Yf = np.zeros(nfft, dtype=complex)
    Yf[idx] = h_true * x_ref_bins

    s = np.fft.ifft(np.fft.ifftshift(Yf), nfft)
    s_cp = np.concatenate([s[-cp:], s])

    # Two LTFs + optional prefix zeros
    prefix = np.zeros(ltf_start, dtype=complex)
    rx = np.concatenate([prefix, s_cp, s_cp])

    # Add noise
    noise = noise_std * (np.random.randn(len(rx)) + 1j * np.random.randn(len(rx)))
    rx = rx + noise
    return rx, h_true


def _parse_bins(s):
    return [int(v.strip()) for v in s.split(",") if v.strip()]


def main():
    parser = argparse.ArgumentParser(
        description="Simple receiver-side channel estimation from two LTF symbols."
    )
    parser.add_argument("--rx-npy", type=str, default=None, help="Path to complex rx .npy file")
    parser.add_argument("--x-npy", type=str, default=None, help="Path to complex x_ref_bins .npy file")
    parser.add_argument("--ltf-start", type=int, default=0, help="Start index of first LTF CP")
    parser.add_argument("--nfft", type=int, default=64)
    parser.add_argument("--cp", type=int, default=16)
    parser.add_argument("--zigbee-npy", type=str, default=None, help="Path to ZigBee-only TX complex .npy")
    parser.add_argument("--zigbee-fs", type=float, default=5e6, help="ZigBee waveform sample rate")
    parser.add_argument("--wifi-fs", type=float, default=5e6, help="WiFi receiver sample rate")
    parser.add_argument("--save-x-npy", type=str, default=None, help="Save extracted x_small to this .npy path")
    parser.add_argument(
        "--empty-bins",
        type=str,
        default="0,1,2,3,4,5",
        help="Comma-separated shifted bins (e.g., '0,1,2,3,4,5' or '-6,-5,-4,-3,-2,-1')",
    )
    args = parser.parse_args()

    empty_bins = _parse_bins(args.empty_bins)
    K = len(empty_bins)

    if args.x_npy is not None:
        x_ref_bins = np.load(args.x_npy).astype(complex)
    elif args.zigbee_npy is not None:
        zigbee_tx = np.load(args.zigbee_npy).astype(complex)
        x_ref_bins, _ = extract_x_from_zigbee(
            zigbee_tx=zigbee_tx,
            ltf_start_wifi=args.ltf_start,
            empty_bins_shifted=empty_bins,
            fs_zigbee=args.zigbee_fs,
            fs_wifi=args.wifi_fs,
            nfft=args.nfft,
            cp=args.cp,
        )
        if args.save_x_npy is not None:
            np.save(args.save_x_npy, x_ref_bins)
            print(f"Saved x_small to: {args.save_x_npy}")
    else:
        # Simple fixed reference; replace with your calibrated known x on those bins.
        x_ref_bins = np.ones(K, dtype=complex)

    if len(x_ref_bins) != K:
        raise ValueError(f"x_ref_bins length {len(x_ref_bins)} != number of bins {K}")

    if args.rx_npy is not None:
        rx = np.load(args.rx_npy).astype(complex)
        h_hat, h_scalar = estimate_h_from_two_ltf(
            rx=rx,
            ltf_start=args.ltf_start,
            x_ref_bins=x_ref_bins,
            empty_bins_shifted=empty_bins,
            nfft=args.nfft,
            cp=args.cp,
        )
        print("Estimated H(k):", h_hat)
        print("Scalar h (mean over bins):", h_scalar)
    else:
        # Demo mode if rx not provided.
        rx, h_true = _build_demo_rx(
            ltf_start=args.ltf_start,
            empty_bins_shifted=empty_bins,
            x_ref_bins=x_ref_bins,
            nfft=args.nfft,
            cp=args.cp,
        )
        h_hat, h_scalar = estimate_h_from_two_ltf(
            rx=rx,
            ltf_start=args.ltf_start,
            x_ref_bins=x_ref_bins,
            empty_bins_shifted=empty_bins,
            nfft=args.nfft,
            cp=args.cp,
        )
        print("Demo mode (synthetic rx)")
        print("True H(k):     ", h_true)
        print("Estimated H(k):", h_hat)
        print("Scalar h (mean over bins):", h_scalar)


if __name__ == "__main__":
    main()



# python3 receiver_channel_estimation.py \
#   --zigbee-npy zigbee_tx_only.npy \
#   --zigbee-fs 500e3 \
#   --wifi-fs 5e6 \
#   --ltf-start <wifi_ltf_start_index> \
#   --empty-bins 0,1,2,3,4,5 \
#   --save-x-npy x_small.npy
