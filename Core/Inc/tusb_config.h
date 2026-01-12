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

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "main.h"		// For SAMPLES_PER_FRAME

#ifdef __cplusplus
 extern "C" {
#endif

 void usb_handlers_init();

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// Limit to full speed:
#define CFG_TUD_MAX_SPEED 	  OPT_MODE_FULL_SPEED
// #define CFG_TUD_MAX_SPEED 	  OPT_MODE_HIGH_SPEED

// Suggested by chatgpt:
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
// #define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED


/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

// #define UACv1 1

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_AUDIO             1
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

#define TUD_AUDIO_PREFER_RING_BUFFER 1

// Sample rate kHz x 2 Bytes/Sample x CFG_TUD_UACv1_N_CHANNELS_TX Channels - the Windows driver
// always needs an extra sample per channel of space more, otherwise it complains... found by trial and error
// #define CFG_TUD_UACv1_EP_SZ_IN (SAMPLES_PER_FRAME) * CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX * CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX


//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------

#define USB_SAMPLING_RATE_INDEX 8		// 384
#define USB_SAMPLES_PER_FRAME (SETTINGS_SAMPLING_RATE_MULTIPLIER_KHZ * USB_SAMPLING_RATE_INDEX)
#define USB_SAMPLING_RATE (USB_SAMPLING_RATE_INDEX * SETTINGS_SAMPLING_RATE_MULTIPLIER_KHZ * 1000)
#define USB_HALF_SAMPLES_PER_FRAME (USB_SAMPLES_PER_FRAME >> 1)
#define USB_FRAMES_PERSECOND 1000		// Assume FS USB for now.


#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE (USB_SAMPLES_PER_FRAME * 1000)		// JM Assumption: FS USB.

// JM: Hard coded the fact this there is one channel of 16 bit data:
#define CFG_TUD_AUDIO_ENABLE_EP_IN                    1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX    2         // This value is not required by the driver, it parses this information from the descriptor once the alternate interface is set by the host - we use it for the setup
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX            1         // This value is not required by the driver, it parses this information from the descriptor once the alternate interface is set by the host - we use it for the setup
#define CFG_TUD_AUDIO_EP_SZ_IN                        TUD_AUDIO_EP_SIZE(TUD_OPT_HIGH_SPEED, CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)


#define CFG_TUD_UACv1_FUNC_1_DESC_LEN                                 TUD_UACv1_MIC_X_CH_DESC_LEN

#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL              0

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX             CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ          (TUD_OPT_HIGH_SPEED ? 32 : 4) * CFG_TUD_AUDIO_EP_SZ_IN // Example write FIFO every 1ms, so it should be 8 times larger for HS device

#if 0
#define CFG_TUD_UACv1_ENABLE_EP_IN                                    1
# define CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX                    2         // This value is not required by the driver, it parses this information from the descriptor once the alternate interface is set by the host - we use it for the setup
#if STEREO_MODE
# define CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX                            2 // 4         // This value is not required by the driver, it parses this information from the descriptor once the alternate interface is set by the host - we use it for the setup
#else
# define CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX                            1 // 4         // This value is not required by the driver, it parses this information from the descriptor once the alternate interface is set by the host - we use it for the setup
#endif
// #define CFG_TUD_UACv1_EP_SZ_IN                                        (SAMPLES_PER_FRAME + 1) * CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX * CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX      // Sample rate kHz x 2 Bytes/Sample x CFG_TUD_UACv1_N_CHANNELS_TX Channels - the Windows driver always needs an extra sample per channel of space more, otherwise it complains... found by trial and error
// JM: Important: we mustn't send more data than expected per frame, even if it is available, as this
 // confuses the ioctl method of reading the isochronous stream in the Android app:
#define CFG_TUD_UACv1_EP_SZ_IN                                        (SAMPLES_PER_FRAME) * CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX * CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX      // Sample rate kHz x 2 Bytes/Sample x CFG_TUD_UACv1_N_CHANNELS_TX Channels - the Windows driver always needs an extra sample per channel of space more, otherwise it complains... found by trial and error
#define CFG_TUD_UACv1_FUNC_1_EP_IN_SZ_MAX                             CFG_TUD_UACv1_EP_SZ_IN
//#define CFG_TUD_UACv1_FUNC_1_EP_IN_SW_BUF_SZ                          CFG_TUD_UACv1_EP_SZ_IN
// JM we use this FIFO to buffer between variable rate data acquisition and USB so increase its length
// to minimize the risk of over/underflow:
#define CFG_TUD_UACv1_FUNC_1_EP_IN_SW_BUF_SZ                          (CFG_TUD_UACv1_EP_SZ_IN * 4)

#define CFG_TUD_UACv1_ENABLE_ENCODING                                 0	// 1 JM
#define CFG_TUD_UACv1_ENABLE_TYPE_I_ENCODING                          0 // 1 JM
#if STEREO_MODE
# define CFG_TUD_UACv1_FUNC_1_CHANNEL_PER_FIFO_TX                      2 //2         // One I2S stream contains two channels, each stream is saved within one support FIFO - this value is currently fixed, the driver does not support a changing value
#else
# define CFG_TUD_UACv1_FUNC_1_CHANNEL_PER_FIFO_TX                      1 //2         // One I2S stream contains two channels, each stream is saved within one support FIFO - this value is currently fixed, the driver does not support a changing value
#endif
#define CFG_TUD_UACv1_FUNC_1_N_TX_SUPP_SW_FIFO                        (CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX / CFG_TUD_UACv1_FUNC_1_CHANNEL_PER_FIFO_TX)
#define CFG_TUD_UACv1_FUNC_1_TX_SUPP_SW_FIFO_SZ                       (CFG_TUD_UACv1_EP_SZ_IN / CFG_TUD_UACv1_FUNC_1_N_TX_SUPP_SW_FIFO)

#endif // 0


#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
