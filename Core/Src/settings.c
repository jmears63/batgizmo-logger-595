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
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "settings.h"
#include "gain.h"
#include "buffer.h"

#define JSMN_STRICT
#include "jsmn.h"

static void process_trigger_flags(settings_t *ps);
static void process_trigger_thresholds(settings_t *ps);

// Default values to use when there are no settings readable from SD card:
static settings_t s_settings = {
		// Default values for settings, which can be individually overridden
		// by providing a settings file on the SD card.
		max_sampling_time_s: 5,		// Align with the BTO pipeline.
		min_sampling_time_s: 2,
		pretrigger_time_s: 0.5,
		sensitivity_range: 3,
		sensitivity_disable: false,
		write_settings_to_sd: 1,
		trigger_max_count: 16,
		trigger_string: 			"*  x  x  x  x  x  x  x  x  x  *  *  *  *  *  *",
		trigger_thresholds_string: 	"67 67 51 51 47 47 45 43 42 42 42 36 36 36 36 36",
		disable_usb_msc: false,		// TODO: true is incompatible with the build with ENABLE_USB_MSC defined as 0.
		longitude: 0,
		latitude: 0,
		logger_sampling_rate_index: 8,		// Sampling rate as multiples of 48 kHz: 5:240, 6:288, 7: 336, 8:384, 9:432: 10:480, 11:528
		gated_recording: false,		// Will we write data to SD at the same time as acquiring it?

		_trigger_thresholds: {0},
		_trigger_flags: {false},
		_firmware_version: FIRMWARE_VERSION,
		_location_present: false
};

// Lifted from the jsmn example code:
static bool json_eq_string(const char *json, jsmntok_t *tok, const char *s)
{
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return true;
  }
  return false;
}

static bool json_get_integer(const char *json, jsmntok_t *tok, int *value)
{
	// Attempt to extract an integer from the token:
	if (tok->type == JSMN_PRIMITIVE) {
		const char *cptr = json + tok->end;
		char c = *cptr;
		* (char*) cptr = '\0';	// Temporarily terminate the string after the value.
		const char *s = json + tok->start, *tailptr = s;
		*value = strtod(s, (char**) &tailptr);
		* (char*) cptr = c;
		return tailptr > s;
	}

	return false;
}

static bool json_get_float(const char *json, jsmntok_t *tok, float *value)
{
	// Attempt to extract an integer from the token:
	if (tok->type == JSMN_PRIMITIVE) {
		const char *cptr = json + tok->end;
		char c = *cptr;
		* (char*) cptr = '\0';	// Temporarily terminate the string after the value.
		const char *s = json + tok->start, *tailptr = s;
		*value = strtof(s, (char**) &tailptr);
		* (char*) cptr = c;
		return tailptr > s;
	}

	return false;
}

static bool json_get_bool(const char *json, jsmntok_t *tok, bool *value)
{
	// Attempt to extract an integer from the token:
	if (tok->type == JSMN_PRIMITIVE) {
		const char *cptr = json + tok->end;
		char c = *cptr;
		* (char*) cptr = '\0';	// Temporarily terminate the string after the value.
		const char *s = json + tok->start;
		bool rc = false;
		if (strncmp(s, "true", 4) == 0) {
			*value = true;
			rc = true;
		}
		else if (strncmp(s, "false", 5) == 0) {
			*value = false;
			rc = true;
		}
		* (char*) cptr = c;
		return rc;
	}

	return false;
}

static int json_get_string(const char *json, jsmntok_t *tok, char *buf, size_t buflen)
{
	// Attempt to extract an integer from the token:
	if (tok->type == JSMN_STRING) {
		const char *cptr = json + tok->end;
		char c = *cptr;
		* (char*) cptr = '\0';	// Temporarily terminate the string after the value.
		const char *s = json + tok->start;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
		strncpy(buf, s, buflen);
#pragma GCC diagnostic pop
		buf[buflen - 1] = '\0';	// strncpy can leave the string unterminated.
		* (char*) cptr = c;
		return strlen(buf);
	}

	return 0;
}


void settings_init(void)
{
}

const settings_t *settings_get(void)
{
	return &s_settings;
}

static int clip_to_int_range(int value, int min, int max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;

	return value;
}

static float clip_to_float_range(float value, float min, float max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;

	return value;
}

#define MAX_SETTINGS_TOKENS 64

bool settings_parse_and_process_json_settings(const char *json)
{
	jsmn_parser parser;
	jsmntok_t tokens[MAX_SETTINGS_TOKENS];
	typedef enum { top_level /*, trigger_item */ } parser_scope_t;

	/*
	 * For now, error handling is as follows:
	 * 	If it is not valid, we give up and return false.
	 * 	If it is valid, we process each token as best we can, failing silently leaving the value
	 * 	as default, or silently clipping its value within the valid range.
	 */

	jsmn_init(&parser);
	int token_count = jsmn_parse(&parser, json, strlen(json), tokens, MAX_SETTINGS_TOKENS);
	if (token_count < 0)
		return false;

	parser_scope_t parser_scope = top_level;
	// int trigger_entries = 0, trigger_entry_index = 0, trigger_item_size = 0, trigger_item_values_seen = 0;
	for (int i = 0; i < token_count; i++) {
		jsmntok_t token = tokens[i];
		switch (parser_scope) {
			case top_level: {
				if (json_eq_string(json, &token, "max_sampling_time_s")) {
					// The value is the next token:
					token = tokens[++i];
					float float_value;
					if (json_get_float(json, &token, &float_value))
						s_settings.max_sampling_time_s = clip_to_float_range(float_value, 0.5, 120);
				}
				else if (json_eq_string(json, &token, "min_sampling_time_s")) {
					// The value is the next token:
					token = tokens[++i];
					float float_value;
					if (json_get_float(json, &token, &float_value))
						s_settings.min_sampling_time_s = clip_to_float_range(float_value, 0.5, 120);
				}
				else if (json_eq_string(json, &token, "pretrigger_time_s")) {
									// The value is the next token:
									token = tokens[++i];
									float float_value;
									if (json_get_float(json, &token, &float_value))
										s_settings.pretrigger_time_s = clip_to_float_range(float_value, 0.0, 2.0);
								}
				else if (json_eq_string(json, &token, "sensitivity_range")) {
					// The value is the next token:
					token = tokens[++i];
					int int_value;
					if (json_get_integer(json, &token, &int_value))
						s_settings.sensitivity_range = clip_to_int_range(int_value, 0, GAIN_MAX_RANGE_INDEX);
				}
				else if (json_eq_string(json, &token, "sensitivity_disable")) {
					// The value is the next token:
					token = tokens[++i];
					bool bool_value = false;
					if (json_get_bool(json, &token, &bool_value))
						s_settings.sensitivity_disable = bool_value;
				}
				else if (json_eq_string(json, &token, "write_settings_to_sd")) {
					// The value is the next token:
					token = tokens[++i];
					bool bool_value;
					if (json_get_bool(json, &token, &bool_value))
						s_settings.write_settings_to_sd = bool_value;
				}
				else if (json_eq_string(json, &token, "trigger_max_count")) {
					// The value is the next token:
					token = tokens[++i];
					int int_value;
					if (json_get_integer(json, &token, &int_value))
						s_settings.trigger_max_count = clip_to_int_range(int_value, 1, MAX_TRIGGER_MATCH_CLAUSES);
				}
				else if (json_eq_string(json, &token, "trigger")) {
					// The value is the next token:
					token = tokens[++i];
					json_get_string(json, &token, s_settings.trigger_string, SETTINGS_TRIGGER_MATCH_LEN);
				}
				else if (json_eq_string(json, &token, "trigger_thresholds")) {
					// The value is the next token:
					token = tokens[++i];
					json_get_string(json, &token, s_settings.trigger_thresholds_string, SETTINGS_TRIGGER_MATCH_LEN);
				}
				else if (json_eq_string(json, &token, "disable_usb_msc")) {
					// The value is the next token:
					token = tokens[++i];
					bool bool_value;
					if (json_get_bool(json, &token, &bool_value))
						s_settings.disable_usb_msc = bool_value;
				}
				else if (json_eq_string(json, &token, "location")) {
					// The value is the next token:
					token = tokens[++i];
					json_get_string(json, &token, g_128bytes_char_buffer, LEN_128BYTES_BUFFER);
					// Attempt to parse out the latitude and longitude allowing arbitrary space between them:
					double latitude, longitude;
					if (sscanf(g_128bytes_char_buffer, "%lf %lf", &latitude, &longitude) == 2) {
						s_settings.latitude = latitude;
						s_settings.longitude = longitude;
						s_settings._location_present = true;
					}
					else {
						s_settings.latitude = 0;
						s_settings.longitude = 0;
						s_settings._location_present = false;
					}
				}
				else if (json_eq_string(json, &token, "logger_sampling_rate_index")) {
					// The value is the next token:
					token = tokens[++i];
					int int_value;
					if (json_get_integer(json, &token, &int_value))
						s_settings.logger_sampling_rate_index = clip_to_int_range(int_value,
								SETTINGS_MIN_SAMPLING_RATE_INDEX, SETTINGS_MAX_SAMPLING_RATE_INDEX);
				}
				else if (json_eq_string(json, &token, "gated_recording")) {
					// The value is the next token:
					token = tokens[++i];
					bool bool_value;
					if (json_get_bool(json, &token, &bool_value))
						s_settings.gated_recording  = bool_value;
				}
				else {
					// Intentionally ignore unknown tokens to allow for compatibility when we add new tokens.
				}
			}
			break;
		}
	}

	process_trigger_flags(&s_settings);
	process_trigger_thresholds(&s_settings);

	return true;
}

static void process_trigger_flags(settings_t *ps)
{
	// Split the string based on whitespace (space, tab, newline). Note that the original
	// string is modified as terminators are inserted into it.
	// Loop through the tokens. This treats multiple whitespace as one.
	const char *ws = " \t\n";

	// We have to work on a copy of the string, as strtok modifies it:
	strcpy(g_2k_char_buffer, ps->trigger_string);
	char *token = strtok(g_2k_char_buffer, ws);
	int tokens_processed = 0;
	while (token != NULL) {
		if (tokens_processed < MAX_TRIGGER_MATCH_CLAUSES) {
			ps->_trigger_flags[tokens_processed] = stricmp(token, "x") == 0;
			tokens_processed++;
		}
		token = strtok(NULL, ws);
	}

	for (int i = tokens_processed; i < MAX_TRIGGER_MATCH_CLAUSES; i++) {
		// In case not enough were supplied, just fill up with * values:
		ps->_trigger_flags[i] = false;
	}
}

static void process_trigger_thresholds(settings_t *ps)
{
/*
	Parse out the matches string into the form we need to use the trigger.
	The match string is in this format: * * * >n >n * * * * ...
	Each entry corresponds to a frequency bucket. n the threshold in dB. The final
	... means repeat the last entry up to the full number.
*/

	// Split the string based on whitespace (space, tab, newline). Note that the original
	// string is modified as terminators are inserted into it.
	// Loop through the tokens. This treats multiple whitespace as one.
	const char *ws = " \t\n";

	// We have to work on a copy of the string, as strtok modifies it:
	strcpy(g_2k_char_buffer, ps->trigger_thresholds_string);
	char *token = strtok(g_2k_char_buffer, ws);
	int tokens_processed = 0;
	while (token != NULL) {
		if (tokens_processed < MAX_TRIGGER_MATCH_CLAUSES) {
			if (*token == '*') {
				// Ignore this frequency bucket:
				ps->_trigger_thresholds[tokens_processed] = SETTINGS_IGNORE_TRIGGER_VALUE;
			}
			else {
				float db = 0.0;
				/*int n =*/ sscanf(token, "%f", &db);

				// TODO limit the value of db to within sane limits that avoid an integer overflow
				// below.

				/*
				 * We need to convert the floating point dB value to a raw q31_t value that can be
				 * directly used in the data stream. 0 dB is the value corresponding to 0x0004 in q31_t,
				 * the smallest measurable value for 14 bit data, on the most sensitive gain range
				 * we offer which is range GAIN_MAX_RANGE_INDEX;
				 */

				// Convert the dB value to a factor relative to power at 0x0004 on the most sensitive
				// range.
				float factor = powf(10, db / 20.0);
				// Calculate the value on the most sensitive range (which can be > 7FFF):
				q31_t reference = 0x0004;	// Based on 14 bit data.
				float result = factor * reference + 0.5;
				// Square for comparability with squares in the frequency buckets:
				ps->_trigger_thresholds[tokens_processed] = result * result;
			}

			tokens_processed++;
		}
		token = strtok(NULL, ws);
	}

	for (int i = tokens_processed; i < MAX_TRIGGER_MATCH_CLAUSES; i++) {
		// In case not enough were supplied, just fill up with * values:
		ps->_trigger_thresholds[i] = SETTINGS_IGNORE_TRIGGER_VALUE;
	}
}

size_t settings_get_json_settings_string(char *buf, size_t buflen)
{
	snprintf(buf, buflen,
			"{\n"										\
			"  \"firmware_version\":%s,\n"				\
			"  \"max_sampling_time_s\":%.1f,\n"			\
			"  \"min_sampling_time_s\":%.1f,\n"			\
			"  \"pretrigger_time_s\":%.1f,\n"			\
			"  \"sensitivity_range\":%d,\n"				\
			"  \"sensitivity_disable\":%s,\n"			\
			"  \"write_settings_to_sd\":%s,\n"			\
			"  \"trigger_max_count\":%d,\n"				\
			"  \"trigger\":\"%s\",\n"			\
			"  \"trigger_thresholds\":\"%s\",\n"		\
			"  \"disable_usb_msc\":%s,\n"				\
			"  \"logger_sampling_rate_index\":%d,\n"	\
			"  \"gated_recording\":%s\n"				\
			"}\n",
			s_settings._firmware_version,
			s_settings.max_sampling_time_s,
			s_settings.min_sampling_time_s,
			s_settings.pretrigger_time_s,
			s_settings.sensitivity_range,
			s_settings.sensitivity_disable ? "true" : "false",
			s_settings.write_settings_to_sd ? "true" : "false",
			s_settings.trigger_max_count,
			s_settings.trigger_string,
			s_settings.trigger_thresholds_string,
			s_settings.disable_usb_msc ? "true" : "false",
			s_settings.logger_sampling_rate_index,
			s_settings.gated_recording ? "true" : "false"
		);

	return strlen(buf);
}

static bool get_minutes(char *s, int *m)
{
	int hours, minutes;
	int n = sscanf(s, "%d:%d", &hours, &minutes);
	if (n == 2 && hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60) {
		minutes += hours * 60;
		*m = minutes;
		return true;
	}
	return false;
}

static int compare_intervals(const void *pv1, const void *pv2)
{
	const schedule_interval_t *pi1 = * (schedule_interval_t **) pv1,
			*pi2 = * (schedule_interval_t **) pv2;
	if (pi1->start_minutes == pi2->start_minutes)
		return 0;
	else if (pi1->start_minutes < pi2->start_minutes)
		return -1;
	else
		return 1;
}

static int max_int(int i, int j)
{
	if (i > j)
		return i;
	else
		return j;
}

/**
 * Sort the intervals provided by start time and merge any that overlap.
 */
static int calculate_resultant_intervals(schedule_interval_t intervals[], int count,
		schedule_interval_t resultant_intervals[])
{
	int resultant_count = 0;

	// Create an array of pointers to intervals, sorted by the start of the interval:
	schedule_interval_t *sorted_intervals[count];
	for (int i = 0; i < count; i++)
		sorted_intervals[i] = &intervals[i];
	qsort(sorted_intervals, count, sizeof(schedule_interval_t *), compare_intervals);

	if (count > 0) {
		schedule_interval_t *pI = sorted_intervals[0];
		// start and duration contain the results of merging intervals, initialised from the first
		// interval in the list:
		int start = pI->start_minutes;
		int duration = pI->duration_minutes;

		for (int i = 1; i < count; i++) {
			pI = sorted_intervals[i];
			// Check for any overlap:
			if (pI->start_minutes > start + duration) {
				// No overlap with our current merged interval, so copy that latter over.
				resultant_intervals[resultant_count].start_minutes = start;
				resultant_intervals[resultant_count].duration_minutes = duration;
				resultant_count++;

				// Start again with the current entry:
				start = pI->start_minutes;
				duration = pI->duration_minutes;
			}
			else {
				// This entry starts before the end of the previous one so merge them.
				// Note that they might fully or partially overlap, hence the max_int:
				duration = max_int(duration, pI->start_minutes + pI->duration_minutes);
			}
		}
		resultant_intervals[resultant_count].start_minutes = start;
		resultant_intervals[resultant_count].duration_minutes = duration;
		resultant_count++;
	}

	return resultant_count;
}

#define MAX_SCHEDULE_TOKENS 64

/**
 * Parse the JSON supplied and populate the array of intervals, merging any intervals that overlap.
 * Return the number of intervals, or -1 if it didn't work out.
 */
int settings_parse_and_normalize_schedule(const char *json, schedule_interval_t resultant_intervals[])
{
	jsmn_parser parser;
	jsmntok_t tokens[MAX_SCHEDULE_TOKENS];
	schedule_interval_t intervals[MAX_SCHEDULE_INTERVALS];

	jsmn_init(&parser);
	int token_count = jsmn_parse(&parser, json, strlen(json), tokens, MAX_SCHEDULE_TOKENS);
	if (token_count < 0)
		return -1;

	int i = 0;
	jsmntok_t token = tokens[i++];
	if (token.type != JSMN_OBJECT)
		return -1;

	token = tokens[i++];
	if (!json_eq_string(json, &token, "schedule"))
		return -1;

	token = tokens[i++];
	if (token.type != JSMN_ARRAY)
		return -1;

	typedef enum { OBJECT, START, END } expecting_t;
	expecting_t expecting = OBJECT;
	char start[8], end[8];
	int m_start = 0, m_end = 0;		// Total minutes into the day.
	int interval_index = 0;
	bool valid_times = false;
	for (; i < token_count; i++) {
		token = tokens[i];
		switch (expecting) {
			case OBJECT:
				if (token.type != JSMN_OBJECT)
					return false;
				if (interval_index == MAX_SCHEDULE_INTERVALS)
					return -1;
				valid_times = true;
				// Note that we have a hard coded expectation that the start value
				// precedes the end value - which is not quite JSON, but never mind.
				expecting = START;
				break;
			case START:
				if (json_eq_string(json, &token, "from")) {
					token = tokens[++i];
					if (json_get_string(json, &token, start, sizeof(start)) > 0) {
						valid_times = valid_times && get_minutes(start, &m_start);
					}
				}
				else {
					return -1;
				}
				expecting = END;
				break;
			case END:
				if (json_eq_string(json, &token, "to")) {
					token = tokens[++i];
					if (json_get_string(json, &token, end, sizeof(end)) > 0) {
						valid_times = valid_times && get_minutes(end, &m_end);
						intervals[interval_index].start_minutes = m_start;
						int duration = m_end - m_start;
						if (duration < 0) {
							// If the end is before the start, we take that to mean that it
							// spans midnight. We are not supporting daylight savings time.
							duration += 24 * 60;
						}
						// duration += 1;	// Inclusive of the final minute, so minute 3 to 3 is one minute.
						intervals[interval_index].duration_minutes = duration;
						interval_index++;
					}
				}
				else {
					return -1;
				}
				expecting = OBJECT;
				break;
		}
	}

	return calculate_resultant_intervals(intervals, interval_index, resultant_intervals);
}

int settings_get_logger_sampling_rate(void) {
	return s_settings.logger_sampling_rate_index * SETTINGS_SAMPLING_RATE_MULTIPLIER_KHZ * 1000;
}
