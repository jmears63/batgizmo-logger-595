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

#ifndef MY_DATA_PROCESSOR_BUFFERS_H
#define MY_DATA_PROCESSOR_BUFFERS_H

#include <data_acquisition.h>

typedef enum {
	DATA_PROCESSOR_TRIGGERED,
	DATA_PROCESSOR_CONTINUOUS
} data_processor_mode_t;

void data_processor_buffers_init(void);
void data_processor_buffers_reset(data_processor_mode_t mode);
void data_processor_buffers_fast_main_processing(int main_tick_count);
void data_processor_buffers(const sample_type_t *, int buffer_offset);

// We will write to SD in exact chunks of 32 KB, intened to align with blocks and sectors in the SD card
// file system, and should therefore be efficient to write:
#define DATA_BUFFER_ENTRIES (32768 / sizeof(sample_type_t))

bool dataprocessor_buffers_get_next(sample_type_t **buffer);

#endif // MY_DATA_PROCESSOR_BUFFERS_H
