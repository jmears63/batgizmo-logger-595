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

#include <data_processor_buffers.h>
#include <memory.h>
#include <stdbool.h>

#include "recording.h"
#include "storage.h"
#include "settings.h"
#include "leds.h"
#include "sd_lowlevel.h"

#define BLINK_LEDS 1

// When recording data we access storage in "low noise" mode, ie 1 bits, as quality is more
// important than speed:
#define STORAGE_MODE STORAGE_LOW_NOISE


static FX_FILE s_fx_file;

static FX_MEDIA *s_fx_pMedium = NULL;
static FX_FILE *s_fx_pFile = NULL;

static int s_max_samples_per_file = 0;
static int s_file_samples_written = 0;
static bool s_recording_opened = false;
static bool s_recording_primed = false;		// Has recording_prime been called?
static bool s_recording_started = false;
static bool s_recording_first = false;
static int s_sampling_rate = 0;

/*
 * Here's how to use the functions in this module from another module:
 *
 *  recording_init					<-- Once.
 *  loop1:
 *  	recording_open				<-- Typically as part of client module opening.
 *  	loop2:						<-- Repeated recording by the same module.
 *  		recording_prime			<-- Optional: may be time consuming.
 *  		recording_start			<-- This is will low latency if recording_prime was called.
 *  		recording_stop
 *  	recording_close				<-- Typically as part of client module closing.
 *
 */


void recording_init(void)
{
	s_fx_pMedium = NULL;
	s_fx_pFile = NULL;
	memset(&s_fx_file, 0, sizeof(s_fx_file));
	s_max_samples_per_file = 0;
	s_file_samples_written = 0;
	s_recording_opened = false;
	s_recording_started = false;
	s_recording_first = false;
	s_recording_primed = false;
}

void recording_open(int sampling_rate)
{
	// Write the settings at the start of the session, if required. Do this here rather
	// than when writing the first data file to avoid extra latency at that time.

	if (settings_get()->write_settings_to_sd) {
		// Mount the SD card if it is present, in 1 bit bus mode to minimize noise:
		s_fx_pMedium = storage_mount(STORAGE_MODE);
		if (s_fx_pMedium) {
			storage_write_settings(s_fx_pMedium);
			storage_unmount(true);
			s_fx_pMedium = NULL;
		}
	}

	s_recording_opened = true;
	s_recording_first = true;
	s_recording_primed = false;
	s_recording_started = false;
	s_sampling_rate = sampling_rate;
}

static void close_or_clean_up(FX_MEDIA *pMedium, FX_FILE *pFile) {
	// Avoid leaving files with no data in:
	if (s_file_samples_written > 0)
		storage_close_wav_file(s_fx_pMedium, s_fx_pFile);
	else
		storage_clean_up_wav_file(s_fx_pMedium, s_fx_pFile);
}

void recording_close(void)
{
	if (s_recording_started)
		recording_stop(false);

	// Clean up anything that left over. This can happen if this function is called while
	// recording is primed.

	if (s_fx_pFile)
		close_or_clean_up(s_fx_pMedium, s_fx_pFile);

	// Unmount the SD card if we mounted it successfully:
	if (s_fx_pMedium)
		storage_unmount(true);
	s_fx_pMedium = NULL;

	s_recording_opened = false;
}

void recording_prime(void)
{
	if (s_recording_primed) {
		// It's already been primed. If we get here, it is a bug in the client module; the
		// most sensible thing we can do is nothing:
		return;
	}

	// Mount the SD card if it is present, in 1 bit bus mode to minimize noise:
	s_fx_pMedium = storage_mount(STORAGE_MODE);	// ~ 100+250 ms, or 100+100ms with STORAGE_NORMAL.
	if (s_fx_pMedium) {
		// ~300 ms:
		s_fx_pFile = storage_open_wav_file(s_fx_pMedium, &s_fx_file, s_sampling_rate, "(primed)");
		s_max_samples_per_file = settings_get()->max_sampling_time_s * s_sampling_rate;
		s_file_samples_written = 0;

		if (s_fx_pFile) {
			// Get ahead of the game by flushing FAT updates and the file header to SD:
			storage_flush(s_fx_pMedium);
		}
	}

	s_recording_primed = true;
}

#if 0
void recording_start(const char *trigger)
#else
void recording_start()
#endif
{
	// The client module may or may not have already primed us ready to record:
	if (!s_recording_primed) {
		recording_prime();
	}

#if 0
	// For inclusion in guano data:
	storage_set_trigger_string(trigger);
#endif

	s_recording_started = true;
	s_recording_primed = false;
}

void recording_stop(bool go_to_standby)
{
	if (s_fx_pFile) {
		close_or_clean_up(s_fx_pMedium, s_fx_pFile);
		s_fx_pFile = NULL;
	}

	s_recording_started = false;

	if (go_to_standby) {
		// Prepare for another recording. Leave the SD card mounted, and open a new file ready:
		if (s_fx_pMedium) {
			// ~300 ms:
			s_fx_pFile = storage_open_wav_file(s_fx_pMedium, &s_fx_file, s_sampling_rate, "(preopened)");
			s_max_samples_per_file = settings_get()->max_sampling_time_s * s_sampling_rate;
			s_file_samples_written = 0;

			if (s_fx_pFile) {
				// Get ahead of the game by flushing FAT updates and the file header to SD:
				storage_flush(s_fx_pMedium);
			}
			s_recording_primed = true;

			(void) s_recording_primed;
		}
	}
	else {
		// We're done for now. Unmount the SD card if we mounted it successfully:
		if (s_fx_pMedium)
			storage_unmount(true);
		s_fx_pMedium = NULL;
	}
}

void recording_main_processing(int main_tick_count)
{
	if (s_recording_opened)	{

		// Alert the user if they remove the SD card, even if we are not recording
		// at this moment:

		bool sd_present = sd_lowlevel_get_debounced_sd_present();
		// Warn the user if there is no SD card:
		static bool was_present = false;
		(void) was_present;
#if BLINK_LEDS
		if (s_recording_first) {
			s_recording_first = false;
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

		// Has the SD card been removed or reinserted?
		if (s_fx_pMedium && !sd_present) {
			// The SD card seems to not be there any more. Unmount it with extreme prejudice:
			storage_unmount(false);
			s_fx_pMedium = NULL;
		}
		else if (!s_fx_pMedium && sd_present)
		{
			// The SD card has reappeared, and we should be recording, so mount it and open a new file:
			s_fx_pMedium = storage_mount(STORAGE_MODE);
			if (s_fx_pMedium) {
				s_fx_pFile = storage_open_wav_file(s_fx_pMedium, &s_fx_file, s_sampling_rate, "continued");
				s_file_samples_written = 0;
			}
		}

		if (sd_present) {
			sample_type_t *buffer_to_write = NULL;
			const bool should_close_file = dataprocessor_buffers_get_next(&buffer_to_write);
			if (should_close_file) {
				// Close the file, standing by for the next one.
				recording_stop(true);
			}
			// leds_set(led_red, false);

			else if (buffer_to_write) {
				if (s_fx_pFile == NULL) {
					// We need to open a file:
					recording_start();
				}

				// Do we need to start a new data file?
				if (s_file_samples_written >= s_max_samples_per_file) {
#if BLINK_LEDS
					leds_set(LEDS_GREEN, true);
#endif
					// Close the wav file and open a new one:
					if (s_fx_pFile) {
						storage_close_wav_file(s_fx_pMedium, s_fx_pFile);
						s_fx_pFile = NULL;
					}

					s_fx_pFile = storage_open_wav_file(s_fx_pMedium, &s_fx_file, s_sampling_rate, "continued");

					s_file_samples_written = 0;
#if BLINK_LEDS
					leds_set(LEDS_GREEN, false);
#endif
				}

				if (s_fx_pFile) {
#if BLINK_LEDS
					leds_set(LEDS_GREEN, true);
#endif
					// The following line blocks while it writes. Perhaps it would be smarter to kick off
					// an async write, so as not to block the main thread. One day.
					storage_wav_file_append_data(s_fx_pFile, (sample_type_t *) buffer_to_write, DATA_BUFFER_ENTRIES);
					s_file_samples_written += DATA_BUFFER_ENTRIES;
#if BLINK_LEDS
					leds_set(LEDS_GREEN, false);
#endif
				}
			}
		}
	}
}
