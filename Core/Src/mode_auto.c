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
#include <time.h>

#include "mode_auto.h"
#include "storage.h"
#include "buffer.h"
#include "settings.h"
#include "rtc.h"
#include "leds.h"
#include "streaming.h"
#include "recording.h"
#include "trigger.h"
#include "init.h"
#include "adc.h"
#include "tusb_config.h"

#define BLINK_LEDS 1

// When recording data we access storage in "low noise" mode, ie 1 bits, as quality is more
// important than speed:
#define STORAGE_MODE STORAGE_LOW_NOISE

static void init_auto_mode(void);
static void open_auto_mode(void);
static void close_auto_mode(void);

const mode_driver_t auto_mode_driver = {
	init_auto_mode,
	open_auto_mode,
	close_auto_mode
};

typedef enum { STATE_START, STATE_SETTINGS_ERROR,
	STATE_ACTIVE_MODE,
	STATE_SOFT_STANDBY_MODE,
	STATE_HARD_STANDBY_MODE,
} auto_state_t;

static auto_state_t s_state = STATE_START;
static bool s_main_processing_enabled = false;
static bool s_streaming_started = false;

#define SCHEDULE_FILE_NAME "schedule.json"
#define MINUTES_PER_DAY (24 * 60)
#define SECONDS_PER_DAY (24 * 60 * 60)

typedef struct {
	time_t start_epoch;
	time_t duration_epoch;
} date_mapped_interval_t;

static int read_raw_schedule(schedule_interval_t intervals[]);
static time_t get_time_now(struct tm *now);
static void enter_standby(time_t wakeup_epoch);
static void exit_standby(void);
static void enter_active(void);
static void exit_active(void);
static bool is_in_range(int v, int min, int max);
static int realize_intervals(schedule_interval_t raw_intervals[], int raw_interval_count,
		date_mapped_interval_t mapped_intervals[]);

#define DO_HARDWARE_STANDBY 1		// Disable this for easier debugging.

static void reset_vars(void)
{
	s_state = STATE_START;
	s_main_processing_enabled = false;
}

static void init_auto_mode(void)
{
	reset_vars();
}

static void open_auto_mode(void)
{
	// Acquired data will be processed for the SD card:
	data_processor_buffers_reset(DATA_PROCESSOR_TRIGGERED, settings_get_logger_sampling_rate());
	data_acquisition_set_processor(data_processor_buffers);
	reset_vars();
	s_main_processing_enabled = true;

	// Switch to switched mode power supply. This reduces power current draw, at the expense of possibly
	// more electrical noise:
	HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY);			// PWR_SMPS_SUPPLY or PWR_LDO_SUPPLY.
}

static void close_auto_mode(void)
{
	s_main_processing_enabled = false;

	// Switch to LDO. This increases power current draw and allegedly reduces
	// electrical noise, though I don't think any difference is evident.
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);			// PWR_SMPS_SUPPLY or PWR_LDO_SUPPLY.

	// Stop anything that is running at this point:
	if (s_streaming_started) {
		streaming_stop();
		s_streaming_started = false;
	}

	recording_close();
	data_acquisition_set_processor(NULL);
}

void auto_mode_main_processing(int main_tick_count)
{
	if (!s_main_processing_enabled)
		return;

	// IMPORTANT - this static data is not preserved through hard standby. Anything
	// that matters needs to be repopulated during the start state.

	static schedule_interval_t raw_intervals[MAX_SCHEDULE_INTERVALS];
	static date_mapped_interval_t intervals[MAX_SCHEDULE_INTERVALS * 3];		// Allow space x 3: Yesterday, today, tomorrow.
	static int interval_count = 0, raw_interval_count = 0;
	static const time_t s_soft_standby_duration = 30;			// Time taken to fall asleep before entering standby mode.
	static const time_t s_minimum_hard_standby_duration = 15;			// Don't go into into hard standby for less than this duration
	static time_t start_epoch, end_epoch;
	static time_t s_standby_wakeup_epoch, s_pending_standby_started;


	struct tm tm_now;
	time_t now_epoch = get_time_now(&tm_now);

#define DEBUG_USING_LEDS 0
#if DEBUG_USING_LEDS
	switch (s_state) {
		case STATE_START:
			leds_set(LED_RED, true);
			leds_set(LED_GREEN, false);
			break;
		case STATE_ACTIVE_MODE:
			leds_set(LED_RED, true);
			leds_set(LED_GREEN, true);
			break;
		case STATE_SOFT_STANDBY_MODE:
			leds_set(LED_RED, false);
			leds_set(LED_GREEN, true);
			break;
		default:
		case STATE_HARD_STANDBY_MODE:
			leds_set(LED_RED, false);
			leds_set(LED_GREEN, false);
			break;
	}
#endif

	switch (s_state) {
		case STATE_START:
		{
			// Read the schedule here in the main loop as it might be updated at any point.
			HAL_Delay(10);	// Hack: not sure why but we seem to need this delay to be able to read from the SD here.
			raw_interval_count = read_raw_schedule(raw_intervals);
			interval_count = realize_intervals(raw_intervals, raw_interval_count, intervals);

			if (interval_count > 0) {
				// See if there there is a currently active internal, or one due to become active
				// in the next short time. Intervals have already been sorted in ascending order.
				// Choose the latest one by searching in reverse order.

				bool active_interval_found = false;
				for (int i = interval_count - 1; i >= 0; i--) {
					// start and end can be outside the range of today:
					time_t start = intervals[i].start_epoch;
					time_t end = start + intervals[i].duration_epoch;
					if (now_epoch >= start && now_epoch <= end) {
						start_epoch = start;
						end_epoch = end;
						active_interval_found = true;
						break;
					}
				}

				if (active_interval_found) {
					enter_active();
					s_state = STATE_ACTIVE_MODE;
					break;
				}

				// There is no currently active interval nor one starting shortly, so we need to go to standby,
				// having first figured out when we need to wake up.

				// Find the next interval we need to wake up for:
				bool next_wakeup_found = false;
				for (int i = 0; i < interval_count && !next_wakeup_found; i++) {
					time_t start = intervals[i].start_epoch;
					time_t end = start + intervals[i].duration_epoch;
					if (start > now_epoch) {
						start_epoch = start /*- starting_prepone_s*/;
						end_epoch = end;
						next_wakeup_found = true;
					}
				}

				if (next_wakeup_found) {
					// The next wakeup time is now in start_minutes, which may be beyond midnight.
					// Calculate the date and time that we need to wake up:

					// We should never normally get here. For testing purposes, we'll simulate standby
					// using a state:
					s_standby_wakeup_epoch = start_epoch;
					s_pending_standby_started = now_epoch;
					s_state = STATE_SOFT_STANDBY_MODE;
					break;
				}

				// What if we get here? That's impossible.
				s_state = STATE_SETTINGS_ERROR;
			}
			else {
				// Couldn't read the schedule (missing, no intervals, bad data etc).
				s_state = STATE_SETTINGS_ERROR;
			}
		}
		break;

		case STATE_SETTINGS_ERROR:
		{
			/*	recording module will do this for us.
			// Flash all LEDS to attract attention.
			bool on = (main_tick_count & 0x07) == 0;
			// TODO change this:
			int value = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
			HAL_GPIO_WritePin(GPIO_LED_R_GPIO_Port, GPIO_LED_R_Pin, value);
			HAL_GPIO_WritePin(GPIO_LED_Y_GPIO_Port, GPIO_LED_Y_Pin, value);
			HAL_GPIO_WritePin(GPIO_LED_G_GPIO_Port, GPIO_LED_G_Pin, value);
			*/
		}
		break;

		case STATE_ACTIVE_MODE:
		{
			if (!is_in_range(now_epoch, start_epoch, end_epoch)) {
				exit_active();
				s_state = STATE_START;
			}
		}
		break;

		case STATE_SOFT_STANDBY_MODE:
		{
			// Pause here before we enter standby. This is to allow time to attach
			// a debugger in the event of immediate delay. It also avoids going into hard standby
			// for a very short time, avoiding the risk of wake up time in the past.

			if ((now_epoch > s_pending_standby_started + s_soft_standby_duration)
					&&
				(start_epoch > now_epoch + s_minimum_hard_standby_duration))
				{
				// Time to go to standby:
				enter_standby(start_epoch);
				s_state = STATE_HARD_STANDBY_MODE;
			}

			if (now_epoch >= s_standby_wakeup_epoch) {
				// Time for the next active interval.
				s_state = STATE_START;
			}
		}
		break;

		case STATE_HARD_STANDBY_MODE:
		{
			// This state simulates standby mode for testing purposes.
			if (now_epoch >= s_standby_wakeup_epoch) {
				exit_standby();

				// Simulate hardware standby by resetting static data:
				memset(raw_intervals, 0, sizeof(raw_intervals));
				memset(intervals, 0, sizeof(intervals));
				interval_count = 0; raw_interval_count = 0;
				start_epoch = end_epoch = 0;
				s_standby_wakeup_epoch = s_pending_standby_started = 0;

				s_state = STATE_START;
			}
		}
		break;
	}
}

void auto_mode_main_fast_processing(int main_tick_count)
{
}

/**
 * Try to mount the SD card and read any schedule json file there.
 */
static int read_raw_schedule(schedule_interval_t intervals[])
{
	int count = 0;

	// Low speed mode, but past enough for this purpose:
	FX_MEDIA *pMedium = storage_mount(STORAGE_MODE);
	if (pMedium) {
		// We've mounted the SD card. Let's see if the schedule JSON file is there.
		FX_FILE file;
		memset(&file, 0, sizeof(file));
		if (fx_file_open(pMedium, &file, SCHEDULE_FILE_NAME, FX_OPEN_FOR_READ) == FX_SUCCESS) {
			ULONG actual_len = 0;
			// Allow buffer space for the final \0:
			/*UINT status =*/ fx_file_read(&file, (void *) g_2k_char_buffer, LEN_2K_BUFFER - 1, &actual_len);
			g_2k_char_buffer[actual_len] = '\0';
			fx_file_close(&file);

			count = settings_parse_and_normalize_schedule(g_2k_char_buffer, intervals);
		}
	}

	if (pMedium) {
		storage_unmount(true);
		pMedium = false;
	}

	return count;
}

static int realize_intervals(schedule_interval_t raw_intervals[], int raw_interval_count,
		date_mapped_interval_t mapped_intervals[])
{
	// Calculate the start of today as a unix epoch time.
	struct tm now;
	time_t t_now = get_time_now(&now);
	(void) t_now;

	// Truncate the time to the start of the current day:
	now.tm_hour = 0;
	now.tm_min = 0;
	now.tm_sec = 0;
	now.tm_isdst = 0;		// No support for daylight savings time, the user needs to reset the clock manually.

	// Fill in some missing values:
	time_t t_today = mktime(&now);

	// Convert each interval start time to an epoch time, for yesterday, today and tomorrow:
	int j = 0;
	for (time_t day_offset = t_today - (time_t) SECONDS_PER_DAY;
			day_offset <= t_today + (time_t) SECONDS_PER_DAY;
			day_offset += SECONDS_PER_DAY) {
		for (int i = 0; i < raw_interval_count; i++) {
			mapped_intervals[j].start_epoch = raw_intervals[i].start_minutes * 60 + day_offset;
			mapped_intervals[j].duration_epoch = raw_intervals[i].duration_minutes * 60;
			j++;
		}
	}

	return j;
}

/**
 * Get the current time from the RTC, populating now with the broken down time
 * and returning the unix epoc time.
 */
static time_t get_time_now(struct tm *now)
{
	RTC_TimeTypeDef t;
	RTC_DateTypeDef d;

	memset(&t, 0, sizeof(t));
	memset(&d, 0, sizeof(d));
	int rc = HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
	// Why would this fail? Ignoring the possibility for now.
	(void) rc;

	// We *have* to call GetDate, otherwise the time is stuck. Duh.
	HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
	(void) d;

	now->tm_sec = t.Seconds;
	now->tm_min = t.Minutes;
	now->tm_hour = t.Hours;
	now->tm_mday = d.Date;			// 1 based.
	now->tm_mon = d.Month - 1;		// 0 based.
	now->tm_year = (int) d.Year + 2000;
	now->tm_isdst = -1;

	return mktime(now);		// Populate tm_wday, tm_yday and tm_isdst.
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hrtc);}

#if DO_HARDWARE_STANDBY

static void set_alarm(time_t alarm_epoch)
{
	// Convert the alarm_epoch to local split up time:
	struct tm lt;
	localtime_r(&alarm_epoch, &lt);

	// This code inspired by ioc generated code:
	RTC_AlarmTypeDef sAlarm = {0};
	sAlarm.AlarmTime.Hours = RTC_ByteToBcd2(lt.tm_hour);		// tm_hour is 0-23
	sAlarm.AlarmTime.Minutes = RTC_ByteToBcd2(lt.tm_min);		// tm_min is 0-59
	sAlarm.AlarmTime.Seconds = RTC_ByteToBcd2(lt.tm_sec);		// tm_sec is 0-59
	sAlarm.AlarmTime.SubSeconds = 0;
	sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
	sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
	sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
	sAlarm.AlarmDateWeekDay = RTC_ByteToBcd2(lt.tm_mday);		// tm_mday is 1 to 31
	sAlarm.Alarm = RTC_ALARM_A;
	// The HAL interrupt handle clears some bits to reset things on wake up:
	if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
	{
		Error_Handler();
	}
}

#endif

static void enter_standby(time_t alarm_epoch)
{
	/*
		I found it helpful to do a backup domain reset in main() during development to get a clean
		baseline for each run during development.

		You need to enable either an RTC alarm or an RTC wakeup in the ioc, which makes the NVIC setting
		available, then enable the interrupt under the NVIC tab. However, the code created then actually
		configures a wakeup or an alarm as part of the init code which you don't want and is confusing.
		A hack around this is to put a "return" in the user section in the init code.
	*/

#if DO_HARDWARE_STANDBY
	// Set an alarm to wake us from standby:
	set_alarm(alarm_epoch);

	HAL_SuspendTick();		// Otherwise the timer tick wakes up the stop mode immediately.

	// Enable debugging during standby mode. No effect on power consumption:
	HAL_DBGMCU_EnableDBGStandbyMode();
	// HAL_DBGMCU_DisableDBGStandbyMode();		// Saves a little current in standby mode, with the risk that we lock ourselves out.

	// We need a pull up on the wakeup pin, as we have an external pull down pin.
	// This is in addition to the setting in GPIO, as that is not active during standby.
	HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, GPIO_PIN_13);
	HAL_PWREx_EnablePullUpPullDownConfig();

	// Wake up when the the user switches away from auto mode. We sense this on PC13/PWR_WKUP2
	// OR wakeup on RTC AlarmA:
	// Magic needed to wake from standby via AlarmA:
	// see https://github.com/STMicroelectronics/STM32CubeU5/blob/main/Projects/NUCLEO-U575ZI-Q/Examples/PWR/PWR_LPMODE_RTC/Src/stm32u5xx_hal_msp.c
	HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN7_HIGH_3 | PWR_WAKEUP_PIN2_HIGH_1);

	__HAL_PWR_CLEAR_FLAG(PWR_WAKEUP_FLAG2);

	// Google suggests we need to do this to make sure register writes have registered before we
	// go to standby:
	(void)PWR->CR1;

	// This function shouldn't return:
	HAL_PWR_EnterSTANDBYMode();
#endif
}

static void exit_standby(void)
{
	// Only called when we simulate standby mode in the state machine.
}

static void enter_active(void)
{
	streaming_start(settings_get()->logger_sampling_rate_index);
	s_streaming_started = true;

	// Tell the data module we are ready for it to tell us about ready data buffers:
	data_acquisition_enable_capture(true);

	// Declare our intention to do some recording at some point, though maybe
	// not just yet:
	recording_open(settings_get_logger_sampling_rate());

	// Prime recording so that we can be ready to start recording with low latency:
	recording_prime();
}

static void exit_active(void)
{
	recording_close();
	streaming_stop();
	s_streaming_started = false;
}

static bool is_in_range(int v, int min, int max)
{
	return v >= min && v <= max;
}
