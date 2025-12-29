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

#include "mode.h"
#include "modedefs.h"
#include "main.h"
#include "mode_manual.h"
#include "mode_auto.h"
#include "mode_usb.h"
#include "leds.h"
#include "storage.h"
#include "init.h"

typedef enum { MODE_NONE=0, MODE_MANUAL, MODE_AUTO, MODE_USB, MODE_LEN } mode_t;

static const mode_driver_t *mode_drivers[MODE_USB + 1];

static mode_t s_mode = MODE_NONE, s_tentative_new_mode = MODE_NONE;
static int s_tentative_tick_count = 0;

// Allow a little time before we act on the mode switch to account for
// contact bounce, and intermediate positions of the switch as it is slid:
#define MODE_SWITCH_DELAY_TICKS (1000 / MAIN_LOOP_DELAY_MS)

static void switch_to_mode(mode_t mode);

void mode_init(void)
{
	// Dumb data initialisation for each mode driver:
	mode_drivers[MODE_NONE] = NULL;
	mode_drivers[MODE_MANUAL] = &manual_mode_driver;
	mode_drivers[MODE_AUTO] = &auto_mode_driver;
	mode_drivers[MODE_USB] = &usb_mode_driver;

	// Initialise the mode driver modules:
	for (int i = 0; i < MODE_LEN; i++) {
		if (mode_drivers[i] != NULL)
			mode_drivers[i]->init();
	}

	// Finally initialize this module's data:
	s_mode = MODE_NONE;
	s_tentative_new_mode = MODE_NONE;
	s_tentative_tick_count = 0;
}

/**
 * Called from the main loop, periodically.
 */
void mode_main_processing(int main_tick_count)
{
	// Discover the current state of the mode switch:
	GPIO_PinState s1 = HAL_GPIO_ReadPin(GPIO_Mode_Auto_GPIO_Port, GPIO_Mode_Auto_Pin);
	GPIO_PinState s2 = HAL_GPIO_ReadPin(GPIO_Mode_USB_GPIO_Port, GPIO_Mode_USB_Pin);
	GPIO_PinState s3 = HAL_GPIO_ReadPin(GPIO_Mode_Manual_GPIO_Port, GPIO_Mode_Manual_Pin);

	mode_t switch_mode = MODE_NONE;
	if (s1 == GPIO_PIN_RESET) {
		switch_mode = MODE_AUTO;
	}
	else if (s2 == GPIO_PIN_RESET) {
		switch_mode = MODE_USB;
	}
	else if (s3 == GPIO_PIN_RESET) {
		switch_mode = MODE_MANUAL;
	}

	if (switch_mode != s_mode) {
		if (main_tick_count == 0) {
			// Immediately adopt the the mode of the initial switch setting:
			switch_to_mode(switch_mode);
		}
		else if (s_tentative_new_mode != switch_mode) {
			s_tentative_new_mode = switch_mode;
			s_tentative_tick_count = 0;
		}
		else {
			s_tentative_tick_count++;
			if (s_tentative_tick_count >= MODE_SWITCH_DELAY_TICKS) {
				leds_reset();	// Always clear the LEDs as we change mode.
				// The mode switch has been in the same position for a while,
				// so we can go ahead now and change mode:
				switch_to_mode(s_tentative_new_mode);
			}
		}
	}
}

static void switch_to_mode(mode_t mode)
{
	// Close with the current mode:
	const mode_driver_t *mode_driver = mode_drivers[s_mode];
	if (mode_driver)
		mode_driver->close();

	// The LEDs may be in any start: reset them for the new mode:
	leds_reset();

	// Read fresh settings etc on any mode change:
	init_read_all_settings();

	s_mode = mode;

	// Open the new mode:
	mode_driver = mode_drivers[s_mode];
	if (mode_driver)
		mode_driver->open();
}

