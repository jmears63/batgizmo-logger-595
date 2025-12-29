/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/* --------------------------------------------------------------------------
 * Additional modifications and custom code:
 *
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
 * -------------------------------------------------------------------------- */

#include <stdio.h>

#include "tusb.h"
#include "main.h"
#include "settings.h"

#define USB_VID   0x1209		// Vendor id.
#define USB_BCD   0x0100		// USB version 1.0.	This is not the speed.
#define DEVICE_VERSION 0x104	// Device release version, we decide how it is used.

// String Descriptor Index
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_UNUSED,
  STRID_MSC_IF,
  STRID_UAC1_IF,
};

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = 0,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = 0x077C,				// Different from batgizmo < 1.4.
    .bcdDevice          = DEVICE_VERSION,

    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

// Interface numbers (distinct from endpoint addresses below):
enum
{
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING,
	// JM TODO add MTP here.
  ITF_NUM_TOTAL
};

// JM TODO: add in the length of the MTP config eventually:
#define NUM_SAMPLING_FREQUENCIES 1
#define CONFIG_UAC1_TOTAL_LEN    	(TUD_CONFIG_DESC_LEN + TUD_AUDIO10_MIC_ONE_CH_DESC_LEN(NUM_SAMPLING_FREQUENCIES))

#define EPNUM_AUDIO       0x01

uint8_t const desc_uac1_configuration[] = {
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_UAC1_TOTAL_LEN, 0x00, 100),

  // Interface number, string index, EP Out & EP In address, EP size
  TUD_AUDIO10_MIC_ONE_CH_DESCRIPTOR(
		  /*_itfnum*/ ITF_NUM_AUDIO_CONTROL,
		  /*_stridx*/ 0,
		  /*_nBytesPerSample*/ CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
		  /*_nBitsUsedPerSample*/ CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX*8,
		  /*_epin*/ 0x80 | EPNUM_AUDIO,
		  /*_epsize*/ CFG_TUD_AUDIO_EP_SZ_IN,	// JM TODO: needs to be different for HS.
		  SAMPLING_RATE)
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
// JM TODO: when we add high speed, will that require a different descriptor?
  return desc_uac1_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+


// array of pointer to string descriptors
static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
    "Gimell",                      // 1: Manufacturer
    "BatGizmo Logger",             // 2: Product
    NULL,                          // 3: Serials will use unique ID if possible
    NULL,                          // 4: not used
    "Storage",                	   // 5: MSC Interface
    "Microphone",                  // 6: Audio Interface
};

static uint16_t _desc_str[32 + 1];

static size_t copy_desc_string(const char *str)
{
	// Cap at max char
	size_t chr_count = strlen(str);
	const size_t max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;

	if (chr_count > max_count)
		chr_count = max_count;

	// Convert ASCII string into UTF-16
	for (size_t i = 0; i < chr_count; i++) {
		_desc_str[1 + i] = (uint16_t)str[i];
	}
	return chr_count;
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  switch ( index ) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
    	// Get the MCU's unique id:
		uint32_t uid0 = HAL_GetUIDw0();
		uint32_t uid1 = HAL_GetUIDw1();
		uint32_t uid2 = HAL_GetUIDw2();
		char uidStr[sizeof(_desc_str) - 1];
		snprintf(uidStr, sizeof(uidStr), "%08lX-%08lX-%08lX", uid0, uid1, uid2);
	    chr_count = copy_desc_string(uidStr);
      break;

    default:
      // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
      // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

      if ( !(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) ) {
        return NULL;
      }

      const char *str = string_desc_arr[index];

      // Cap at max char
      chr_count = copy_desc_string(str);
      break;
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}

#if 0

// full speed configuration
uint8_t const desc_fs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

#if 0	// JM TODO
  // Interface number, string index, EP Out & EP In address, EP size
	TUD_UACv1_MIC_ONE_CH_DESCRIPTOR(
		  /*_nBytesPerSample*/ CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX,
		  /*_nBitsUsedPerSample*/ CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX*8,
		  /*_epin*/ 0x80 | EPNUM_AUDIO,
		  /*_epsize*/ CFG_TUD_UACv1_EP_SZ_IN),
#endif
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index; // for multiple configurations

#if TUD_OPT_HIGH_SPEED
  // Although we are highspeed, host may be fullspeed.
  return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
  return desc_fs_configuration;
#endif
}

#endif // 0
