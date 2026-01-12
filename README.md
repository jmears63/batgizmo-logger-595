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