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

#include <data_acquisition.h>
#include <data_processor_buffers.h>
#include <stdbool.h>
#include <memory.h>

#include "mode_manual.h"
#include "storage.h"
#include "settings.h"
#include "tusb_config.h"

#include "gain.h"
#include "init.h"
#include "tim.h"
#include "adc.h"
#include "gpdma.h"
#include "spi.h"
#include "tim.h"
#include "leds.h"
#include "main.h"
#include "streaming.h"
#include "recording.h"
#include "trigger.h"
#include "tusb_config.h"

#define BLINK_LEDS 1

static void init_manual_mode(void);
static void open_manual_mode(void);
static void close_manual_mode(void);

const mode_driver_t manual_mode_driver = {
	init_manual_mode,
	open_manual_mode,
	close_manual_mode
};

static bool s_manual_mode_active = false;

static void init_manual_mode(void)
{
	s_manual_mode_active = false;
}

static void open_manual_mode(void)
{
	// Acquired data will be processed for the SD card:
	const int sampling_rate = settings_get_logger_sampling_rate();
	data_processor_buffers_reset(DATA_PROCESSOR_CONTINUOUS, sampling_rate);
	data_acquisition_set_processor(data_processor_buffers);

	streaming_start(settings_get()->logger_sampling_rate_index);
	recording_open(sampling_rate);
#if 0
	recording_start("manual");
#else
	recording_start();
#endif


	// Tell the data module we are ready for to tell us about ready data buffers:
	data_acquisition_enable_capture(true);

	// Use LDO mode power supply. This draws a little more current but possibly results in less
	// analogue noise. Though in practice, I see no difference.
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);			// PWR_SMPS_SUPPLY or PWR_LDO_SUPPLY.

	// Start processing main loop callback code:
	s_manual_mode_active = true;
}

static void close_manual_mode(void)
{
	s_manual_mode_active = false;

	recording_stop(false);
	recording_close();
	streaming_stop();
	data_acquisition_set_processor(NULL);
}

void manual_mode_main_processing(int main_tick_count)
{
	if (s_manual_mode_active) {

	}
}
