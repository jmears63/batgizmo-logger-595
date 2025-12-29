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
#include <arm_math.h>

#include "autophasecontrol.h"
#include "main.h"
#include "adc.h"
#include "leds.h"

#pragma GCC push_options
// Useful to disable optimisation when debugging this code.	Comment this out when not needed:
// #pragma GCC optimize ("O0")


static void clock_based_rate_adjuster(int32_t offset_error);
static void set_PLL_fraction(int32_t fraction);
static uint32_t get_dma_offset(void);

#define DO_APC 1

#define DO_DIAGNOSTICS 1	// Uses valuable SRAM.

#if DO_DIAGNOSTICS
#define DIAGNOSTICS_SAMPLES (SAMPLES_PER_FRAME * 4)
int16_t s_diagnostics[DIAGNOSTICS_SAMPLES], s_diagnostics1[DIAGNOSTICS_SAMPLES];
static size_t diagnostics_offset = 0;
#endif

// The following are defined by CMSIS-DSP:
// #define MIN(a, b)  (((a) < (b)) ? (a) : (b))
// #define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define RANGE_CLIP(lower, x, upper) MAX(MIN(x, upper), lower)

#define PLL_NOMINAL_FRACTION 3277 // + 288
#define PLL_MAX_CONTROL_DELTA 500
#define LOCKIN_DELTA_ALLOWED 3


static bool s_apc_active = false;
static bool s_locked_on = false;

void apc_init(void)
{
	s_apc_active = false;
	s_locked_on = false;
}

void apc_start(void)
{
	set_PLL_fraction(0);
	s_apc_active = true;
	s_locked_on = false;
}

void apc_stop(void)
{
	set_PLL_fraction(0);
	s_apc_active = false;
	s_locked_on = false;
}

bool apc_locked_on(void)
{
	return s_locked_on;
}

void apc_on_SoF(void)
{
	if (!s_apc_active)
		return;

#if 0
	static bool s_led_lit = false;
	s_led_lit = !s_led_lit;	// Toggle it each time through, as the time in this code is very brief.
	leds_set(s_led_lit ? LED_RED : LED_NONE, false);
#endif

	// Current DMA write position in 16 bit samples, always positive:
	uint32_t dma_offset = get_dma_offset();

	// Avoid exact numbers of half frame lengths, to keep USB frames out of sync with data acquisition
	// interrupts which are every half frame:
	const int32_t offset_target = SAMPLES_PER_FRAME * 3 >> 2;
	int32_t offset_error = (uint32_t) dma_offset - offset_target;

	// If offset_error is positive, USB is gaining on us, and we need to increase the sample
	// rate. So a positive error needs to result in reduced fractional part of the clock
	// divider.

	s_locked_on = (offset_error <= LOCKIN_DELTA_ALLOWED) && (offset_error >= -LOCKIN_DELTA_ALLOWED);
	clock_based_rate_adjuster(offset_error);

#if DO_DIAGNOSTICS
	s_diagnostics[diagnostics_offset] = offset_error;
	s_diagnostics1[diagnostics_offset] = dma_offset;
	diagnostics_offset = (diagnostics_offset + 1) % DIAGNOSTICS_SAMPLES;
	static int wrap_counter = 0;
	if (diagnostics_offset == 0)
	{
		wrap_counter++;
		if (wrap_counter > 5)
		{
			wrap_counter = 0;
		}
	}
#endif
}

static void clock_based_rate_adjuster(int32_t offset_error)
{
	/*
	 * PI controller. As the offset error increases, the DMA write offset is increasing too fast
	 * so we need to slow things down by reducing the fraction (which multiplies the clock).
	 *
	 * Note that P and I terms below are gains, not ranges, as is conventional in PID control
	 * theory.
	 *
	 * PI values are optimized by trial and error. Don't allow the feedback gain to be too high as this
	 * can result in a one bit change in error resulting in a discernable step in sampling frequency.
	 */

	const float P_COEFFICIENT = 3.0;    // Rapidly bring things under control.
	const float I_COEFFICIENT = 0.3; 	// Gradually bring the error down to zero.

	int p_fraction = -offset_error * P_COEFFICIENT;

	static float i_fraction = 0;
	i_fraction -= offset_error * I_COEFFICIENT;
	const float i_range = 500;			// Avoid integrator wind-up.
	i_fraction = RANGE_CLIP(-i_range, i_fraction, i_range);

	float fraction = p_fraction + (int32_t) i_fraction;
	fraction = RANGE_CLIP(-PLL_MAX_CONTROL_DELTA, fraction, PLL_MAX_CONTROL_DELTA);

#if DO_APC
	set_PLL_fraction(fraction);
#endif

#if 0
	s_diagnostics[diagnostics_offset] = offset_error;
	s_diagnostics1[diagnostics_offset] = fraction;		// fraction
	diagnostics_offset = (diagnostics_offset + 1) % DIAGNOSTICS_SAMPLES;
	static int wrap_counter = 0;
	if (diagnostics_offset == 0)
	{
		wrap_counter++;
		if (wrap_counter > 5)
		{
			wrap_counter = 0;
		}
	}
#endif

}

static void set_PLL_fraction(int32_t fraction)
{
	__HAL_RCC_PLL_FRACN_DISABLE();
	__HAL_RCC_PLL2FRACN_DISABLE();

	// Ideally we would set the following two values at precisely the same moment:
	__HAL_RCC_PLL_FRACN_CONFIG(PLL_NOMINAL_FRACTION + fraction);
	__HAL_RCC_PLL2FRACN_CONFIG(PLL_NOMINAL_FRACTION + fraction);

	__HAL_RCC_PLL_FRACN_ENABLE();
	__HAL_RCC_PLL2FRACN_ENABLE();
}

/*
 * Get the instantaneous DMA writing offset relating to ADC1.
 */
static uint32_t get_dma_offset(void)
{
	// Get the remaining data to transfer in the block in bytes:
	uint32_t bndt = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
	// Actually we want the number of 16 bit values, so halve it:
	bndt >>= 1;
	// Calculate the how far we are through the frame in data samples:
	return SAMPLES_PER_FRAME - bndt;
}

#pragma GCC pop_options
