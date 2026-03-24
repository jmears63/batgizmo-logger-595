# Trigger Profile Utility

`trigger_profile.py` is a command-line tool that emulates the firmware trigger path in `Core/Src/trigger.c` for a WAV file.

It can:

- compute trigger checks window-by-window using the same 32-sample FFT bucket logic,
- optionally write the per-window profile to CSV (`-o`),
- print percentile bucket values (`-p`),
- print derived `trigger_profile` values.

## Requirements

- Python 3.9+ (or equivalent modern Python 3)
- `numpy`

Install dependency:

```bash
python3 -m pip install numpy
```

## Input assumptions

- WAV must be 16-bit PCM.
- If WAV has multiple channels, channel 0 is used.
- Trigger settings are read from `--settings` JSON if provided; otherwise firmware defaults from `settings.c` are used.

## Usage

```bash
python3 utils/trigger_profile.py [options] <input.wav>
```

## Examples

Using the settings file provided, caculated how many times the audio data provided would have triggered,
and write the details to the output CSV file specified:

```bash
python utils/trigger_profile.py --settings samples/settings.json -o analysis.csv audiodata.wav 

Processed 2408470 samples at 480000 Hz, 20070 checked windows, 8 triggered half-frames.
```

Using the settings file provided, caculate what trigger thresholds would trigger at the 99th percentile of the data:

```bash
python3 utils/trigger_profile.py input.wav -p 99 --settings samples/settings.json -p

Percentile: 99.0
4713241,1601074,105333,78849,156432,155607,73478,23371,13033,9501,7229,6780,5158,4060,5097,6565
"trigger_profile": "55 50 38 37 40 40 37 32 29 28 27 26 25 24 25 26"
Processed 2408470 samples at 480000 Hz, 20070 checked windows, 8 triggered half-frames.
```

## Output

### Per-window CSV (`-o`)

Columns:

- `half_frame_index`
- `window_index_in_half_frame`
- `window_start_sample`
- `window_start_time_s`
- `gain_range`
- `match_count`
- `matched_bucket_indices`
- `window_triggered`
- `half_frame_triggered`
- `bucket_0` ... `bucket_15`

### Stdout (`-p`)

When `-p` is supplied, stdout prints:

1. `Percentile: <value>`
2. Comma-separated aggregate bucket values (`bucket_0..bucket_15`)
3. A JSON-style `trigger_profile` string derived from those aggregate values
   (firmware adds `trigger_headroom`, default `12` dB, when converting this to thresholds).

### Stderr

Always prints a processing summary.  
If `-o` was provided, stderr also shows where the per-window CSV was written.
