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
#include <data_processor_buffers.h>
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

#define NUM_BUFFERS 74		// Maximize this to maximize our ability to pretrigger. Must be > 2.
							// 6 for STM32U535, up to 76 for STMU595.
							// 70 x 16K => 1146880 samples, @384kHz => 2.99s
							// 12 is about 0.5s.

#define BUFFERS_PER_SECOND (SAMPLING_RATE / DATA_BUFFER_ENTRIES)

#define BUFFER_DELTA 4		// The number of buffers margin we allow in calculations to avoid risk
							// of reading from a buffer that is being overwritten.
							// Must be less than NUM_BUFFERS.

#if NUM_BUFFERS <= BUFFER_DELTA
#error BUFFER_DELTA must be less than NUM_BUFFERS
#endif

#define MAXIMUM_READ_LEAD 24	// We defer yielding values to consumers of the FIFO to read until they are
								// close to be overwritten by writes.
								// This value approximates to 1s, allowing time for the FileX/SD
								// to reopen the data file without data expiring.

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
static volatile size_t s_buffer_fifo_next_read = 0, s_buffer_fifo_next_write = 0, s_buffer_fifo_count = 0;

#if 0
/**
 *  The FIFO of sequence metadata corresponding to the sequences above.
 */
#define METADATA_FIFO_LENGTH (20)
static int32_t s_metadata_fifo[METADATA_FIFO_LENGTH];
static size_t s_metadata_fifo_next_read = 0, s_metadata_fifo_next_write = 0, s_metadata_fifo_count = 0;
#endif

static bool s_is_triggered = false;
static int32_t s_trigger_unwrapped_buffer_count = 0;	// The buffer count at the moment of being triggered.
static int32_t s_final_unwrapped_buffer_for_trigger = 0;		// While we are triggered, continue writing buffers up to this value.
static data_processor_mode_t s_mode = DATA_PROCESSOR_TRIGGERED;

static void data_processor_buffers_on_trigger(void);

void data_processor_buffers_init(void)
{
	data_processor_buffers_reset(DATA_PROCESSOR_TRIGGERED);
}

void data_processor_buffers_reset(data_processor_mode_t mode)
{
	s_mode = mode;
	s_active_buffer_index = 0;
	// s_ready_buffer_count = 0;
	s_active_buffer_entry_count = 0;
	s_active_buffer_ptr = &s_buffers[s_active_buffer_index][0];

	s_unwrapped_filled_buffer_counter = 0;
	s_buffer_fifo_next_read = s_buffer_fifo_next_write = s_buffer_fifo_count = 0;
#if 0
	s_metadata_fifo_next_read = s_metadata_fifo_next_write = s_metadata_fifo_count = 0;
#endif
	s_is_triggered = false;
	s_trigger_unwrapped_buffer_count = s_final_unwrapped_buffer_for_trigger = 0;

	// No need to initialize_buffers to zero as .bss data is zeroed on startup.
	// And in any case, we will never read from a buffer before it has been
	// populated.
}

void data_processor_buffers_fast_main_processing(int main_tick_count)
{
	if (g_trigger_triggered) {
		g_trigger_triggered = false;	// Consume the trigger flag.
		data_processor_buffers_on_trigger();
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
void data_processor_buffers(const sample_type_t *pDMABuffer, int dma_buffer_offset)
{
	// TODO consider replacing the following with CMSIS vector operations, or writing our own composite one.

	// Try to append the data to the currently writing buffer. We might need to copy data in two chunks
	// if the buffer fills up.

	// We could improve this to avoid the extra intermediate buffer. Rainy day stuff.

	int samples_remaining = HALF_SAMPLES_PER_FRAME;
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
				if (s_unwrapped_filled_buffer_counter <= s_final_unwrapped_buffer_for_trigger) {
					// Continue pushing buffers to the fifo as long as we are in triggered state:
					buffer_fifo_put(s_unwrapped_filled_buffer_counter);
				}
				else {
					// We've reached the end of the buffer range to write to file,
					// so exit from triggered state:
					s_is_triggered = false;
					// Signal that this is the end of a triggered sequence:
					buffer_fifo_put(BUFFERFIFO_END_SEQUENCE);
				}
			}
		}
		else if (s_mode == DATA_PROCESSOR_CONTINUOUS) {
			// In continuous mode populate the fifo regardless of triggering.
			buffer_fifo_put(s_unwrapped_filled_buffer_counter);
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
 * Call this to get the next buffer to be written to file, if any.
 * The return value is true if we should close the current file.
 * *buffer is set to NULL if no data is available.
 */
bool dataprocessor_buffers_get_next(sample_type_t **pBuffer) {

	/*
	 * Sniff the next buffer count.
	 * If it is a special value, consume and return it.
	 * If it is within N of the write index catching it up, consume and return it.
	 * It is is expired already, consume it, discard it, try again.
	 * Otherwise, buffer is set to NULL.
	 */

	static bool s_is_new_sequence = false;

	*pBuffer = NULL;
	int32_t unwrapped_buffer_index = 0;
	while (buffer_fifo_sniff(&unwrapped_buffer_index)) {

		// There is something in the buffer that can be read, so decide what to do:

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

		// Sanity: if the buffer_count is expired, discard it and try again.
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

	return false;
}

static void data_processor_buffers_on_trigger(void) {

#if BLINK_LEDS
	leds_blink(LEDS_YELLOW);
#endif

	if (s_is_triggered) {

		/*
		 * We are currently triggered, so this is a retrigger. We need to recalculate the
		 * last buffer count.
		 */

		s_final_unwrapped_buffer_for_trigger = s_unwrapped_filled_buffer_counter + BUFFERS_PER_SECOND * settings_get()->min_sampling_time_s;
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
		uint32_t pretrigger_buffer_count = MIN(BUFFERS_PER_SECOND * settings_get()->pretrigger_time_s, unexpired_buffers_available);

		// Calculate the start and end unwrapped buffer count for this trigger. Note that it can be extended
		// later by a retrigger.
		uint32_t initial_buffer_count = s_unwrapped_filled_buffer_counter - pretrigger_buffer_count;
		uint32_t final_buffer_count = s_unwrapped_filled_buffer_counter + BUFFERS_PER_SECOND * settings_get()->min_sampling_time_s;

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
