# BatGizmo Firmware

This repo contains the firmware source for the **BatGizmo** bat detector device. The firmware targets STM32U595RIT6Q MCUs. It is fully compatible with the free and open source [BatGizmo Android app](https://play.google.com/store/apps/details?id=uk.org.gimell.batgizmoapp).

The BatGizmo device is a dual purpose bat detector, functioning as both a USB microphone for real time bat discovery (transects) and as an unattended trigger logging to SD card (passive detection). Both modes were designed as first class features from the ground up - neither is an after thought.

General information can be found in [this blog post](https://twilighttravels.org/2026/04/04/batgizmo-detector-design/).

The [schematic can be found here](https://github.com/jmears63/batgizmo-logger-kicad).


Main features:
 
- **Ultrasonic Audio Capture**
  - High-speed ADC sampling from an ultrasonic microphone.
  - DMA-driven acquisition pipeline.
  - Precise timing and clock control.
  
- **USB mode**: functions as a high quality USB microphone
  - Fully compatible with the free BatGizmo Android App.
  - USB AUC1 compliant.
  - Sampling at 384 kHz with automatic phase locking to the USB host to avoid glitches.
  - Analogue gain can be set via USB, for example, from the BatGizmo App, with a choice of five levels.

- **Passive mode**: automatic logger
  - Recording to .wav files on SD card, with a configurable upper file size.
  - Sampling rates in the range 288 to 528 kHz (48 kHz steps) can be configured.
  - Flexible triggering of recording based on a set of thresholds in frequency bands.
  - Several seconds of recorded data is buffered in SRAM:
  	- Pretriggering allows recordings to include the lead up to a trigger.
  	- Allows for data acquisition and SD writing to be alternated, resulting in very low noise.
  - Very efficient power management based on IoT technology includes an extremely low power standby mode, allowing the device to be left in place for passive logging for long intervals.

- **Fully configurable** via JSON files on the SD card:
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
| `max_sampling_time_s` | number | `5` | Valid range **0.5–120** seconds. Maximum duration of a single recording segment (non–gated recording, see below). |
| `min_sampling_time_s` | number | `2` | Valid range **0.5–120** seconds. Minimum duration of a triggered recording, excluding the pretrigger. |
| `pretrigger_time_s` | number | `0.5` | Valid range **0.0–2.0** seconds. Duration of audio from *before* the trigger that will be included. |
| `sensitivity_range` | integer | `3` | Valid range **0–4**, corresponding to 0-18 dB in steps of 6 dB. Selects the analogue gain. |
| `write_settings_to_sd` | boolean | `true` | If `true`, the device will write a copy of settings to the SD card when when a recording session starts to a file named <date>_<time>_settings.json. |
| `trigger_max_count` | integer | `16` | Valid range **1–16** (`MAX_TRIGGER_MATCH_CLAUSES`). Experimental - do not set.  |
| `trigger_headroom` | integer | `12` | Valid range **0..48** dB. A single setting than can conveniently be used to control the overall trigger sensitivity. It is added to `trigger_profile` bucket values before use as a trigger thresholds. |
| `trigger` | string | (see sample) | Up to **128** characters. Whitespace-separated tokens, one per frequency bucket: **`x`** = triggering enabled for that bucket, **`*`** = disabled. There are 16 frequency buckets spanning the range from 0 kHz to the Nyquist frequency.|
| `trigger_profile` | string | (see sample) | Up to **128** characters. Whitespace-separated values in **dB** per frequency bucket. |
| `location` | string | n/a | Optional. Two numbers: **latitude** and **longitude**, separated by whitespace (e.g. `"51.5 -0.12"`). Included in GUANO metadata if present. |
| `logger_sampling_rate_index` | integer | `8` | Valid range **5–11**. Logger sampling rate is **`index × 48 kHz`**, so allows sampling rates of 240-528 kHz. When the detector is used in active mode as a USB microphone this setting is ignored and the sampling rate is 384 kHz |
| `gated_recording` | boolean | `false` | If `true`, data acquisition is alternated with writing to SD card to achieve lowest noise recordings. In this mode the maximum duration of a recording segment is determined by the MCU cache size. |
| `auto_disable_leds` | boolean | `false` | Optional. If `true`, LEDs are automatically disabled one minute after entering an operating mode or reinserting the SD card. This can be used to save battery power and avoid distracting lights. |
| `rtc_mute` | boolean | `false` | Experimental - do not set. |

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

## Install firmware from a `.dfu` file (Linux)

Prebuilt versions of the BatGizmo firmware are available from [the releases listed in github](https://github.com/jmears63/batgizmo-logger-595/releases).

On Linux, use `dfu-util` to transfer firmware to the BatGizmo using USB DFU mode. Similar utilities are available for other operating systems.

1) Find a suitable USB cable to connect the BatGizmo to the computer. Start with the computer switched on and the USB cable plugged into the computer, but the BatGizmo **disconnected** initially.

2) Hold down the DFU button on the BatGizmo, then plug the USB cable into the BatGizmo, taking care to **continue holding down the DFU button throughout**.

3) Release the DFU button. The BatGizmo should now start up in DFU mode, which you can verify by listing USB devices that support DFU. You should see something similar to this:

```bash
dfu-util --list
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

Found DFU: [0483:df11] ver=0200, devnum=13, cfg=1, intf=0, path="1-6", alt=2, name="@OTP Memory   /0x0BFA0000/01*512 e", serial="205B396A5242"
Found DFU: [0483:df11] ver=0200, devnum=13, cfg=1, intf=0, path="1-6", alt=1, name="@Option Bytes   /0x40022040/01*64 e", serial="205B396A5242"
Found DFU: [0483:df11] ver=0200, devnum=13, cfg=1, intf=0, path="1-6", alt=0, name="@Internal Flash   /0x08000000/512*08Kg", serial="205B396A5242"
```
Note the USB device code, which is **0483:df11** in this example.

4) Download firmware to the device, using the device code you just noted, as below. Substitute the name of your actual DFU file.

```bash
dfu-util -d 0483:df11 -a 0 -D batgizmo-x.y.z.dfu -s 0x8000000
```

5) Restart the BatGizmo into normal operating mode by unplugging, then replugging the USB cable, this time **without** holding down the DFU button.
