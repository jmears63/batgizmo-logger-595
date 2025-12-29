/**
 * Copyright (c) 2022-2026 John Mears
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>

#include "mode_usb.h"
#include "fx_api.h"
#include "usb_otg.h"
#include "tusb.h"
#include "leds.h"
#include "init.h"
#include "data_processor_uac.h"
#include "data_acquisition.h"
#include "streaming.h"
#include "autophasecontrol.h"
#include "device/dcd.h"
#include "usb_handlers.h"
#include "trigger.h"
#include "sd_lowlevel.h"
#include "storage.h"
#include "init.h"

#define BLINK_LEDS 1

// In USB mode, we access storage in "normal" mode, ie 4 bits, as speed is more
// important than low noise:
#define STORAGE_MODE STORAGE_FAST

static void init_usb_mode(void);
static void open_usb_mode(void);
static void close_usb_mode(void);

const mode_driver_t usb_mode_driver = {
	init_usb_mode,
	open_usb_mode,
	close_usb_mode
};

static bool s_usb_running = false;
static bool s_mode_opened = false;
static bool s_just_opened = false;
static bool s_sd_mounted = false;


static void init_usb_mode(void)
{
	//s_pMedium = NULL;
	s_usb_running = false;
	s_mode_opened = false;
	s_just_opened = false;
	s_sd_mounted = false;
}

static void start_usb(void)
{
	// Enable power to the USB PHY:
	HAL_PWREx_EnableVddUSB();

	// Initialise the USB peripheral:
	MX_USB_OTG_HS_PCD_Init();

	// Initialise tinyusbL
	tud_init(TUD_OPT_RHPORT);

	// We need the SoF interrupt enabling for auto phase control:
	dcd_sof_enable(TUD_OPT_RHPORT, true);

	// Use LDO mode power supply. This draws a little more current but possibly results in less
	// analogue noise. Though in practice, I see no difference.
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);			// PWR_SMPS_SUPPLY or PWR_LDO_SUPPLY.

	s_usb_running = true;
}

static void stop_usb(void)
{
	if (s_usb_running) {
		// Close down tinyusb:
		tud_deinit(TUD_OPT_RHPORT);

		// Finish with the USB peripheral:
		USB_StopDevice(USB_OTG_HS);
		// HAL_PCD_DeInit();	// JM TODO: should we also call HAL_PCD_MspDeInit? Or does that happen implicitly?

		// Remove power from the USB PHY.
		HAL_PWREx_DisableVddUSB();

		s_usb_running = false;
	}
}

static void open_usb_mode(void)
{
	// Acquired data will be processed for UAC:
	data_processor_uac_reset();
	data_acquisition_set_processor(data_processor_uac);

	// Starting acquiring data:
	streaming_start();
	data_acquisition_enable_capture(true);
	// Enable auto phase control to keep the sampling rate in sync with the USB SoF:
	apc_start();

	// This may not succeed, for example, if there is no SD card. That's OK.
	s_sd_mounted = sd_lowlevel_open(STORAGE_MODE);

	// Keep running USB the whole time as it is needed for both MSC and UAC:
	start_usb();

	s_mode_opened = true;
	s_just_opened = true;
}

static void close_usb_mode(void)
{
	// Re-read settings in case they have changed during USB mode.
	// No need for low noise mode here.

	s_mode_opened = false;
	stop_usb();
	sd_lowlevel_close();		// It's OK to call this even if open failed.

	apc_stop();
	streaming_stop();
	data_acquisition_set_processor(NULL);
}

void usb_mode_main_processing(int main_tick_count)
{
	if (s_mode_opened) {

#if 0	// JM TODO
		// Let USB know if the SD is present and mounted:
		msc_disk_sdmmc_set_present(s_sd_mounted);
#endif

		// Check if the SD card is inserted:
		bool sd_present = sd_lowlevel_get_debounced_sd_present();

		// Warn the user if there is no SD card:
		static bool was_present = false;
		(void) was_present;
#if BLINK_LEDS
		if (s_just_opened) {
			if (!sd_present)
				leds_start_flash();
		}
		else {
			if (!sd_present && was_present)
				leds_start_flash();
			else if (sd_present && !was_present)
				leds_reset();
		}
#endif
		was_present = sd_present;
		s_just_opened = false;

		bool status_good = s_usb_running && usb_handlers_ismounted() && apc_locked_on();
		(void) status_good;
#if BLINK_LEDS
		leds_set(LEDS_GREEN, status_good);
#endif

		if (s_sd_mounted && !sd_present) {
			// The card was present but seems to been removed:
			sd_lowlevel_close();
			s_sd_mounted = false;
		}
		else if (!s_sd_mounted && sd_present) {
			init_read_all_settings();
			s_sd_mounted = sd_lowlevel_open(STORAGE_MODE);
		}
	}
}

void usb_mode_main_fast_processing(int main_tick_count)
{
	if (s_usb_running) {
		tud_task();
	}
}
