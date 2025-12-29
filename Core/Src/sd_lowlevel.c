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

#include <stdio.h>
#include <stdbool.h>
#include <memory.h>

#include "my_sdmmc.h"
#include "gpio.h"
#include "stm32u5xx_hal_sd.h"		// For BLOCKSIZE.
#include "sdmmc.h"
#include "sd_lowlevel.h"

// Support for logic for debouncing SD card presence detection:
static bool s_debounced_sd_present = false;
static int s_sd_present_count = 0;
#define DEBOUNCE_COUNT 20

// Track whether the SD is currently open:
static bool s_opened = false;

// Cached values relating to the SD card:
static uint32_t s_block_count = 0;
static uint16_t s_block_size = 0;


void sd_lowlevel_init(void)
{
	bool sd_present = HAL_GPIO_ReadPin(GPIO_SD_DETECT_GPIO_Port, GPIO_SD_DETECT_Pin) == GPIO_PIN_RESET;
	s_debounced_sd_present = sd_present;		// Initialise to the current state.
	s_sd_present_count = 0;
	s_opened = false;
	s_block_count = 0;
	s_block_size = 0;
}

/**
 * Provide a debounced opinion about whether the SD card is present.
 * We acknowledge absence immediately, but require presence for a period
 * of time to allow for contact bounce and SD card startup.
 */
static void do_sd_present(void)
{
	bool sd_present = HAL_GPIO_ReadPin(GPIO_SD_DETECT_GPIO_Port, GPIO_SD_DETECT_Pin) == GPIO_PIN_RESET;

	if (s_debounced_sd_present != sd_present) {
		// Something changed.

		if (sd_present)
		{
			// The SD card seems to have been inserted. We need it to stay that way for a little
			// while to debounce it and allow the SD card itself to start up:
			if (s_sd_present_count >= DEBOUNCE_COUNT) {
				s_debounced_sd_present = true;
				s_sd_present_count = 0;
			}
			else
				s_sd_present_count++;
		}
		else
		{
			// Respond immediately to the SD card being missing:
			s_debounced_sd_present = false;
			s_sd_present_count = 0;
		}

	}
	else
		s_sd_present_count = 0;
}

bool sd_lowlevel_get_debounced_sd_present(void)
{
	return s_debounced_sd_present;
}

void sd_lowlevel_main_processing(int)
{
	do_sd_present();
}


bool sd_lowlevel_capacity(uint32_t* block_count, uint16_t* block_size)
{
	if (s_opened) {
		HAL_SD_CardInfoTypeDef cardInfo;

		HAL_StatusTypeDef status = HAL_SD_GetCardInfo(&hsd1, &cardInfo);
		if (status == HAL_OK) {
			*block_size = s_block_size = cardInfo.BlockSize;
			*block_count = s_block_count = cardInfo.BlockNbr;

			return true;
		}
	}

	return false;
}

int32_t sd_lowlevel_read_blocks(uint32_t first_block_num, uint32_t byte_offset, void *buffer, uint32_t requested_byte_count)
{
	if (!s_opened)
		return -1;
	if (byte_offset > BLOCKSIZE)
	   return -1;   // Silly value for the offset.
	if (byte_offset != 0)
		return -1;	// Not sure if this can happen - writing a partial block I guess? Not supported at present.

	// requested_count tells us how much data is required. The supplied buffer can be assumed
	// to be large enough.
	// block_num is the starting block number.

	// Calculate how many blocks we need, rounding up:
	uint32_t blocks_required = (requested_byte_count + byte_offset + BLOCKSIZE - 1) / BLOCKSIZE;

	while (hsd1.State == HAL_SD_STATE_BUSY)
		;
	// Note: the following call starts data transfer via DMA, but doesn't wait for it to complete.
	// A successful return code only signifies that we succeeded in *starting* transfer.
	HAL_StatusTypeDef status = HAL_SD_ReadBlocks_DMA(&hsd1, buffer, first_block_num, blocks_required);
	while (hsd1.State == HAL_SD_STATE_BUSY)
		;

	if (status != HAL_OK) {
		// MY_BREAKPOINT();
		return -1;
	}

	return requested_byte_count;
}

// #pragma GCC push_options
// #pragma GCC optimize("O0")

typedef struct {
	uint32_t transfer_byte_count;
	bool in_progress;
} async_read_state_t;
static async_read_state_t s_read_state = { transfer_byte_count: 0, in_progress: false };

int32_t sd_lowlevel_read_blocks_async_start(uint32_t first_block_num, uint32_t byte_offset,
		void *buffer, uint32_t transfer_byte_count)
{
	if (!s_opened)
		return -1;
	if (byte_offset > BLOCKSIZE)
		return -1;   // Silly value for the offset.
	if (byte_offset != 0)
		return -1;	// Not sure if this can happen - writing a partial block I guess? Not supported at present.
	if ((transfer_byte_count & 0x1FF) != 0)
		return -1;   // Must be a multiple of BLOCKSIZE (fixed at 512 at the moment).

	// Sanity check: this shouldn't happen. One transaction should finish before the next
	// one starts in USB.
	if (s_read_state.in_progress == true) {
		// MY_BREAKPOINT();
	}

	// requested_count tells us how much data is required. The supplied buffer can be assumed
	// to be large enough.
	// block_num is the starting block number.

	// Calculate how many blocks we need, rounding up:
	uint32_t blocks_required = (transfer_byte_count + byte_offset + BLOCKSIZE - 1) / BLOCKSIZE;

	// while (hsd1.State == HAL_SD_STATE_BUSY)
	//	;

	// Note: the following call starts data transfer via DMA, but doesn't wait for it to complete.
	// A successful return code only signifies that we succeeded in *starting* transfer.
	s_read_state.transfer_byte_count = transfer_byte_count;
	s_read_state.in_progress = true;
	HAL_StatusTypeDef status = HAL_SD_ReadBlocks_DMA(&hsd1, buffer, first_block_num, blocks_required);
	if (status != HAL_OK) {
		// MY_BREAKPOINT();
		return -1;
	}

	return 0;		// Results in a USB NAK and retry.
}

int32_t sd_lowlevel_read_blocks_async_poll(void)
{
	if (hsd1.State == HAL_SD_STATE_BUSY)
		return 0;	// Results in a USB NAK and retry.

	if (hsd1.State == HAL_SD_STATE_ERROR)
		return -1;	// Results in a USB stall and abort.

	// The transfer is complete:
	s_read_state.in_progress = false;
	return s_read_state.transfer_byte_count;
}

typedef struct {
	uint32_t transfer_byte_count;
	const uint8_t *pBuffer;
	uint32_t blocks_required;
	uint32_t start_block;
	uint32_t block_count;
	int32_t transfer_result;
	bool in_progress;
} async_write_state_t;

static async_write_state_t s_write_state;

int32_t sd_lowlevel_write_blocks_async_start(uint32_t first_block_num, uint32_t byte_offset,
		void *buffer, uint32_t transfer_byte_count)
{
	if (!s_opened)
		return -1;
	if (byte_offset > BLOCKSIZE)
		return -1;   // Silly value for the offset.
	if (byte_offset != 0)
		return -1;	// Not sure if this can happen - writing a partial block I guess? Not supported at present.
	if ((transfer_byte_count & 0x1FF) != 0)
		return -1;   // Must be a multiple of BLOCKSIZE (fixed at 512 at the moment).

	// Sanity check: this shouldn't happen. One transaction should finish before the next
	// one starts in USB.
	if (s_write_state.in_progress == true) {
		MY_BREAKPOINT();
	}

	// requested_count tells us how much data is required. The supplied buffer can be assumed
	// to be large enough.
	// block_num is the starting block number.

	// Calculate how many blocks we need, rounding up:
	uint32_t blocks_required = (transfer_byte_count + byte_offset + BLOCKSIZE - 1) / BLOCKSIZE;

	s_write_state.blocks_required = blocks_required;
	s_write_state.block_count = 0;
	s_write_state.start_block = first_block_num;
	s_write_state.pBuffer = (uint8_t*) buffer;
	s_write_state.transfer_byte_count = transfer_byte_count;
	s_write_state.transfer_result = 0;
	s_write_state.in_progress = true;

	// TODO: For now we write one block at a time, which is not ideal. Writing multiple blocks results in a HAL error. No idea.

	// Note: the following call starts data transfer via DMA, but doesn't wait for it to complete.
	// A successful return code only signifies that we succeeded in *starting* transfer.
	HAL_StatusTypeDef status = HAL_SD_WriteBlocks_DMA(&hsd1, s_write_state.pBuffer, s_write_state.start_block + s_write_state.block_count, 1);
	s_write_state.pBuffer += BLOCKSIZE;
	s_write_state.block_count++;

	if (status != HAL_OK) {
		// MY_BREAKPOINT();
		return -1;
	}

	return 0;		// Results in a USB NAK and retry.
}

static void sd_lowlevel_write_blocks_async_advance(void)
{
	if (s_write_state.in_progress) {
		// Transfer is not complete, see what we need to do next.

		if (hsd1.State == HAL_SD_STATE_ERROR) {
			s_write_state.transfer_result = -1;	// Transfer failed; results in a stall.
			s_write_state.in_progress = false;
		}
		else if (hsd1.State != HAL_SD_STATE_BUSY) {
			// Block transfer in progress has finished.
			if (s_write_state.block_count == s_write_state.blocks_required) {
				// We've transferred all the blocks.
				s_write_state.transfer_result = s_write_state.transfer_byte_count;	// Assumption: whole number of blocks.
				s_write_state.in_progress = false;
			}
			else {
				// Transfer the next block.

				HAL_StatusTypeDef status = HAL_SD_WriteBlocks_DMA(&hsd1, s_write_state.pBuffer, s_write_state.start_block + s_write_state.block_count, 1);
				s_write_state.pBuffer += BLOCKSIZE;
				s_write_state.block_count++;
				if (status != HAL_OK) {
					// MY_BREAKPOINT();
					s_write_state.transfer_result = -1;
				}
			}
		}
	}
}

int32_t sd_lowlevel_write_blocks_async_poll(void)
{
	sd_lowlevel_write_blocks_async_advance();

	return s_write_state.transfer_result;
}

// #pragma GCC pop_options

#if 1
int32_t sd_lowlevel_write_blocks(uint32_t block_num, uint32_t offset, void* buffer, uint32_t bytes_to_write)
{
	if (!s_opened)
		return -1;
	if (offset != 0)
		return -1;	// Not sure if this can happen - writing a partial block I guess? Not supported at present.
	if (offset > BLOCKSIZE)
		return -1;   // Silly value for offset.
	if ((bytes_to_write & 0x1FF) != 0)
		return -1;   // Must be a multiple of BLOCKSIZE (fixed at 512 at the moment).

	// requested_count tells us how much data we need to write from the buffer supplied.
	// block_num is the starting block number.

	uint32_t total_written = 0;

	// Keep writing blocks until we have written the buffer supplied:
	while (total_written < bytes_to_write) {
		if (block_num > s_block_count)
			return -1;

		// For now we write one block at a time:
		while (hsd1.State == HAL_SD_STATE_BUSY)
			;
		if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)buffer, block_num, 1) != HAL_OK)
		{
			// MY_BREAKPOINT();
			return -1;
		}
		while (hsd1.State == HAL_SD_STATE_BUSY)
			;

		total_written += BLOCKSIZE;
		// requested_count tells us how much data we need to write from the buffer supplied.
		// block_num is the starting block number.

		block_num++;
	}

	return total_written;
}
#else

/*
 * The following code results in intermittent HAL_ERROR/CRC failure errors, whereas the older code
 * that loops and sends a block at a time doesn't. Perhaps it is this issue:
 * 	 https://community.st.com/t5/stm32-mcus-embedded-software/stm32l4xx-sd-hal-dma-isr-race-condition-causes-failed-reads/td-p/279285
 */
int32_t sd_lowlevel_write_blocks(uint32_t first_block_num, uint32_t byte_offset, void* buffer, uint32_t bytes_to_write)
{
	if (!s_opened)
		return -1;
	if (byte_offset != 0)
		return -1;	// Not sure if this can happen - writing a partial block I guess? Not supported at present.
	if (byte_offset > BLOCKSIZE)
		return -1;   // Silly value for offset.
	if ((bytes_to_write & 0x1FF) != 0)
		return -1;   // Must be a multiple of BLOCKSIZE (fixed at 512 at the moment).

	// Calculate how many blocks we need, rounding up:
	uint32_t blocks_to_write = (bytes_to_write + byte_offset + BLOCKSIZE - 1) / BLOCKSIZE;

	while (hsd1.State == HAL_SD_STATE_BUSY)
		;
	// Note: the following call starts data transfer via DMA, but doesn't wait for it to complete.
	// A successful return code only signifies that we succeeded in *starting* transfer.
	HAL_StatusTypeDef status = HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)buffer, first_block_num, blocks_to_write);
	if (status != HAL_OK)
	{
		// MY_BREAKPOINT();
		return -1;
	}
	while (hsd1.State == HAL_SD_STATE_BUSY)
		;

	return bytes_to_write;
}

#endif

static void apply_sd_power(bool powered)
{
	if (powered) {
		HAL_GPIO_WritePin(SD_Power_Enable_GPIO_Port, SD_Power_Enable_Pin, GPIO_PIN_SET);
		// Arbitrary time for the SD to power up:
		HAL_Delay(100);
	}
	else {
		HAL_GPIO_WritePin(SD_Power_Enable_GPIO_Port, SD_Power_Enable_Pin, GPIO_PIN_RESET);
	}
}

bool sd_lowlevel_open(storage_write_type_t write_type)
{
	apply_sd_power(true);
	s_opened = false;

	// Needed for hardware version 1.1, does no harm with other versions:
	HAL_GPIO_WritePin(DAT0_PULLUP_GPIO_Port, DAT0_PULLUP_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(CMD_PULLUP_GPIO_Port, CMD_PULLUP_Pin, GPIO_PIN_SET);

	// Is the SD card is inserted (GPIO_PIN_RESET if present)?
	GPIO_PinState sd_present = HAL_GPIO_ReadPin(GPIO_SD_DETECT_GPIO_Port, GPIO_SD_DETECT_Pin);
	if (sd_present == GPIO_PIN_RESET) {
		My_SDMMC1_SD_Init(write_type);
		if (hsd1.ErrorCode == HAL_SD_ERROR_NONE) {
			s_opened = true;
			return true;
		}
	}

	return false;
}

void sd_lowlevel_close(void)
{
	if (hsd1.Instance)
		HAL_SD_DeInit(&hsd1);

	apply_sd_power(false);

	// Needed for hardware version 1.1, does no harm with other versions:
	HAL_GPIO_WritePin(DAT0_PULLUP_GPIO_Port, DAT0_PULLUP_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(CMD_PULLUP_GPIO_Port, CMD_PULLUP_Pin, GPIO_PIN_RESET);

	s_opened = false;
}

void sd_lowlevel_main_fast_processing(int)
{
	sd_lowlevel_write_blocks_async_advance();
}
