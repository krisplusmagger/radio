#!/usr/bin/env python3
"""Summarize archived WiFi/ZigBee experiment stats.

The auto-run script archives files as:
  seq_pdu_stats_YYYYMMDD_HHMMSS_runNNNN.txt
  zigbee_correction_stats_YYYYMMDD_HHMMSS_runNNNN.txt

This script pairs those files, ranks runs by ZigBee correction recovery rate,
prints the top runs, and computes the average recovery rate over the top N.
It also writes a text summary with trimmed overall averages.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Optional


ARCHIVE_RE = re.compile(
    r"^(?P<prefix>seq_pdu_stats|zigbee_correction_stats)_"
    r"(?P<stamp>\d{8}_\d{6})_run(?P<run>\d+)\.txt$"
)


@dataclass
class RunStats:
    stamp: str
    run: int
    seq_path: Optional[Path]
    zigbee_path: Path
    seq: Dict[str, str]
    zigbee: Dict[str, str]
    recovery_rate: float


def parse_key_value_file(path: Path) -> Dict[str, str]:
    values: Dict[str, str] = {}
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def to_float(values: Dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        return float(values.get(key, default))
    except (TypeError, ValueError):
        return default


def to_int(values: Dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(float(values.get(key, default)))
    except (TypeError, ValueError):
        return default


def optional_float(values: Dict[str, str], key: str) -> Optional[float]:
    if key not in values:
        return None
    try:
        return float(values[key])
    except ValueError:
        return None


def archived_stats_files(directory: Path, prefix: str) -> Iterable[Path]:
    return sorted(directory.glob(f"{prefix}_*_run*.txt"))


def recovery_rate_from_stats(values: Dict[str, str]) -> float:
    if "recovery_rate" in values:
        return to_float(values, "recovery_rate")

    attempts = to_float(values, "correction_attempt_count")
    successes = to_float(values, "correction_crc_success_count")
    if attempts <= 0.0:
        return 0.0
    return successes / attempts


def collect_runs(directory: Path) -> list[RunStats]:
    seq_files: Dict[tuple[str, int], Path] = {}
    zigbee_files: Dict[tuple[str, int], Path] = {}

    for path in archived_stats_files(directory, "seq_pdu_stats"):
        match = ARCHIVE_RE.match(path.name)
        if match:
            seq_files[(match.group("stamp"), int(match.group("run")))] = path

    for path in archived_stats_files(directory, "zigbee_correction_stats"):
        match = ARCHIVE_RE.match(path.name)
        if match:
            zigbee_files[(match.group("stamp"), int(match.group("run")))] = path

    runs: list[RunStats] = []
    for key, zigbee_path in sorted(zigbee_files.items()):
        stamp, run = key
        zigbee = parse_key_value_file(zigbee_path)
        seq_path = seq_files.get(key)
        seq = parse_key_value_file(seq_path) if seq_path else {}
        runs.append(
            RunStats(
                stamp=stamp,
                run=run,
                seq_path=seq_path,
                zigbee_path=zigbee_path,
                seq=seq,
                zigbee=zigbee,
                recovery_rate=recovery_rate_from_stats(zigbee),
            )
        )
    return runs


def average(values: list[float]) -> float:
    if not values:
        return 0.0
    return sum(values) / len(values)


def trimmed_average(values: list[float]) -> tuple[float, int, Optional[float], Optional[float]]:
    """Average after dropping one lowest and one highest value when possible."""
    if not values:
        return 0.0, 0, None, None

    sorted_values = sorted(values)
    if len(sorted_values) < 3:
        return average(sorted_values), len(sorted_values), None, None

    dropped_low = sorted_values[0]
    dropped_high = sorted_values[-1]
    trimmed = sorted_values[1:-1]
    return average(trimmed), len(trimmed), dropped_low, dropped_high


def build_report(runs: list[RunStats], top: int, average_top: int) -> list[str]:
    ranked = sorted(runs, key=lambda item: item.recovery_rate, reverse=True)
    if not ranked:
        return ["No archived zigbee_correction_stats_*_run*.txt files found."]

    average_group = ranked[:average_top]
    top_recovery_average = average([item.recovery_rate for item in average_group])
    all_recovery_rates = [item.recovery_rate for item in ranked]
    throughput_values = [
        value
        for item in ranked
        for value in [optional_float(item.seq, "throughput_bps")]
        if value is not None
    ]
    trimmed_recovery, recovery_count, dropped_recovery_low, dropped_recovery_high = (
        trimmed_average(all_recovery_rates)
    )
    trimmed_throughput, throughput_count, dropped_throughput_low, dropped_throughput_high = (
        trimmed_average(throughput_values)
    )

    lines = [
        f"Found {len(ranked)} archived runs with ZigBee correction stats.",
        f"Average recovery_rate over top {len(average_group)} runs: "
        f"{top_recovery_average:.6f} ({top_recovery_average * 100:.2f}%)",
        (
            f"Overall trimmed recovery_rate average: {trimmed_recovery:.6f} "
            f"({trimmed_recovery * 100:.2f}%) from {recovery_count} runs"
        ),
        (
            f"Overall trimmed throughput_bps average: {trimmed_throughput:.3f} "
            f"from {throughput_count} runs"
        ),
    ]
    if dropped_recovery_low is not None and dropped_recovery_high is not None:
        lines.append(
            "Dropped recovery_rate outliers: "
            f"lowest={dropped_recovery_low:.6f}, highest={dropped_recovery_high:.6f}"
        )
    if dropped_throughput_low is not None and dropped_throughput_high is not None:
        lines.append(
            "Dropped throughput_bps outliers: "
            f"lowest={dropped_throughput_low:.3f}, highest={dropped_throughput_high:.3f}"
        )
    lines.extend(["", f"Top {min(top, len(ranked))} runs by recovery_rate:"])

    header = (
        "rank run stamp recovery_rate attempts successes throughput_bps "
        "ack_count nack_count total_tx elapsed_s corr ltf_start"
    )
    lines.append(header)
    lines.append("-" * len(header))

    for rank, item in enumerate(ranked[:top], start=1):
        lines.append(
            f"{rank} "
            f"{item.run:04d} "
            f"{item.stamp} "
            f"{item.recovery_rate:.6f} "
            f"{to_int(item.zigbee, 'correction_attempt_count')} "
            f"{to_int(item.zigbee, 'correction_crc_success_count')} "
            f"{to_float(item.seq, 'throughput_bps'):.3f} "
            f"{to_int(item.seq, 'ack_count')} "
            f"{to_int(item.seq, 'nack_count')} "
            f"{to_int(item.seq, 'total_tx_count')} "
            f"{to_float(item.seq, 'elapsed_seconds'):.3f} "
            f"{to_float(item.zigbee, 'last_correlation_score'):.6f} "
            f"{to_int(item.zigbee, 'last_ltf_start_raw')}"
        )

    lines.extend(["", "File pairs:"])
    for item in ranked[:top]:
        seq_name = item.seq_path.name if item.seq_path else "missing"
        lines.append(f"run{item.run:04d}: {seq_name} + {item.zigbee_path.name}")

    return lines


def print_table(runs: list[RunStats], top: int, average_top: int) -> None:
    print("\n".join(build_report(runs, top, average_top)))


def write_csv(path: Path, runs: list[RunStats]) -> None:
    ranked = sorted(runs, key=lambda item: item.recovery_rate, reverse=True)
    fields = [
        "rank",
        "run",
        "stamp",
        "recovery_rate",
        "correction_attempt_count",
        "correction_crc_success_count",
        "throughput_bps",
        "ack_count",
        "nack_count",
        "total_tx_count",
        "elapsed_seconds",
        "last_correlation_score",
        "last_ltf_start_raw",
        "seq_file",
        "zigbee_file",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for rank, item in enumerate(ranked, start=1):
            writer.writerow(
                {
                    "rank": rank,
                    "run": item.run,
                    "stamp": item.stamp,
                    "recovery_rate": item.recovery_rate,
                    "correction_attempt_count": to_int(
                        item.zigbee, "correction_attempt_count"
                    ),
                    "correction_crc_success_count": to_int(
                        item.zigbee, "correction_crc_success_count"
                    ),
                    "throughput_bps": to_float(item.seq, "throughput_bps"),
                    "ack_count": to_int(item.seq, "ack_count"),
                    "nack_count": to_int(item.seq, "nack_count"),
                    "total_tx_count": to_int(item.seq, "total_tx_count"),
                    "elapsed_seconds": to_float(item.seq, "elapsed_seconds"),
                    "last_correlation_score": to_float(
                        item.zigbee, "last_correlation_score"
                    ),
                    "last_ltf_start_raw": to_int(item.zigbee, "last_ltf_start_raw"),
                    "seq_file": item.seq_path.name if item.seq_path else "",
                    "zigbee_file": item.zigbee_path.name,
                }
            )


def write_summary_txt(path: Path, runs: list[RunStats], top: int, average_top: int) -> None:
    path.write_text("\n".join(build_report(runs, top, average_top)) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rank archived WiFi/ZigBee experiment runs by recovery rate."
    )
    parser.add_argument(
        "--directory",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Directory containing archived stats files.",
    )
    parser.add_argument("--top", type=int, default=5, help="Number of top runs to print.")
    parser.add_argument(
        "--average-top",
        type=int,
        default=50,
        help="Number of top recovery rates to average.",
    )
    parser.add_argument("--csv", type=Path, help="Optional CSV output path.")
    parser.add_argument(
        "--summary-txt",
        type=Path,
        help="Text report output path. Defaults to recovery_stats_summary.txt.",
    )
    args = parser.parse_args()

    runs = collect_runs(args.directory)
    print_table(runs, max(args.top, 0), max(args.average_top, 1))

    summary_path = args.summary_txt or args.directory / "recovery_stats_summary.txt"
    write_summary_txt(summary_path, runs, max(args.top, 0), max(args.average_top, 1))
    print(f"Wrote text summary: {summary_path}")

    if args.csv:
        write_csv(args.csv, runs)
        print(f"Wrote CSV summary: {args.csv}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
