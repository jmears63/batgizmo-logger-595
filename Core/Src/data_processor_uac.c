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

#include "data_processor_uac.h"
#include "tusb.h"
#include "audio_device.h"

// Define a long buffer we can use to queue samples in:
#define SUPERBUFFERFACTOR 4
#define SUPERBUFFERLEN (SAMPLES_PER_FRAME * SUPERBUFFERFACTOR)

typedef struct {
	sample_type_t buffer[SUPERBUFFERLEN];
	volatile uint16_t next_write_index;					// The next location to copy to in the buffer.
} superbuffer_t;

static superbuffer_t s_sb;

static void sb_reset(superbuffer_t *sb)
{
	for (int i = 0; i < sizeof(s_sb.buffer) / sizeof(uint16_t); i++)
		s_sb.buffer[i] = 0x8000;		// Unsigned integer "zero" value.
	s_sb.next_write_index = 0;
}

void data_processor_uac_init(void)
{
	data_processor_uac_reset();
}

void data_processor_uac_reset(void)
{
	sb_reset(&s_sb);
}

/**
 * This function is called in interrupt context. Its job is to pass the half frame
 * into the FIFO buffer that feeds USB with minimal overhead.
 *
 * The FIFO implementation from tusb looks safe to use from one ISR to another.
 */
void data_processor_uac(const sample_type_t *pDataBuffer, int buffer_offset)
{
	tud_audio_write((const void *) (pDataBuffer + buffer_offset), HALF_SAMPLES_PER_FRAME * sizeof(*pDataBuffer));
}
