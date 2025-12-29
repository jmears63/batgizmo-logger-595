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

#include <sd_lowlevel.h>
#include "bsp/board_api.h"
#include "tusb.h"
#include <stdbool.h>
#include "sdmmc.h"
#include "stm32u5xx_hal_sd.h"		// For BLOCKSIZE.
#include "sd_lowlevel.h"
#include "main.h"

static bool s_is_present = false;

#define USE_SD_DIRECT 1


void msc_disk_sdmmc_set_present(bool is_present)
{
	s_is_present = is_present;
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun;

  const char vid[] = "BatGizmo";
  const char pid[] = "Logger";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void) lun;

  if (!s_is_present) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
  }

  return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
  (void) lun;

  // bool b = false;

  // Defaults in case the capacity command is unsupported:
  *block_count = 0;
  *block_size = BLOCKSIZE;
  if (lun == LUN_SD_STORAGE) {
#if USE_SD_DIRECT == 1
	  sd_lowlevel_capacity(block_count, block_size);
#else
	  /* bool b = */ storage_capacity(block_count, block_size);
#endif
  }
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void) lun;
  (void) power_condition;

  if ( load_eject )
  {
    if (start)
    {
      // This can only be done physically by the user.
    } else
    {
        // This can only be done physically by the user.
    }
  }

  return true;
}

#define ASYNC_MODE 1
#if ASYNC_MODE

#pragma GCC push_options
// #pragma GCC optimize("O0")

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t block_num, uint32_t offset, void* buffer, uint32_t transfer_byte_count)
{
  (void) lun;

  if (!s_is_present) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return -1;
  }
  uint32_t block_count = 0;
  uint16_t block_size = BLOCKSIZE;		// Unused.
  if (!sd_lowlevel_capacity(&block_count, &block_size))
    return -1;

  if (block_num >= block_count)
    return -1;

  typedef enum { async_start, async_pending } async_state_t;
  static async_state_t state = async_start;

  switch (state) {
	case async_start:
	{
      // Kick off the async data transfer:
	  int32_t rc = sd_lowlevel_read_blocks_async_start(block_num, offset, buffer, transfer_byte_count);
	  if (rc == 0)
	    state = async_pending;
	  return rc;
	}
	break;

	case async_pending:
	{
	  // TODO: Sanity check that parameter values match the stored state.

	  // Poll until the transfer is complete:
	  int32_t rc = sd_lowlevel_read_blocks_async_poll();
	  if (rc != 0)
	    state = async_start;
	  return rc;
	}
	break;
  }

  // Shouldn't get here:
  MY_BREAKPOINT();
  return -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t block_num, uint32_t offset, uint8_t* buffer, uint32_t transfer_byte_count)
{
  (void) lun;

  if (!s_is_present) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return -1;
  }

  uint32_t block_count = 0;
  uint16_t block_size = BLOCKSIZE;		// Unused.
  if (!sd_lowlevel_capacity(&block_count, &block_size))
    return -1;

  if (block_num >= block_count)
    return -1;

  typedef enum { async_start, async_pending } async_state_t;
  static async_state_t state = async_start;

  switch (state) {
	case async_start:
	{
      // Kick off the async data transfer:
	  int32_t rc = sd_lowlevel_write_blocks_async_start(block_num, offset, buffer, transfer_byte_count);
	  if (rc == 0)
	    state = async_pending;
	  return rc;
	}
	break;

	case async_pending:
	{
	  // TODO: Sanity check that parameter values match the stored state.

	  // Poll until the transfer is complete:
	  int32_t rc = sd_lowlevel_write_blocks_async_poll();
	  if (rc != 0)
	    state = async_start;
	  return rc;
	}
	break;
  }

  // Shouldn't get here:
  MY_BREAKPOINT();
  return -1;
}

#pragma GCC pop_options

#else

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t block_num, uint32_t offset, void* buffer, uint32_t bufsize)
{
  (void) lun;

  if (!s_is_present) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return -1;
  }
  uint32_t block_count = 0;
  uint16_t block_size = BLOCKSIZE;		// Unused.
  if (!sd_lowlevel_capacity(&block_count, &block_size))
    return -1;

  if (block_num >= block_count)
    return -1;

  return sd_lowlevel_read_blocks(block_num, offset, buffer, bufsize);
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t block_num, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void) lun;

  if (!s_is_present) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return -1;
  }

  uint32_t block_count = 0;
  uint16_t block_size = BLOCKSIZE;		// Unused.
  if (!sd_lowlevel_capacity(&block_count, &block_size))
    return -1;

  if (block_num >= block_count)
    return -1;

  return sd_lowlevel_write_blocks(block_num, offset, buffer, bufsize);
}

#endif

bool tud_msc_is_writable_cb (uint8_t lun)
{
  (void) lun;

  return true;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  // read10 & write10 has their own callback and MUST not be handled here

  void const* response = NULL;
  int32_t resplen = 0;

  // most scsi handled is input
  bool in_xfer = true;

  switch (scsi_cmd[0])
  {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      /* SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL is the Prevent/Allow Medium Removal
      command (1Eh) that requests the library to enable or disable user access to
      the storage media/partition. */
      // ESP_LOGI(TAG, "tud_msc_scsi_cb() invoked: SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL");
      resplen = 0;
      break;

    default:
      // Set Sense = Invalid Command Operation
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

      // negative means error -> tinyusb could stall and/or response with failed status
      resplen = -1;
    break;
  }

  // return resplen must not larger than bufsize
  if ( resplen > bufsize ) resplen = bufsize;

  if ( response && (resplen > 0) )
  {
    if(in_xfer)
    {
      memcpy(buffer, response, (size_t) resplen);
    }else
    {
      // SCSI output
    }
  }

  return (int32_t) resplen;
}
