#!/usr/bin/env python3
"""
Automatically run wifi_zigbee_tx_rx.grc experiments in a loop.

Each run:
1. Optionally regenerates wifi_tx_rx.py from wifi_zigbee_tx_rx.grc.
2. Runs wifi_tx_rx.py for a fixed duration.
3. Stops the flowgraph on timeout, or notices an early crash/exit.
4. Archives seq_pdu_stats.txt and zigbee_correction_stats.txt with a timestamp.
5. Starts the next run.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_STATS_FILES = ("seq_pdu_stats.txt", "zigbee_correction_stats.txt")


def timestamp() -> str:
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def archive_file(path: Path, stamp: str, run_index: int) -> Path | None:
    if not path.exists():
        return None

    archived = path.with_name(f"{path.stem}_{stamp}_run{run_index:04d}{path.suffix}")
    counter = 1
    while archived.exists():
        archived = path.with_name(
            f"{path.stem}_{stamp}_run{run_index:04d}_{counter}{path.suffix}"
        )
        counter += 1

    path.rename(archived)
    return archived


def terminate_process(proc: subprocess.Popen, grace_seconds: float) -> None:
    if proc.poll() is not None:
        return

    try:
        os.killpg(proc.pid, signal.SIGINT)
    except ProcessLookupError:
        return

    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return
        time.sleep(0.2)

    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return

    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return
        time.sleep(0.2)

    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        return


def run_checked(cmd: list[str], cwd: Path) -> None:
    print(f"[auto-run] Running: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=str(cwd), check=True)


def run_flowgraph(args: argparse.Namespace, run_index: int) -> tuple[str, int | None, Path]:
    stamp = timestamp()
    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"wifi_tx_rx_{stamp}_run{run_index:04d}.log"

    cmd = [sys.executable, "-u", str(args.python_file)]
    print(f"[auto-run] Starting run {run_index} for {args.duration:.1f}s")
    print(f"[auto-run] Log: {log_path}")

    with log_path.open("w", encoding="utf-8", buffering=1) as log_file:
        log_file.write(f"start_time={stamp}\n")
        log_file.write(f"cmd={' '.join(cmd)}\n")
        log_file.flush()

        proc = subprocess.Popen(
            cmd,
            cwd=str(SCRIPT_DIR),
            stdout=log_file,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        start = time.monotonic()
        status = "timeout"
        return_code: int | None = None
        while True:
            return_code = proc.poll()
            if return_code is not None:
                status = "completed" if return_code == 0 else "crashed"
                break

            if time.monotonic() - start >= args.duration:
                terminate_process(proc, args.stop_grace)
                return_code = proc.poll()
                status = "timeout"
                break

            time.sleep(args.poll_interval)

        end_stamp = timestamp()
        log_file.write(f"\nend_time={end_stamp}\n")
        log_file.write(f"status={status}\n")
        log_file.write(f"return_code={return_code}\n")

    return status, return_code, log_path


def archive_stats(stats_files: list[str], run_index: int) -> None:
    stamp = timestamp()
    for name in stats_files:
        archived = archive_file(SCRIPT_DIR / name, stamp, run_index)
        if archived is None:
            print(f"[auto-run] Stats file not found, skipping: {name}")
        else:
            print(f"[auto-run] Archived {name} -> {archived.name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repeatedly run wifi_zigbee_tx_rx.grc and archive stats files."
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=100.0,
        help="Seconds per run before the flowgraph is stopped and restarted.",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=0,
        help="Number of runs to execute. Use 0 for infinite runs.",
    )
    parser.add_argument(
        "--restart-delay",
        type=float,
        default=2.0,
        help="Seconds to wait between runs.",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=1.0,
        help="Seconds between process health checks.",
    )
    parser.add_argument(
        "--stop-grace",
        type=float,
        default=5.0,
        help="Grace seconds after SIGINT/SIGTERM before stronger termination.",
    )
    parser.add_argument(
        "--no-generate",
        action="store_true",
        help="Do not run grcc before each experiment run.",
    )
    parser.add_argument(
        "--grc-file",
        type=Path,
        default=SCRIPT_DIR / "wifi_zigbee_tx_rx.grc",
        help="Path to the GRC file.",
    )
    parser.add_argument(
        "--python-file",
        type=Path,
        default=SCRIPT_DIR / "wifi_tx_rx.py",
        help="Path to the generated Python flowgraph.",
    )
    parser.add_argument(
        "--log-dir",
        type=Path,
        default=SCRIPT_DIR / "auto_run_logs",
        help="Directory for per-run stdout/stderr logs.",
    )
    parser.add_argument(
        "--stats-file",
        action="append",
        default=list(DEFAULT_STATS_FILES),
        help="Stats file to archive after each run. Can be repeated.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.grc_file = args.grc_file.resolve()
    args.python_file = args.python_file.resolve()
    args.log_dir = args.log_dir.resolve()

    if shutil.which("grcc") is None and not args.no_generate:
        print("[auto-run] ERROR: grcc not found. Use --no-generate or install GNU Radio.")
        return 1

    run_index = 1
    try:
        while args.runs == 0 or run_index <= args.runs:
            if not args.no_generate:
                run_checked(["grcc", "-o", str(SCRIPT_DIR), str(args.grc_file)], SCRIPT_DIR)

            status, return_code, log_path = run_flowgraph(args, run_index)
            print(
                f"[auto-run] Run {run_index} ended: "
                f"status={status}, return_code={return_code}, log={log_path.name}"
            )
            archive_stats(args.stats_file, run_index)

            run_index += 1
            if args.runs != 0 and run_index > args.runs:
                break

            time.sleep(args.restart_delay)
    except KeyboardInterrupt:
        print("\n[auto-run] Stopped by user.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
