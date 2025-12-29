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

#ifndef INC_SETTINGS_H_
#define INC_SETTINGS_H_

#include <arm_math.h>
#include "stdbool.h"

#define MAX_TRIGGER_MATCH_CLAUSES 16
#define SETTINGS_TRIGGER_MATCH_LEN 128
#define SETTINGS_IGNORE_TRIGGER_VALUE ((q31_t) -1)

typedef struct {
	float max_sampling_time_s;
	float min_sampling_time_s;
	int sensitivity_range;
	bool sensitivity_disable;
	bool write_settings_to_sd;
	int trigger_max_count;
	char trigger_string[SETTINGS_TRIGGER_MATCH_LEN];			// Flags that enable/disable triggering per bucket.
	char trigger_thresholds_string[SETTINGS_TRIGGER_MATCH_LEN];	// Threshold for each bucket to trigger.
	bool disable_usb_msc;
	double longitude, latitude;				// Looking at example data from other detectors, 6 dps seems to be used.
	float pretrigger_time_s;

	// Some calculated fields:
	q31_t _trigger_thresholds[MAX_TRIGGER_MATCH_CLAUSES];	// Values for comparison with FFT buckets.
	bool _trigger_flags[MAX_TRIGGER_MATCH_CLAUSES];		// Flags that enable/disable triggering by each bucket.
	char _firmware_version[16];
	bool _location_present;
} settings_t;

/*
 * Minutes are in the range 0 to 24 * 60 - 1.
 * If the end minutes are less than the start, that means it spans midnight.
 */
typedef struct {
	int start_minutes;
	int duration_minutes;		// Use duration rather than end time to make midnight wrapping easier.
} schedule_interval_t;

#define MAX_SCHEDULE_INTERVALS 20

void settings_init(void);
const settings_t *settings_get(void);
bool settings_parse_and_process_json_settings(const char *json_string);
size_t settings_get_json_settings_string(char *buf, size_t buflen);
int settings_parse_and_normalize_schedule(const char *json, schedule_interval_t intervals[]);

#endif /* INC_SETTINGS_H_ */
