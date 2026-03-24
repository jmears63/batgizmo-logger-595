#!/usr/bin/env python3
"""
Emulate the trigger path in Core/Src/trigger.c for a WAV file.

This script processes mono 16-bit PCM WAV sample data and outputs a
window-by-window trigger profile as CSV.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np


FFT_WINDOW_SIZE = 32
FFT_BUCKET_COUNT = FFT_WINDOW_SIZE // 2
WINDOWS_TO_CHECK_LOG2 = 1
WINDOWS_TO_CHECK = 1 << WINDOWS_TO_CHECK_LOG2
SETTINGS_IGNORE_TRIGGER_VALUE = -1
GAIN_MAX_RANGE_INDEX = 4


DEFAULT_SETTINGS = {
    "max_sampling_time_s": 5.0,
    "min_sampling_time_s": 2.0,
    "pretrigger_time_s": 0.5,
    "sensitivity_range": 3,
    "sensitivity_disable": False,
    "write_settings_to_sd": True,
    "trigger_max_count": 16,
    "trigger_headroom": 12,
    "trigger": "*  x  x  x  x  x  x  x  x  x  *  *  *  *  *  *",
    "trigger_profile": "67 67 51 51 47 47 45 43 42 42 42 36 36 36 36 36",
    "disable_usb_msc": False,
    "location": None,
    "logger_sampling_rate_index": 8,
    "gated_recording": False,
    "rtc_mute": False,
}


GAIN_SHIFTS = [0, 1, 2, 3, 4]


FFT_WINDOW_FLOAT = np.array(
    [
        0.00000000,
        0.01023503,
        0.04052109,
        0.08961828,
        0.15551654,
        0.23551799,
        0.32634737,
        0.42428611,
        0.52532458,
        0.62532627,
        0.72019708,
        0.80605299,
        0.87937906,
        0.93717331,
        0.97706963,
        0.99743466,
        0.99743466,
        0.97706963,
        0.93717331,
        0.87937906,
        0.80605299,
        0.72019708,
        0.62532627,
        0.52532458,
        0.42428611,
        0.32634737,
        0.23551799,
        0.15551654,
        0.08961828,
        0.04052109,
        0.01023503,
        0.00000000,
    ],
    dtype=np.float64,
)


def arm_float_to_q15(x: np.ndarray) -> np.ndarray:
    y = np.rint(x * 32768.0)
    y = np.clip(y, -32768, 32767)
    return y.astype(np.int16)


FFT_WINDOW_Q15 = arm_float_to_q15(FFT_WINDOW_FLOAT)


def arm_mult_q15(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    prod = (a.astype(np.int32) * b.astype(np.int32)) >> 15
    prod = np.clip(prod, -32768, 32767)
    return prod.astype(np.int16)


@dataclass
class TriggerSettings:
    trigger_max_count: int
    trigger_headroom: int
    sensitivity_range: int
    logger_sampling_rate_index: int
    trigger_flags: list[bool]
    trigger_profile_q31: list[int]


def clip_int(value: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, value))


def clip_float(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def parse_trigger_flags(trigger_str: str) -> list[bool]:
    tokens = trigger_str.split()
    flags = []
    for i in range(FFT_BUCKET_COUNT):
        if i < len(tokens):
            flags.append(tokens[i].lower() == "x")
        else:
            flags.append(False)
    return flags


def parse_trigger_profile(threshold_str: str, trigger_headroom: int) -> list[int]:
    tokens = threshold_str.split()
    out: list[int] = []
    for i in range(FFT_BUCKET_COUNT):
        if i >= len(tokens):
            out.append(SETTINGS_IGNORE_TRIGGER_VALUE)
            continue

        t = tokens[i].strip()
        if t == "*":
            out.append(SETTINGS_IGNORE_TRIGGER_VALUE)
            continue

        db = float(t) + trigger_headroom
        factor = 10.0 ** (db / 20.0)
        reference = 0x0004
        result = int(factor * reference + 0.5)
        out.append(result * result)
    return out


def bucket_power_to_threshold_db(power: float) -> int:
    """
    Approximate inverse of settings.c threshold conversion.

    settings.c does:
        factor = 10^(db/20)
        reference = 4
        threshold_q31 = round(factor * reference)^2

    So approximately:
        db ~= 10 * log10(threshold_q31 / 16)
    """
    safe_power = max(float(power), 1.0)
    db = 10.0 * math.log10(safe_power / 16.0)
    return int(round(db))


def build_settings(settings_path: Path | None) -> TriggerSettings:
    raw = dict(DEFAULT_SETTINGS)

    if settings_path is not None:
        with settings_path.open("r", encoding="utf-8") as f:
            loaded = json.load(f)
        if not isinstance(loaded, dict):
            raise ValueError("settings file must contain a top-level JSON object")
        raw.update(loaded)

    # Mirror clip behaviour in settings.c where relevant.
    raw["trigger_max_count"] = clip_int(int(raw["trigger_max_count"]), 1, FFT_BUCKET_COUNT)
    raw["trigger_headroom"] = clip_int(int(raw["trigger_headroom"]), -48, 48)
    raw["sensitivity_range"] = clip_int(int(raw["sensitivity_range"]), 0, GAIN_MAX_RANGE_INDEX)
    raw["logger_sampling_rate_index"] = clip_int(int(raw["logger_sampling_rate_index"]), 5, 11)
    raw["max_sampling_time_s"] = clip_float(float(raw["max_sampling_time_s"]), 0.5, 120.0)
    raw["min_sampling_time_s"] = clip_float(float(raw["min_sampling_time_s"]), 0.5, 120.0)
    raw["pretrigger_time_s"] = clip_float(float(raw["pretrigger_time_s"]), 0.0, 2.0)

    flags = parse_trigger_flags(str(raw["trigger"]))
    thresholds = parse_trigger_profile(str(raw["trigger_profile"]), int(raw["trigger_headroom"]))
    return TriggerSettings(
        trigger_max_count=int(raw["trigger_max_count"]),
        trigger_headroom=int(raw["trigger_headroom"]),
        sensitivity_range=int(raw["sensitivity_range"]),
        logger_sampling_rate_index=int(raw["logger_sampling_rate_index"]),
        trigger_flags=flags,
        trigger_profile_q31=thresholds,
    )


def read_wav_mono_i16(path: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(path), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        frame_count = wf.getnframes()
        raw = wf.readframes(frame_count)

    if sample_width != 2:
        raise ValueError("only 16-bit PCM WAV files are supported")

    samples = np.frombuffer(raw, dtype="<i2")
    if channels > 1:
        samples = samples.reshape((-1, channels))[:, 0]
    return sample_rate, samples.astype(np.int16, copy=False)


def compute_freq_buckets_q31(window_q15: np.ndarray) -> np.ndarray:
    """
    Approximate the trigger.c FFT path and return 16 bucket powers.

    Note:
    - trigger.c uses CMSIS fixed-point RFFT (`arm_rfft_q15`) with internal scaling.
    - This Python version uses numpy FFT and keeps bucket ordering/size/check logic
      consistent with trigger.c.
    """
    if window_q15.shape[0] != FFT_WINDOW_SIZE:
        raise ValueError("window size mismatch")

    windowed_q15 = arm_mult_q15(FFT_WINDOW_Q15, window_q15)

    # Keep first 16 bins to match trigger.c bucket count.
    fft_complex = np.fft.fft(windowed_q15.astype(np.float64), n=FFT_WINDOW_SIZE)[:FFT_BUCKET_COUNT]

    power = np.rint((fft_complex.real ** 2) + (fft_complex.imag ** 2))
    return power.astype(np.int64)


def check_for_trigger(
    freq_buckets_q31: np.ndarray,
    settings: TriggerSettings,
    gain_range: int,
) -> tuple[bool, int, list[int]]:
    gain_range = clip_int(gain_range, 0, GAIN_MAX_RANGE_INDEX)
    shift_for_gain = GAIN_SHIFTS[GAIN_MAX_RANGE_INDEX] - GAIN_SHIFTS[gain_range]

    match_count = 0
    matched_indices: list[int] = []

    for i in range(FFT_BUCKET_COUNT):
        threshold = settings.trigger_profile_q31[i]
        if (not settings.trigger_flags[i]) or (threshold == SETTINGS_IGNORE_TRIGGER_VALUE):
            continue

        adjusted_threshold = threshold >> shift_for_gain
        adjusted_threshold = adjusted_threshold >> shift_for_gain

        if int(freq_buckets_q31[i]) >= int(adjusted_threshold):
            match_count += 1
            matched_indices.append(i)

    triggered = (match_count > 0) and (match_count <= settings.trigger_max_count)
    return triggered, match_count, matched_indices


def iter_half_frames(samples: np.ndarray, half_frame_samples: int) -> Iterable[tuple[int, np.ndarray]]:
    frame_idx = 0
    for start in range(0, len(samples) - half_frame_samples + 1, half_frame_samples):
        yield frame_idx, samples[start : start + half_frame_samples]
        frame_idx += 1


def main() -> int:
    parser = argparse.ArgumentParser(
        add_help=False,
        description=(
            "Read a 16-bit PCM WAV file and emulate trigger.c window checks. "
            "Outputs a 99th-percentile aggregate profile to stdout."
        )
    )
    parser.add_argument("--help", action="help", help="Show this help message and exit.")
    parser.add_argument("wav", type=Path, help="Input WAV file")
    parser.add_argument(
        "--settings",
        type=Path,
        default=None,
        help="Optional settings.json; defaults match firmware settings.c",
    )
    parser.add_argument(
        "-p",
        "--percentile",
        nargs="?",
        const=99.0,
        type=float,
        default=None,
        help=(
            "Print aggregate bucket values at this percentile (0..100). "
            "If -p is supplied without a value, 99 is used."
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output CSV path. If omitted, no CSV is written.",
    )
    args = parser.parse_args()

    settings = build_settings(args.settings)
    sample_rate, samples = read_wav_mono_i16(args.wav)

    # Firmware path: samples_per_frame comes from logger_sampling_rate_index * 48kHz,
    # and trigger sees half-frames.
    half_frame_samples = (settings.logger_sampling_rate_index * 48000) // 2000
    if half_frame_samples <= 0:
        raise ValueError("half-frame sample count must be positive")
    if half_frame_samples < FFT_WINDOW_SIZE:
        raise ValueError("half-frame sample count must be at least 32")

    increment = half_frame_samples >> WINDOWS_TO_CHECK_LOG2
    if increment <= 0:
        raise ValueError("invalid half-frame sample count")
    if args.percentile is not None and (args.percentile < 0.0 or args.percentile > 100.0):
        raise ValueError("percentile must be in range 0..100")

    gain_range = clip_int(int(settings.sensitivity_range), 0, GAIN_MAX_RANGE_INDEX)

    out_f = args.output.open("w", newline="", encoding="utf-8") if args.output is not None else None
    try:
        writer = csv.writer(out_f) if out_f is not None else None
        if writer is not None:
            header = [
                "half_frame_index",
                "window_index_in_half_frame",
                "window_start_sample",
                "window_start_time_s",
                "gain_range",
                "match_count",
                "matched_bucket_indices",
                "window_triggered",
                "half_frame_triggered",
            ]
            header.extend([f"bucket_{i}" for i in range(FFT_BUCKET_COUNT)])
            writer.writerow(header)

        windows_checked = 0
        rows_written = 0
        half_frames_with_trigger = 0
        all_buckets: list[np.ndarray] = []

        for half_frame_idx, half_frame in iter_half_frames(samples, half_frame_samples):
            half_triggered = False
            pending_rows = []

            for window_idx in range(WINDOWS_TO_CHECK):
                offset = window_idx * increment
                if offset + FFT_WINDOW_SIZE > half_frame.shape[0]:
                    # In firmware this should not happen for the configured frame sizes.
                    continue

                window = half_frame[offset : offset + FFT_WINDOW_SIZE]
                buckets = compute_freq_buckets_q31(window)
                window_triggered, match_count, matched = check_for_trigger(
                    buckets, settings, gain_range
                )
                half_triggered = half_triggered or window_triggered

                start_sample = (half_frame_idx * half_frame_samples) + offset
                pending_rows.append(
                    [
                        half_frame_idx,
                        window_idx,
                        start_sample,
                        f"{start_sample / sample_rate:.9f}",
                        gain_range,
                        match_count,
                        " ".join(str(i) for i in matched),
                        int(window_triggered),
                        0,  # placeholder, filled below
                        *[int(v) for v in buckets.tolist()],
                    ]
                )
                windows_checked += 1
                all_buckets.append(buckets.copy())

            if half_triggered:
                half_frames_with_trigger += 1
            if writer is not None:
                for row in pending_rows:
                    row[8] = int(half_triggered)
                    writer.writerow(row)
                    rows_written += 1

    finally:
        if out_f is not None:
            out_f.close()

    if args.percentile is not None:
        if all_buckets:
            bucket_matrix = np.vstack(all_buckets)
            aggregate = np.percentile(bucket_matrix, args.percentile, axis=0)
        else:
            aggregate = np.zeros((FFT_BUCKET_COUNT,), dtype=np.float64)

        print(f"Percentile: {args.percentile}")
        print(",".join([str(int(round(v))) for v in aggregate.tolist()]))
        trigger_profile_values = " ".join(
            [str(bucket_power_to_threshold_db(v)) for v in aggregate.tolist()]
        )
        print(f'"trigger_profile": "{trigger_profile_values}"')

    print(
        (
            f"Processed {len(samples)} samples at {sample_rate} Hz, "
            f"{windows_checked} checked windows, "
            f"{half_frames_with_trigger} triggered half-frames."
        ),
        file=sys.stderr,
    )
    if args.output is not None:
        print(f"Wrote CSV profile rows: {rows_written} -> {args.output}", file=sys.stderr)
    else:
        print("No CSV written (use -o/--output to write profile data).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

