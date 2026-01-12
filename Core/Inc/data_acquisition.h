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

#ifndef MY_DATA_ACQUISITION_H
#define MY_DATA_ACQUISITION_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "settings.h"

typedef uint16_t dma_buffer_type_t;
typedef int16_t sample_type_t;
typedef void(*data_processor_t)(const sample_type_t *, int buffer_offset, int count);


extern RAM_DATA_SECTION dma_buffer_type_t g_dmabuffer1[] __ALIGNED(32);
// extern SRAM4_DATA_SECTION dma_buffer_type_t dmabuffer4[] __ALIGNED(32);

void data_acquisition_init(void);
void data_acquisition_main_processing(void);
void data_acquisition_reset(int samples_per_frame);
int data_acquisition_get_conv_counter(void);
void data_acquisition_set_signal_offset_correction(int offset);
void data_acquisition_enable_capture(bool flag);
void data_acquisition_set_processor(data_processor_t processor);


#define MONITOR_OFFSET 0x2000
#define MONITOR_LEFTSHIFT 2

#define ACQUISITION_OFFSET 0x8000
#define ACQUISITION_LEFTSHIFT 0

// The following is defined by CMSIS.
// #define MIN(a, b)  (((a) < (b)) ? (a) : (b))
// #define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define RANGE_CLIP(lower, x, upper) MAX(MIN(x, upper), lower)

// Communication between modules:
extern volatile sample_type_t *g_raw_half_frame;
extern volatile int g_raw_half_frame_size;
extern volatile int g_raw_half_frame_counter;
extern volatile bool g_raw_half_frame_ready;


#endif // MY_DATA_ACQUISITION_H
