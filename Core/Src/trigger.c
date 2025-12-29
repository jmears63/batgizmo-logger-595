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

#include "main.h"
#include "trigger.h"
#include <arm_math.h>
#include "cmplx_mag_squared.h"
#include "settings.h"
#include "gain.h"
#include "data_acquisition.h"
#include "leds.h"
#include "data_processor_buffers.h"

/**
 * Flags used to communicate between interrupt context and main processing consumers of the flag.
 * Usage is for interrupt context to set the flag, and consumers to reset it when they consumed it.
 * Setting a bool is atomic on this processor. Volatile so that the compiler doesn't get ideas about
 * putting the value in a CPU register (which is probably wouldn't for a global but who knows).
 */
volatile bool g_trigger_triggered = false;
volatile bool g_trigger_matches[MAX_TRIGGER_MATCH_CLAUSES] = {0};

#define FFT_WINDOW_SIZE_LOG2 5 									// The FFT output shift and window size need to correspond.
#define FFT_OUTPUT_SHIFT_BITS (FFT_WINDOW_SIZE_LOG2 - 1)		// See the CMSIS docs.
#define FFT_WINDOW_SIZE (1 << FFT_WINDOW_SIZE_LOG2)
#define FFT_INIT(a,b,c,d) arm_rfft_init_q15(a,b,c,d)
#define FFT_INSTANCE_TYPE arm_rfft_instance_q15

static FFT_INSTANCE_TYPE fft_instance;

/*
	import numpy as np

	# Generate a Hann window of 32 samples
	hann_window = np.hanning(32)

	# Create the C string to initialize a float array
	c_string = "static float fft_window[32] = {" + ", ".join(f"{val:.8f}f" for val in hann_window) + "};"

	# Print the C string
	print(c_string)
*/
static float fft_window_float[SAMPLES_PER_FRAME] = {
		0.00000000f, 0.01023503f, 0.04052109f, 0.08961828f, 0.15551654f,
		0.23551799f, 0.32634737f, 0.42428611f, 0.52532458f, 0.62532627f, 0.72019708f, 0.80605299f, 0.87937906f,
		0.93717331f, 0.97706963f, 0.99743466f, 0.99743466f, 0.97706963f, 0.93717331f, 0.87937906f, 0.80605299f,
		0.72019708f, 0.62532627f, 0.52532458f, 0.42428611f, 0.32634737f, 0.23551799f, 0.15551654f, 0.08961828f,
		0.04052109f, 0.01023503f, 0.00000000f
};

static q15_t fft_window_q15[SAMPLES_PER_FRAME];

static bool check_for_trigger(const q31_t fft_squared_output[], volatile bool *matches);
static bool check_each_window(volatile const q15_t *pRawData);


void trigger_init(void)
{
	FFT_INIT(&fft_instance, FFT_WINDOW_SIZE, 0, 1);
    arm_float_to_q15(fft_window_float, fft_window_q15, SAMPLES_PER_FRAME);

	// g_triggered = false;
	memset((void*) g_trigger_matches, '\0', sizeof(g_trigger_matches));
}

static volatile int s_counter = 0;

/**
 * Called in the context of main processing.
 *
 * Wait for a new half frame of data to be ready, process it for triggering, and if there was a trigger
 * and no race condition, publish the trigger.
 */
void trigger_main_fast_processing(int main_tick_count)
{
	if (g_raw_half_frame_ready) {
		// Consume the trigger:
		g_raw_half_frame_ready = false;
		int count1 = g_raw_half_frame_counter;
		bool triggered = check_each_window(g_raw_half_frame);
		// Detect a race condition: ignore any trigger value as the raw data was being updated
		// while we were working on it.
		if (triggered) {
			if (g_raw_half_frame_counter == count1) {
				s_counter++;
				// Tell any interested parties that there has been a trigger:
				g_trigger_triggered = true;
			}
		}
	}
}

static bool check_each_window(volatile const q15_t *pRawData)
{
	static q15_t fft_output[FFT_WINDOW_SIZE * 2], working_copy[FFT_WINDOW_SIZE];
	static q31_t fft_squared_modulus[FFT_WINDOW_SIZE / 2];

	volatile const q15_t *pFftSrc = pRawData;
	bool triggered = false;

	// There aren't enough CPU cycles to evaluate all the windows:
	const int windows_to_check_log2 = 1;	// We'll evaluate two of the windows, distributed.
	const int windows_to_check = 1 << windows_to_check_log2;
	const int increment = HALF_SAMPLES_PER_FRAME >> windows_to_check_log2;

	for (int i = 0; i < windows_to_check; i++, pFftSrc += increment) {
		// The FFT function modifies the source buffer, so we copy it. An optimization might
		// be modify it in place, once we no longer need it:
		memcpy(working_copy, (void*) pFftSrc, sizeof(working_copy));
		// Apply the window to avoid spectral leakage:
		// Calculate the frequency buckets:
		arm_mult_q15(fft_window_q15, working_copy, working_copy, FFT_WINDOW_SIZE);
		arm_rfft_q15(&fft_instance, working_copy, fft_output);
		// The FFT scales down to avoid overflow, so we unscale the output:
		arm_shift_q15(fft_output, FFT_OUTPUT_SHIFT_BITS, fft_output, FFT_WINDOW_SIZE * 2);
		// Avoid arm_cmplx_mag_q15 as it includes a square root we don't want, since
		// power is what we are interested in.
		cmplx_mag_squared_q15_q31(fft_output, fft_squared_modulus, FFT_WINDOW_SIZE / 2);

		/*
			A side effect of the following call is to record the buckets that actually triggered.
			This will be written to guano data to aid in selecting trigger profiles.

			We want setting and consuming of the trigger data and flag to be consistent/atomic,
			which we can achieve by only updating the data when the flag is false, and having
			the reader reset the flag as its last step.
		*/
		// triggered = triggered || check_for_trigger(fft_squared_modulus, g_triggered ? NULL : g_trigger_matches);
		triggered = triggered || check_for_trigger(fft_squared_modulus, NULL);
	}
	return triggered;
}


#if MAX_TRIGGER_MATCH_CLAUSES != (FFT_WINDOW_SIZE / 2)
#	error("bucket count mismatch")
#endif

static bool check_for_trigger(const q31_t freq_buckets[], volatile bool *matches)
{
	const settings_t *ps = settings_get();
	const q31_t *pv = ps->_trigger_thresholds;
	const bool *pf = ps->_trigger_flags;

	int match_count = 0;

	// Bit shift we need to adjust thresholds for the gain range we are on:
	int shift = gain_get_shift();

	// The raw value is relative to the most sensitive range. For less sensitive ranges,
	// we need to shift accordingly to reduce the raw value. Also, the raw value is
	// a square for comparison with the frequency bucket value, so shift twice:
	int shift_for_gain = gain_shift_for_range(GAIN_MAX_RANGE_INDEX) - shift;

	for (int i = 0; i < MAX_TRIGGER_MATCH_CLAUSES; i++, pv++, pf++) {
		if ((*pf == false) || (*pv == SETTINGS_IGNORE_TRIGGER_VALUE)) {
			// Don't care about this bucket, nothing to do.
		}
		else {
			// Adjust the threshold value by the squared of the gain factor difference. A lower
			// gain range means we need to reduce the threshold. Note that we are dealing in squared values
			// so we do the shift twice:
			const q31_t threshold = (*pv >> shift_for_gain) >> shift_for_gain;

			bool matched = freq_buckets[i] >= threshold;
			if (matched)
				match_count++;
			if (matches)
				matches[i] = matched;
		}
	}

	bool triggered = (match_count > 0) && (match_count <= ps->trigger_max_count);

	return triggered;
}

