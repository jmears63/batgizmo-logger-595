# BatGizmo Firmware

This repo contains the firmware source for the **BatGizmo** bat detector device. The firmware targets STM32U595RIT6Q MCUs. It is fully compatible with the free and open source [BatGizmo Android app](https://play.google.com/store/apps/details?id=uk.org.gimell.batgizmoapp).

The BatGizmo device is a dual purpose bat detector, functioning as both a USB microphone for real time bat discovery (transects) and as an unattended trigger logging to SD card (passive detection). Both modes were designed as first class features from the ground up - neither is an after thought.

Main features:

- **Ultrasonic Audio Capture**
  - High-speed ADC sampling from an ultrasonic microphone.
  - DMA-driven acquisition pipeline.
  - Precise timing and clock control.
  
- **USB mode: it functions as a high quality USB microphone
  - Fully compatible with the free BatGizmo Android App.
  - USB AUC1 compliant.
  - Sampling at 384 kHz with automatic phase locking to the USB host to avoid glitches.
  - Analogue gain can be set via USB, for example, from the BatGizmo App, with a choice of five levels.

- **Automatic logger mode: it functions as a passive logger:
  - Recording to .wav files on SD card, with a configurable upper file size.
  - Sampling rates in the range 288 to 528 kHz (48 kHz steps) can be configured.
  - Flexible triggering of recording based on a set of thresholds in frequency bands.
  - Several seconds of recorded data is buffered in SRAM so that nothing is missed:
  	- Pretriggering allows recordings to include the lead up to a trigger.
  	- Recording need not be interrupted by potentially lengthy SD card operations.
  - Very efficient power management based on IoT technology includes an extremely low power standby mode, allowing the device to be left in place for passive logging for long intervals.

- **Fully configurable via JSON files on the SD card:
  - See the samples directory.

## `settings.json` reference

The BatGizmo firmware reads **`settings.json`** from the root of the SD card (same folder as other config files). It is plain JSON: a single object whose keys are listed below.

### When settings are loaded

- Settings are applied when the firmware **parses** `settings.json` successfully (see [Parsing](#parsing)).
- The file is read when the device **mounts the SD card** in contexts such as **mode changes** (e.g. switching between Auto / USB / Manual) and when the card **becomes available** again in USB mode.
- If there is **no** valid `settings.json`, built-in **defaults** are used (see each field).

### Parsing

- The parser expects **valid JSON**. If parsing fails, the load is rejected and defaults remain (or the previous in-memory settings, depending on call site).
- **Unknown keys** are ignored so newer firmware can accept older files and older firmware can ignore new keys.
- Numeric values are **clipped** into allowed ranges when they are out of range (see each field).
- Boolean values must be JSON primitives **`true`** or **`false`**.

### Fields

| Key | Type | Default | Valid range / notes |
|-----|------|---------|---------------------|
| `max_sampling_time_s` | number | `5` | Clipped to **0.5–120** seconds. Upper bound on length of a single recording segment (non–gated recording). |
| `min_sampling_time_s` | number | `2` | Clipped to **0.5–120** seconds. Minimum duration of a triggered recording. |
| `pretrigger_time_s` | number | `0.5` | Clipped to **0.0–2.0** seconds. How much audio *before* a trigger is kept (buffer permitting). |
| `sensitivity_range` | integer | `3` | Clipped to **0–4** (`GAIN_MAX_RANGE_INDEX`). Selects analogue gain range / sensitivity step. |
| `write_settings_to_sd` | boolean | `true` | If `true`, the device may write a copy of settings (or related data) to the card when recording opens, etc. |
| `trigger_max_count` | integer | `16` | Clipped to **1–16** (`MAX_TRIGGER_MATCH_CLAUSES`). How many trigger clauses / buckets are considered. |
| `trigger` | string | (see sample) | Up to **128** characters. Whitespace-separated tokens, one per frequency bucket: **`x`** = triggering enabled for that bucket, **`*`** = disabled. Extra buckets default to `*`. |
| `trigger_thresholds` | string | (see sample) | Up to **128** characters. Whitespace-separated values in **dB** per bucket, or **`*`** to ignore a bucket. Values are converted internally for FFT comparison. |
| `location` | string | *(absent)* | Optional. Two numbers: **latitude** and **longitude**, separated by whitespace (e.g. `"51.5 -0.12"`). Embedded in metadata (e.g. GUANO) when valid. |
| `logger_sampling_rate_index` | integer | `8` | Clipped to **5–11**. Logger sample rate is **`index × 48 kHz`** (e.g. `8` → 384 kHz). |
| `gated_recording` | boolean | `false` | If `true`, acquisition is **gated**: while data is being written to SD, new samples are not buffered (reduces concurrent load; different buffering path). |
| `rtc_mute` | boolean | `false` | If `true`, in **Auto** mode during an **active** scheduled interval the firmware will **stop the 32.768 kHz LSE / RTC** to avoid the appearance of a line at 32.678 kHz in recordings; wall time is continued using **CPU tick** (`HAL_GetTick`) until the interval ends. **Beware**: If you remove power during auto active mode, the RTC will not be restarted. To avoid this, manually move the switch to USB mode before removing power. |

### Example

See [`samples/settings.json`](samples/settings.json) for a minimal working example.

## `schedule.json` reference

**Automatic logger (Auto) mode** uses **`schedule.json`** on the SD card root to decide **when** the device is allowed to be active for passive logging (time-of-day windows). It is separate from **`settings.json`** (triggering, sample rate, etc.).

### Format

- The file must be **valid JSON** with a **top-level object** containing exactly one key: **`"schedule"`**, whose value is an **array** of interval objects.
- Each interval object has two string fields, in this order:
  - **`"from"`** — start time  
  - **`"to"`** — end time  
- Times are **`HH:MM`** in **24-hour** form (`hours` 0–23, `minutes` 0–59), e.g. `"23:00"`, `"03:30"`.

### Midnight crossing

If **`to`** is **earlier** in the day than **`from`** (e.g. `"from":"23:00"`, `"to":"00:00"`), the firmware treats the interval as **spanning midnight**: the duration is extended by one day in minutes so the window wraps correctly. Daylight saving time is **not** handled in this logic.

### Normalization

- Intervals are **sorted by start time** and **overlapping** intervals are **merged** before use.
- There is a hard limit of **`MAX_SCHEDULE_INTERVALS` (20)** raw entries; parsing fails if exceeded.

### Example

See [`samples/schedule.json`](samples/schedule.json).
