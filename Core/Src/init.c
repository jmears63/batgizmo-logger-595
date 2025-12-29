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
#include <stdio.h>

#include "init.h"
#include "sdmmc.h"
#include "gpio.h"
#include "rtc.h"
#include "app_filex.h"
#include "fx_api.h"
#include "fx_stm32_sd_driver.h"
#include "settings.h"
#include "storage.h"
#include "buffer.h"
#include "storage.h"


#define DATETIME_FILE_NAME "datetime.txt"
#define SETTINGS_FILE_NAME "settings.json"


void init_get_datetime_from_sd(FX_MEDIA *pMedium)
{
	char buf[32];
	FX_FILE file;
	ULONG actual_len = 0;

	memset(&file, 0, sizeof(file));
	if (fx_file_open(pMedium, &file, DATETIME_FILE_NAME, FX_OPEN_FOR_READ) == FX_SUCCESS) {
		// Allow buffer space for the final \0:
		/*UINT status =*/ fx_file_read(&file, (void *) buf, sizeof(buf) - 1, &actual_len);
		buf[actual_len] = '\0';
		fx_file_close(&file);
	}
	else
		return;	// No file.

	int year=0, month=0, day=0, hours=0, minutes=0, seconds=0;

	bool ok = (actual_len > 0);
	if (ok) {
		// Try to parse out the date and time provided according to yyyy-MM-ddTHH:mm:ss
		// We'll ignore any trailing data such as time zone.
		// FAT has no concept of time zone; linux seems to assume that is UTC and adjusts accordingly to BST
		// in the summer. Therefore, the user needs to set it as UTC. Other OSs may behave differently.
		// Year in the range 0-99:
		int count = sscanf(buf, "%*2s%2d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hours, &minutes, &seconds);
		ok = (count == 6);
	}

	if (ok) {
		RTC_TimeTypeDef sTime;
		sTime.Hours = RTC_ByteToBcd2(hours);
		sTime.Minutes = RTC_ByteToBcd2(minutes);
		sTime.Seconds = RTC_ByteToBcd2(seconds);
		sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
		sTime.StoreOperation = RTC_STOREOPERATION_RESET;
		ok = ok && (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) == HAL_OK);

		RTC_DateTypeDef sDate;
		sDate.WeekDay = RTC_WEEKDAY_MONDAY;		// Arbitrary, not used.
		sDate.Month = RTC_ByteToBcd2(month);
		sDate.Date = RTC_ByteToBcd2(day);
		sDate.Year = RTC_ByteToBcd2(year);
		ok = ok && (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) == HAL_OK);
	}

	storage_set_filex_time();		// So any file timestamp is right.

	if (ok) {
		// We processed the date/time successfully so will remove the file,
		// ignoring any error:
		fx_file_delete(pMedium, DATETIME_FILE_NAME);
	}
	else {
		// Something went wrong so we will rename it as a signal to the user:
		char err_file_name[sizeof(DATETIME_FILE_NAME) + 20];
		snprintf(err_file_name, sizeof(err_file_name), "%s.err", DATETIME_FILE_NAME);

		// In case it already exists. Usually this will fail which is fine:
		fx_file_delete(pMedium, err_file_name);

		// Ignore any errors:
		fx_file_rename(pMedium, DATETIME_FILE_NAME, err_file_name);

		// Get the file system to a consistent state:
    	fx_media_flush(pMedium);
	}
}

void init_get_settings_from_sd(FX_MEDIA *pMedium)
{
	FX_FILE file;

	memset(&file, 0, sizeof(file));
	if (fx_file_open(pMedium, &file, SETTINGS_FILE_NAME, FX_OPEN_FOR_READ) == FX_SUCCESS) {
		ULONG actual_len = 0;
		// Allow buffer space for the final \0:
		/*UINT status =*/ fx_file_read(&file, (void *) g_2k_char_buffer, LEN_2K_BUFFER - 1, &actual_len);
		g_2k_char_buffer[actual_len] = '\0';
		fx_file_close(&file);

		bool ok = settings_parse_and_process_json_settings(g_2k_char_buffer);

		if (!ok) {
			storage_set_filex_time();		// So any file timestamp is right.

			// Something went wrong so we will rename it as a signal to the user:
			char err_file_name[sizeof(SETTINGS_FILE_NAME) + 20];
			snprintf(err_file_name, sizeof(err_file_name), "%s.err", SETTINGS_FILE_NAME);

			// In case it already exists. Usually this will fail which is fine:
			fx_file_delete(pMedium, err_file_name);

			// Ignore any errors:
			fx_file_rename(pMedium, SETTINGS_FILE_NAME, err_file_name);

			// Get the file system to a consistent state:
	    	fx_media_flush(pMedium);
		}
	}
}

void init_read_all_settings(void)
{
	// Normal mode for speed:
	FX_MEDIA *pMedium = storage_mount(STORAGE_FAST);
	if (pMedium) {
		init_get_datetime_from_sd(pMedium);
		init_get_settings_from_sd(pMedium);
		storage_unmount(true);
	}
}


/**
 * This function executes the power on startup sequence.
 */
void init_startup(void)
{
	// Anything we want to happend once on startup goes here.
}
