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

#ifndef INC_SD_LOWLEVEL_H_
#define INC_SD_LOWLEVEL_H_

#include <stdbool.h>
#include <arm_math.h>
#include "storage.h"

void sd_lowlevel_init(void);
void sd_lowlevel_main_processing(int);
void sd_lowlevel_main_fast_processing(int);

// Relating to TinyUSB, lower level alternatives to the similarly named functions in storage.h:
bool sd_lowlevel_capacity(uint32_t* block_count, uint16_t* block_size);
int32_t sd_lowlevel_read_blocks(uint32_t block_num, uint32_t offset, void* buffer, uint32_t bufsize);
int32_t sd_lowlevel_write_blocks(uint32_t block_num, uint32_t offset, void* buffer, uint32_t bufsize);
bool sd_lowlevel_open(storage_write_type_t write_type);
void sd_lowlevel_close(void);
bool sd_lowlevel_get_debounced_sd_present(void);

int32_t sd_lowlevel_read_blocks_async_start(uint32_t first_block_num, uint32_t byte_offset, void *buffer, uint32_t requested_byte_count);
int32_t sd_lowlevel_read_blocks_async_poll(void);
int32_t sd_lowlevel_write_blocks_async_start(uint32_t first_block_num, uint32_t byte_offset, void *buffer, uint32_t requested_byte_count);
int32_t sd_lowlevel_write_blocks_async_poll(void);

// Relating to TinyUSB:
typedef enum  { LUN_SD_STORAGE = 0 } lun_t;


#endif /* INC_SD_LOWLEVEL_H_ */
