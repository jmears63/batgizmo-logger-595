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

#include "leds.h"
#include "main.h"

#define NUM_LEDS 3

#define FLASH_DURATION_MS (200 / 2)
#define SINGLE_BLINK_DURATION_MS 500

// State that applies to all LEDS:
static enum {
	flash_state_none, flash_state_on, flash_state_off
} s_flash_state = flash_state_none;

// State that applies per LED:
static enum {
	blink_state_none, blink_state_on
} s_blink_state[NUM_LEDS] = { blink_state_none, blink_state_none, blink_state_none };

static int s_blink_end_ticks[NUM_LEDS] = { 0, 0, 0 };

static int s_flash_counter = 0;
static const int s_flashes_requested = 10;
static int s_flash_next_ticks = 0;

static void do_blink(int led);
static void do_set(int led, bool lit);
static void do_flash(void);

void leds_init(void) {
	leds_reset();
}

void leds_reset(void) {
	s_flash_state = flash_state_none;
	s_blink_state[LEDS_RED] = s_blink_state[LEDS_GREEN] = s_blink_state[LEDS_YELLOW] = blink_state_none;
	s_blink_end_ticks[LEDS_RED] = s_blink_end_ticks[LEDS_GREEN] = s_blink_end_ticks[LEDS_YELLOW] = 0;
	s_flash_counter = 0;
	s_flash_next_ticks = 0;

	leds_set(LEDS_ALL, false);
}

void leds_main_processing(int main_tick_count) {
	if (s_flash_state != flash_state_none)
		do_flash();
	else {
		do_blink(LEDS_GREEN);
		do_blink(LEDS_YELLOW);
		do_blink(LEDS_RED);
	}
}

/**
 * Set an individual or all LEDS to be on or off in a stateless way.
 */
void leds_set(int led, bool lit) {

	// Only do the set if we are not currently flashing:
	if (s_flash_state == flash_state_none) {
		do_set(led, lit);
	}
}

/**int s_flash_counter = 0;
int s_flashes_requested = 0;
 *
 * Blink an individual LED.
 */
void leds_blink(leds_led_t led) {
	if (led >= 0 && led < NUM_LEDS) {
		// Don't blink if we are in the process of flashing:
		if (s_flash_state == flash_state_none) {
			s_blink_state[led] = blink_state_on;
			s_blink_end_ticks[led] = HAL_GetTick() + SINGLE_BLINK_DURATION_MS;
			leds_set(led, true);
		}
	}
}

void leds_start_flash(void) {
	// Interrupt any blinking in progress:
	s_blink_state[LEDS_RED] = s_blink_state[LEDS_GREEN] = s_blink_state[LEDS_YELLOW] = blink_state_none;
	s_flash_counter = 0;
	s_flash_next_ticks = HAL_GetTick() + FLASH_DURATION_MS;
	s_flash_state = flash_state_on;

	do_set(LEDS_ALL, true);
}

static void do_blink(int led) {
	if (s_blink_state[led] == blink_state_on) {
		if (HAL_GetTick() > s_blink_end_ticks[led]) {
			s_blink_state[led] = blink_state_none;
			s_blink_end_ticks[led] = 0;
			do_set(led, false);
		}
	}
}

static void do_flash() {
	const uint32_t ticks = HAL_GetTick();
	if (ticks > s_flash_next_ticks) {
		switch (s_flash_state) {
			case flash_state_on:
				do_set(LEDS_ALL, true);
				s_flash_state = flash_state_off;
				break;

			case flash_state_off:
			case flash_state_none:	// Paranoia.
				do_set(LEDS_ALL, false);
				s_flash_state = flash_state_on;
				s_flash_counter++;
				break;
		}
		s_flash_next_ticks = ticks + FLASH_DURATION_MS;

		if (s_flash_counter >= s_flashes_requested) {
			// We have finished flashing:
			s_flash_counter = 0;
			s_flash_state = flash_state_none;
			do_set(LEDS_ALL, false);
		}
	}
}

static void do_set(int led, bool lit) {
	const GPIO_PinState value = lit ? GPIO_PIN_RESET : GPIO_PIN_SET;
	if (led == LEDS_ALL) {
		HAL_GPIO_WritePin(GPIO_LED_R_GPIO_Port, GPIO_LED_R_Pin, value);
		HAL_GPIO_WritePin(GPIO_LED_Y_GPIO_Port, GPIO_LED_Y_Pin, value);
		HAL_GPIO_WritePin(GPIO_LED_G_GPIO_Port, GPIO_LED_G_Pin, value);
	}
	else {
		switch (led) {
			case LEDS_RED: HAL_GPIO_WritePin(GPIO_LED_R_GPIO_Port, GPIO_LED_R_Pin, value); break;
			case LEDS_YELLOW: HAL_GPIO_WritePin(GPIO_LED_Y_GPIO_Port, GPIO_LED_Y_Pin, value); break;
			case LEDS_GREEN: HAL_GPIO_WritePin(GPIO_LED_G_GPIO_Port, GPIO_LED_G_Pin, value); break;
		}
	}
}
