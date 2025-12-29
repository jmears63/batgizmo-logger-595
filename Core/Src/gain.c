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

#include "gain.h"

static uint16_t s_logical_index = 0;


// Map logic gain index to raw (chip) gain index so that we limit it
// to powers of two. We also don't allow the highest gain setting as it is dominated by
// microphone noise.
// The raw chip gains available are: +1, +2, +4, +5, +8, +10, +16, +32 V/V
// The mappings we provide are		 0   1   2       3        4
static uint16_t s_gain_mapping[GAIN_MAX_RANGE_INDEX + 1] = { 0, 1, 2, 4, 6 };
static uint16_t s_gain_values[GAIN_MAX_RANGE_INDEX + 1] = 	{ 1, 2, 4, 8, 16 };
// Bit shifts equivalent to the gain values for use with <<:
static uint16_t s_gain_shifts[GAIN_MAX_RANGE_INDEX + 1] = 	{ 0, 1, 2, 3, 4 };
static uint16_t s_gain_db[GAIN_MAX_RANGE_INDEX + 1] = 	{ 0, 6, 12, 18, 24 };
#define GAIN_DB_STEPSIZE 6

int gain_for_range(int range)
{
	return s_gain_values[range];
}

int gain_shift_for_range(int range)
{
	return s_gain_shifts[range];
}

/**
 * gain_index can GAIN_MIN_RANGE_INDEX to GAIN_MAX_RANGE_INDEX.
 */
static void set_gain(int logical_gain_index)
{
#if 0
	// Set the input channel to 0. Probably unnecessary as there is only one input channel.
	const uint16_t SETCHANNEL_CMD = 0x4001;
	const uint16_t CHANNEL = 0;
	uint16_t cmd = SETCHANNEL_CMD | CHANNEL;
	HAL_SPI_Transmit(&hspi1, (uint8_t*) &cmd, 1, 100);	// Guessing the timeout is in ms.
#endif

	// Set the gain index.
	const uint16_t SETGAIN_CMD = 0x4000;
	uint16_t raw_gain_index = s_gain_mapping[logical_gain_index];
	uint16_t cmd = SETGAIN_CMD | raw_gain_index;	// Set the gain.

	/*
	 *  Hack: send it twice, so that NSS goes to 1 after the first one. The chip needs that.
	 *  The data is actually sent in a low priority interrupt so the extra overhead doesn't matter
	 *  a lot.
	 */

	// static volatile as the ISR accesses it:
	static volatile uint16_t cmd_list[2];
	cmd_list[0] = cmd_list[1] = cmd;
	HAL_SPI_Transmit_IT(&hspi1, (uint8_t*) cmd_list, sizeof(cmd_list)/sizeof(uint16_t));
}

void gain_disable(void)
{
	// Set the gain index.
	const uint16_t SHUTDOWN_CMD = 0x2000;

	/*
	 *  Hack: send it twice, so that NSS goes to 1 after the first one. The chip needs that.
	 *  The data is actually sent in a low priority interrupt so the extra overhead doesn't matter.
	 */

	// static as the ISR accesses it.
	static volatile uint16_t cmd_list[2];
	cmd_list[0] = cmd_list[1] = SHUTDOWN_CMD;
	HAL_SPI_Transmit_IT(&hspi1, (uint8_t*) cmd_list, sizeof(cmd_list)/sizeof(uint16_t));
}

void gain_reenable(void)
{
	set_gain(s_logical_index);
}

void gain_init(void)
{
	s_logical_index = 3;
}

void gain_set(int gain_index, bool disabled)
{
	// Note the gain even if disabled, for use by reenable:
	s_logical_index = gain_index;

	if (disabled)
		gain_disable();
	else
		set_gain(s_logical_index);
}

void gain_set_db(int gain_db, bool disabled)
{
	// Convert the requested db to the nearest supported value.

	if (gain_db < s_gain_db[0])
		gain_db = s_gain_db[0];
	if (gain_db > s_gain_db[GAIN_MAX_RANGE_INDEX])
		gain_db = s_gain_db[GAIN_MAX_RANGE_INDEX];

	gain_set((gain_db - s_gain_db[0]) / GAIN_DB_STEPSIZE, disabled);
}

int gain_get_db()
{
	return s_gain_db[s_logical_index];
}

int gain_get_range(void)
{
	return s_logical_index;
}

int gain_get_shift(void)
{
	return s_gain_shifts[s_logical_index];
}

bool gain_up(void)
{
	if (s_logical_index < GAIN_MAX_RANGE_INDEX)
	{
		s_logical_index++;
		set_gain(s_logical_index);
		return true;
	}
	else
		return false;
}

bool gain_down(void)
{
	if (s_logical_index > GAIN_MIN_RANGE_INDEX)
	{
		s_logical_index--;
		set_gain(s_logical_index);
		return true;
	}
	else
		return false;
}
