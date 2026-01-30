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

#include <stdatomic.h>
#include "data_processor_buffers.h"
#include "trigger.h"
#include "main.h"
#include "leds.h"

#define BLINK_LEDS 1


/*
 * The acquisition buffers are used to buffer frame data arriving from DMA, ready to be written to file.
 * The buffers are written in interrupt context and read in main context.
 * We use a list of buffers so that data can be continuously spooled through them, allowing
 * recent history to be available to support pretriggering.
 * The number of data buffers is adjusted to be the maximum that will fit into SRAM alongside
 * all the other data used by this firmware.
 */

///#define NUM_BUFFERS 74		// Maximize this to maximize our ability to pretrigger. Must be > 2.
							// 6 for STM32U535, up to 76 for STMU595.
							// 70 x 16K => 1146880 samples, @384kHz => 2.99s
							// 12 is about 0.5s.

#define NUM_BUFFERS 37		// For 64K chunk size.

#define BUFFER_DELTA 2		// The number of buffers margin we allow in calculations to avoid risk
							// of reading from a buffer that is being overwritten.
							// Must be less than NUM_BUFFERS.

#if NUM_BUFFERS <= BUFFER_DELTA
#error BUFFER_DELTA must be less than NUM_BUFFERS
#endif

///#define MAXIMUM_READ_LEAD 24	// We defer yielding values to consumers of the FIFO to read until they are
								// close to be overwritten by writes.
								// This value approximates to 1s, allowing time for the FileX/SD
								// to reopen the data file without data expiring.
#define MAXIMUM_READ_LEAD 12	// For 64K chunk size.

// We will rely on C's memory layout of the following, with the last index changing most
// rapidly. In other other words, &s_buffer_additional[NUM_BUFFERS][s_currently_writing_index] points to
// a single contiguous data buffer:
static RAM_DATA_SECTION sample_type_t s_buffers[NUM_BUFFERS][DATA_BUFFER_ENTRIES];

// The index and pointer of the buffer we are currently writing to, and the number
// of entries written to it so far:
static int s_active_buffer_index = 0;
static sample_type_t *s_active_buffer_ptr = &s_buffers[0][0];
static int s_active_buffer_entry_count = 0;

// The number of buffers ready for reading:
// static int s_ready_buffer_count = 0;

/**
 * Buffers are referred to using a 32 bit buffer index.
 * Some magic values are defined below to signal special events
 * such as the start and end of a sequence.
 *
 * Relax:(2^31 / 384 kHz) * 32K = 227 hours, about 9 days.
 *
 */
typedef enum {
	BUFFERFIFO_END_SEQUENCE =  (uint32_t) -1,
	BUFFERFIFO_START_SEQUENCE = (uint32_t) -2
} bufferfifo_ctrl_t;


// Count the total number of buffers filled, ever, no wrapping:
static int32_t s_unwrapped_filled_buffer_counter = 0;

/**
 *  The FIFO of unwrapped buffer indexes that constitute a sequence for writing to a file.
 *  For example:
 *
 *	BUFFERFIFO_START_SEQUENCE, 100, 101, 102...354, BUFFERFIFO_END_SEQUENCE,
 *	BUFFERFIFO_START_SEQUENCE, 300, 301, 302...405, BUFFERFIFO_END_SEQUENCE
 *
 */
#define BUFFER_FIFO_LENGTH (NUM_BUFFERS * 5)
static int32_t s_buffer_fifo[BUFFER_FIFO_LENGTH];
static volatile size_t s_buffer_fifo_next_read = 0, s_buffer_fifo_next_write = 0;
static volatile size_t s_buffer_fifo_count = 0;	 // Number of entries in the buffer FIFO include special values.

static bool s_is_triggered = false;
static int32_t s_trigger_unwrapped_buffer_count = 0;		// The buffer count at the moment of being triggered.
static int32_t s_final_unwrapped_buffer_for_trigger = 0;	// While we are triggered, continue writing buffers up to this value.
static data_processor_mode_t s_mode = DATA_PROCESSOR_TRIGGERED;
static volatile bool s_is_gated = false;
static volatile int s_gate_released_ticks = 0;
static volatile int s_trigger_count = 0;	// For debugging.

static int s_buffers_per_second = 0;

static void data_processor_buffers_on_trigger(int main_tick_count);

void data_processor_buffers_init(void)
{
	// Dummy value for samples_per_second will be set properly when we enter a specific mode:
	data_processor_buffers_reset(DATA_PROCESSOR_TRIGGERED, 0);
}

void data_processor_buffers_reset(data_processor_mode_t mode, int samples_per_second)
{
	s_mode = mode;
	s_active_buffer_index = 0;
	// s_ready_buffer_count = 0;
	s_active_buffer_entry_count = 0;
	s_active_buffer_ptr = &s_buffers[s_active_buffer_index][0];
	s_is_gated = false;
	s_gate_released_ticks = 0;

	s_unwrapped_filled_buffer_counter = 0;
	s_buffer_fifo_next_read = s_buffer_fifo_next_write = s_buffer_fifo_count = 0;
	s_is_triggered = false;
	s_trigger_unwrapped_buffer_count = s_final_unwrapped_buffer_for_trigger = 0;

	s_buffers_per_second = samples_per_second / DATA_BUFFER_ENTRIES;


	// No need to initialize_buffers to zero as .bss data is zeroed on startup.
	// And in any case, we will never read from a buffer before it has been
	// populated.
}

void data_processor_buffers_fast_main_processing(int main_tick_count)
{
	if (g_trigger_triggered) {
		g_trigger_triggered = false;	// Consume the trigger flag.
		data_processor_buffers_on_trigger(main_tick_count);
	}
}

static inline int add_and_wrap(int i, int delta, int modulo)
{
	i += delta;
	if (i >= modulo)
		i -= modulo;
	return i;
}

/**
 * This function is called from interrupt context.
 * Accordingly, instructions are carefully ordered and atomic increments/decrements are used for
 * variables also accessed from the main context.
 */
static void buffer_fifo_put(int32_t unwrapped_buffer_index) {
	s_buffer_fifo[s_buffer_fifo_next_write] = unwrapped_buffer_index;
	s_buffer_fifo_next_write = add_and_wrap(s_buffer_fifo_next_write, 1, BUFFER_FIFO_LENGTH);
	atomic_fetch_add(&s_buffer_fifo_count, 1); //	s_buffer_fifo_count++;
}

/**
 * This function is called in main context, so can interleave with calls to buffer_fifo_put.
 * Accordingly, instructions are carefully ordered and atomic increments/decrements are used
 * for variables also accessed from the interrupt context.
 */
static bool buffer_fifo_get(int32_t* unwrapped_buffer_index) {
	if (s_buffer_fifo_count > 0) {
		*unwrapped_buffer_index = s_buffer_fifo[s_buffer_fifo_next_read];
		s_buffer_fifo_next_read = add_and_wrap(s_buffer_fifo_next_read, 1, BUFFER_FIFO_LENGTH);
		atomic_fetch_sub(&s_buffer_fifo_count, 1); 	// s_buffer_fifo_count--;
		return true;
	}
	else
		return false;
}

static bool buffer_fifo_sniff(int32_t* unwrapped_buffer_index) {
	if (s_buffer_fifo_count > 0) {
		*unwrapped_buffer_index = s_buffer_fifo[s_buffer_fifo_next_read];
		return true;
	}
	else
		return false;
}

/**
 * This function is called in interrupt context when ADC/DMA has read a new half frame of data
 * from input. We add the data into the buffers managed by this module.
 */
void data_processor_buffers(const sample_type_t *pDMABuffer, int dma_buffer_offset, int count)
{
	// TODO consider replacing the following with CMSIS vector operations, or writing our own composite one.

	// Try to append the data to the currently writing buffer. We might need to copy data in two chunks
	// if the buffer fills up.

	// We could improve this to avoid the extra intermediate buffer. Rainy day stuff.

	bool gated_recording = settings_get()->gated_recording;
	if (gated_recording) {
		if (s_is_gated) {
			// Don't fill buffers when we are paused - the data is being
			// read and written to file. Just discard it.
			return;
		}
	}

	int samples_remaining = count;
	int free_entries = DATA_BUFFER_ENTRIES - s_active_buffer_entry_count;
	int samples_to_copy = free_entries < samples_remaining ? free_entries : samples_remaining;
	sample_type_t *pTargetDest = s_active_buffer_ptr + s_active_buffer_entry_count;
	const sample_type_t *pSource_q15 = pDMABuffer + dma_buffer_offset;
	for (int i = 0; i < samples_to_copy; i++) {
		*pTargetDest++ = *pSource_q15++;
	}
	s_active_buffer_entry_count += samples_to_copy;
	samples_remaining -= samples_to_copy;

	// Do we need to switch to the next buffer?
	if (s_active_buffer_entry_count >= DATA_BUFFER_ENTRIES) {
		// Switch to the next buffer:
		s_active_buffer_index += 1;
		if (s_active_buffer_index >= NUM_BUFFERS) {
			s_active_buffer_index = 0;
		}

		s_active_buffer_ptr = &s_buffers[s_active_buffer_index][0];
		s_active_buffer_entry_count = 0;

		if (s_mode == DATA_PROCESSOR_TRIGGERED) {
			// In triggered mode, populate the fifo subject to trigger logic.
			if (s_is_triggered) {
				if (gated_recording) {
					if (s_unwrapped_filled_buffer_counter > s_final_unwrapped_buffer_for_trigger) {
						// We've reached the end of the trigger:
						s_is_triggered = false;
						// Signal that this is the end of a triggered sequence:
						buffer_fifo_put(BUFFERFIFO_END_SEQUENCE);
						// This is the moment to start writing data to SD:
						s_is_gated = true;
					}
					else if (s_buffer_fifo_count >= NUM_BUFFERS + 1) {
						// The fifo is full, time to write to SD.
						buffer_fifo_put(BUFFERFIFO_END_SEQUENCE);
						s_is_gated = true;
					}
					else {
						// Push the buffer to the fifo:
						buffer_fifo_put(s_unwrapped_filled_buffer_counter);
					}
				}
				else {
					if (s_unwrapped_filled_buffer_counter > s_final_unwrapped_buffer_for_trigger) {
						// We've reached the end of the trigger:
						s_is_triggered = false;
						// Signal that this is the end of a triggered sequence:
						buffer_fifo_put(BUFFERFIFO_END_SEQUENCE);
					}
					else {
						// Continue pushing buffers to the fifo as long as we are in triggered state:
						buffer_fifo_put(s_unwrapped_filled_buffer_counter);
					}
				}
			}
		}
		else if (s_mode == DATA_PROCESSOR_CONTINUOUS) {
			// In continuous mode populate the fifo regardless of triggering.
			buffer_fifo_put(s_unwrapped_filled_buffer_counter);

			if (gated_recording) {
				// See if all the buffers are filled, allowing for the special START token:
				if (s_buffer_fifo_count >= NUM_BUFFERS + 1) {
					// We have filled all the buffers, so set the pause flag
					// to prevent any new data overwriting the buffers, and signal
					// the main context code that it can read the data now.
					buffer_fifo_put(BUFFERFIFO_END_SEQUENCE);
					s_is_gated = true;
				}
			}
		}

		// Track the total number of numbers filled without wrapping:
		s_unwrapped_filled_buffer_counter += 1;
	}

	// Is there any more data to write?
	if (samples_remaining > 0) {
		samples_to_copy = samples_remaining;
		pTargetDest = s_active_buffer_ptr + s_active_buffer_entry_count;
		for (int i = 0; i < samples_to_copy; i++) {
			*pTargetDest++ = *pSource_q15++;
		}
		s_active_buffer_entry_count += samples_to_copy;
		samples_remaining -= samples_to_copy;
	}

#if 0
	// Samples_remaining should always be zero at this point:
	while (samples_remaining > 0)
		;
#endif
}

/**
 * Function called by the recording layer to signal that it has finished
 * recording data to SD.
 */
void data_processor_buffers_on_recording_complete(int main_tick_count) {
	s_is_gated = false;
	s_gate_released_ticks = main_tick_count;

	if (s_mode == DATA_PROCESSOR_CONTINUOUS)
		buffer_fifo_put(BUFFERFIFO_START_SEQUENCE);
	else if (s_mode == DATA_PROCESSOR_TRIGGERED) {
		// Make sure the follow on file is at least the minimum length:
		int minimum = s_unwrapped_filled_buffer_counter + s_buffers_per_second * settings_get()->min_sampling_time_s;
		if (s_final_unwrapped_buffer_for_trigger < minimum)
			s_final_unwrapped_buffer_for_trigger = minimum;

		// If the trigger is still active, start of the next sequence:
		buffer_fifo_put(BUFFERFIFO_START_SEQUENCE);
		// The main get loop will pick things up from here.
	}
}

/**
 * Call this to get the next buffer to be written to file, if any.
 * The return value is true if we should close the current file.
 * *buffer is set to NULL if no data is available.
 */
bool dataprocessor_buffers_get_next(sample_type_t **pBuffer) {

	static bool s_is_new_sequence = false;

	*pBuffer = NULL;

	// If we are not in concurrent_mode mode: do nothing until we are paused:
	bool gated_recording = settings_get()->gated_recording;
	if (gated_recording && !s_is_gated) {
		return false;
	}

	int32_t unwrapped_buffer_index = 0;
	// Is there anything in the buffer ready to read?
	while (buffer_fifo_sniff(&unwrapped_buffer_index)) {

		if (unwrapped_buffer_index == BUFFERFIFO_END_SEQUENCE) {
			buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value.
			s_is_new_sequence = false;
			return true;					// Signal that the sequence is ending.
		}

		if (unwrapped_buffer_index == BUFFERFIFO_START_SEQUENCE) {
			buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value.
			s_is_new_sequence = true;
			continue; 	// loop round again to see if there is any actual data ready.
		}

		// Sanity: if the unwrapped_buffer_index refers to expired data, discard it and try again.
		// + 1 to exclude the buffer that is currently being written to.
		if (unwrapped_buffer_index < s_unwrapped_filled_buffer_counter - NUM_BUFFERS + 1) {
			buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value to discard it.
			continue;
		}

		// Sanity: if the buffer_count is in the future, discard it and try again:
		if (unwrapped_buffer_index >= s_unwrapped_filled_buffer_counter) {
			buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value to discard it.
			continue;
		}

		/*
		 * Defer yielding the value until the write index is nearly catching up with
		 * the read index. This has the effect of lazy writing to SD card, which we want
		 * to defer SD access noise.
		 */

		// Figure out the buffer index corresponding to the wrapped buffer index:
		int32_t read_buffer_index = (unwrapped_buffer_index - s_unwrapped_filled_buffer_counter) + (s_active_buffer_index - 1);
		if (read_buffer_index < 0)
			read_buffer_index += NUM_BUFFERS;
		if (read_buffer_index >= NUM_BUFFERS)
			read_buffer_index -= NUM_BUFFERS;

		// Calculate the distance by which reading is leading writing in the buffer:
		const uint32_t write_buffer_index = s_active_buffer_index;
		const uint32_t lead = read_buffer_index > write_buffer_index ?
			read_buffer_index - write_buffer_index : read_buffer_index + NUM_BUFFERS - write_buffer_index;

		if (gated_recording) {
			s_is_new_sequence = false;
			buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value for the caller.
			*pBuffer = (sample_type_t *) &s_buffers[read_buffer_index];
			return false;
		}
		else {
			// If this is a new trigger, stall at this point until write buffer index is catching up with
			// the read buffer index. That means that on new triggers, we defer writing to SD, but once
			// we have started writing data, we continue.
			if ((!s_is_new_sequence) || (lead < MAXIMUM_READ_LEAD)) {
				s_is_new_sequence = false;
				buffer_fifo_get(&unwrapped_buffer_index);	// Consume the value for the caller.
				*pBuffer = (sample_type_t *) &s_buffers[read_buffer_index];
				return false;
			}
			else {
				// Nothing ready yet.
				return false;
			}
		}
	}

	return false;
}

static void data_processor_buffers_on_trigger(int main_tick_count) {

	const int tick_delta = 10;

	if (s_is_gated || (main_tick_count < s_gate_released_ticks + tick_delta)) {
		// Ignore triggers while we are writing to SD card, in case they are self
		// triggers from SD card generated ultrasound. Also for a short period afterwards.
		return;
	}

	s_trigger_count++;

#if BLINK_LEDS
	leds_blink(LEDS_YELLOW);
#endif

	if (s_is_triggered) {

		/*
		 * We are currently triggered, so this is a retrigger. We need to recalculate the
		 * last unwrapped buffer count.
		 */

		s_final_unwrapped_buffer_for_trigger =
				s_unwrapped_filled_buffer_counter + s_buffers_per_second * settings_get()->min_sampling_time_s;
	}
	else {

		/*
		 * This is a new trigger. Calculate the first and last buffer number defining the data range
		 * that we need to write to file. The range may be extended if there is a retrigger.
		 */

		// Note the current buffer number when we received the trigger:
		s_trigger_unwrapped_buffer_count = s_unwrapped_filled_buffer_counter;

		// How much history is available that we can use for the pretrigger?
		uint32_t unexpired_buffers_available = MIN(NUM_BUFFERS - BUFFER_DELTA, s_unwrapped_filled_buffer_counter);
		uint32_t pretrigger_buffer_count = MIN(s_buffers_per_second * settings_get()->pretrigger_time_s, unexpired_buffers_available);

		// Calculate the start and end unwrapped buffer count for this trigger. Note that it can be extended
		// later by a retrigger.
		uint32_t initial_buffer_count = s_unwrapped_filled_buffer_counter - pretrigger_buffer_count;
		uint32_t final_buffer_count = s_unwrapped_filled_buffer_counter + s_buffers_per_second * settings_get()->min_sampling_time_s;

		// Signal that this is the start of a triggered sequence:
		buffer_fifo_put(BUFFERFIFO_START_SEQUENCE);

		// Submit index for the the buffers we already have ie the pretrigger range to the fifo:
		for (uint32_t i = initial_buffer_count; i < s_unwrapped_filled_buffer_counter; i++) {
			buffer_fifo_put(i);
		}

		// Set ourselves up to continue pushing live data buffers to the fifo as they arrive:
		s_final_unwrapped_buffer_for_trigger = final_buffer_count;
		s_is_triggered = true;
	}
}
