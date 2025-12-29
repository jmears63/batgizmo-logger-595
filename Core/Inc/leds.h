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

#ifndef MY_LEDS_H
#define MY_LEDS_H

#include <stdbool.h>

// Define bit values that can be combined:
#define LED_RED 	1
#define LED_YELLOW 	2
#define LED_GREEN 	4
#define LED_ALL		(LED_RED | LED_YELLOW | LED_GREEN)
#define LED_NONE	0

void leds_init(void);
void leds_main_processing(int main_tick_count);
void leds_set(int mask, bool combine);
void leds_single_blink(int mask, int priority);
void leds_flash(int mask, int count, int priority);
void leds_cancel_signal(void);
void leds_reset(void);

#endif // MY_LEDS_H
