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

#include "leds.h"
#include "main.h"

// Permanent LED bit mask, 1 is on:
int s_base_mask = 0;
int s_single_blink_mask = 0;

// Flashing masks for on and off:
int s_flash_mask_on = 0;
int s_flash_mask_off = 0;

typedef enum { state_base, state_single_blink, state_flash_on,  state_flash_off } led_state_t;
led_state_t s_state = state_base;
int s_signal_priority = 0;
int s_end_ticks = 0;
int s_flash_counter = 0;
int s_flashes_requested = 0;

#define FLAG_TO_SETINT(f) (f ? GPIO_PIN_RESET : GPIO_PIN_SET)

#define SINGLE_BLINK_DURATION_MS 500
#define FLASH_DURATION_MS (200 / 2)

static void _light_leds(int mask);

void leds_init(void)
{
	leds_reset();
}

void leds_reset(void)
{
	s_base_mask = 0;
	s_single_blink_mask = 0;
	s_flash_mask_on = 0;
	s_flash_mask_off = 0;

	s_state = state_base;
	s_signal_priority = 0;
	s_end_ticks = 0;
	s_flash_counter = 0;
	s_flashes_requested = 0;

	_light_leds(s_base_mask);
}

/**
 * Set the LED configuration to display in the absence of any signal. Update the LEDs right away if
 * there is no signal current.
 */
void leds_set(int mask, bool combine)
{
	if (combine)
		s_base_mask = mask | s_base_mask;
	else
		s_base_mask = mask;

	if (s_state == state_base)
		_light_leds(s_base_mask);
	else if (s_state == state_single_blink)
		_light_leds(s_base_mask | s_single_blink_mask);
}

/**
 * Signal single blink of the LEDs indicated by the mask.
 */
void leds_single_blink(int mask, int priority)
{
	if (priority >= s_signal_priority) {
		s_signal_priority = priority;
		s_single_blink_mask = mask;
		mask |= s_base_mask;
		_light_leds(mask);
		s_state = state_single_blink;
		s_end_ticks = HAL_GetTick() + SINGLE_BLINK_DURATION_MS;
	}
}

/**
 * Continuous flashing of the LEDs indicated by the mask. Other LEDS maintain their base state.
 * count is the number of flashes requested, or 0 for indefinite.
 */
void leds_flash(int mask, int count, int priority)
{
	if (priority >= s_signal_priority) {
		s_signal_priority = priority;
		s_flash_counter = 0;
		s_flashes_requested = count;
		s_flash_mask_on = mask | s_base_mask;
		s_flash_mask_off = s_base_mask & ~mask;

		_light_leds(s_flash_mask_on);
		s_state = state_flash_on;
		s_end_ticks = HAL_GetTick() + FLASH_DURATION_MS;
	}
}

void leds_cancel_signal(void)
{
	s_signal_priority = 0;
	s_state = state_base;
	_light_leds(s_base_mask);
}

void leds_main_processing(int main_tick_count)
{
	switch (s_state) {
		case state_base:
			break;

		case state_single_blink:
			if (HAL_GetTick() > s_end_ticks) {
				// Revert to the base LED setting:
				_light_leds(s_base_mask);
				s_end_ticks = 0;
				s_signal_priority = 0;
				s_state = state_base;
			}
			break;

		case state_flash_on:
			if (HAL_GetTick() > s_end_ticks) {
				_light_leds(s_flash_mask_off);
				s_end_ticks = HAL_GetTick() + FLASH_DURATION_MS;
				s_state = state_flash_off;
			}
			break;

		case state_flash_off:
			if (HAL_GetTick() > s_end_ticks) {
				if (s_flashes_requested > 0 && ++s_flash_counter == s_flashes_requested) {
					// We're done, revert to the base LED setting:
					_light_leds(s_base_mask);
					s_end_ticks = 0;
					s_signal_priority = 0;
					s_state = state_base;
				}
				else {
					// We've got more flashes to do:
					_light_leds(s_flash_mask_on);
					s_end_ticks = HAL_GetTick() + FLASH_DURATION_MS;
					s_state = state_flash_on;
				}
			}
			break;
	}
}

static void _light_leds(int mask)
{
	HAL_GPIO_WritePin(GPIO_LED_R_GPIO_Port, GPIO_LED_R_Pin, FLAG_TO_SETINT(mask & LED_RED));
	HAL_GPIO_WritePin(GPIO_LED_Y_GPIO_Port, GPIO_LED_Y_Pin, FLAG_TO_SETINT(mask & LED_YELLOW));
	HAL_GPIO_WritePin(GPIO_LED_G_GPIO_Port, GPIO_LED_G_Pin, FLAG_TO_SETINT(mask & LED_GREEN));
}
