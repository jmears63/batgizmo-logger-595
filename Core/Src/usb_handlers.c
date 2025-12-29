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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "leds.h"
#include "gain.h"
#include "data_processor_uac.h"
#include "usb_handlers.h"
#include "device/dcd.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

#define SAMPLE_RATE   (SAMPLES_PER_FRAME * 1000)


// Audio controls
// Current states
static bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1] =       // +1 for master channel 0
{ false, false };
static int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1] = // +1 for master channel 0
{ 24, 24 };

static uint32_t sampFreq;
static uint8_t bytesPerSample;
static uint8_t clkValid;

#if TUD_OPT_HIGH_SPEED

// Range states
// List of supported sample rates
static const uint32_t sampleRatesList[] =
    {
        384000
    };
#endif

#define N_sampleRates TU_ARRAY_SIZE(sampleRatesList)

// Bytes per format of every Alt settings
static const uint8_t bytesPerSampleAltList[2] =
    {
        0,
        2,
};

// Audio test data (first index is the FIFO, seconds index is to the (possibly) interleaved channel(s) in the FIFO.
// FIFO is big enough for 1 ms of data.
// uint16_t i2s_dummy_buffer[CFG_TUD_UACv1_FUNC_1_N_TX_SUPP_SW_FIFO][CFG_TUD_UACv1_FUNC_1_TX_SUPP_SW_FIFO_SZ/2];   // Ensure half word aligned

//uint16_t audio_buffer[CFG_TUD_UACv1_FUNC_1_N_CHANNELS_TX * SAMPLES_PER_FRAME];

static bool s_usb_mounted = false;

void usb_handlers_init(void)
{
  // Initialise USB state.
  sampFreq = SAMPLE_RATE;
  clkValid = 1;

  s_usb_mounted = false;
  /*

  sampleFreqRng.wNumSubRanges = 1;
  sampleFreqRng.subrange[0].bMin = UACv1_SAMPLE_RATE;
  sampleFreqRng.subrange[0].bMax = UACv1_SAMPLE_RATE;
  sampleFreqRng.subrange[0].bRes = 0;

  memset(i2s_dummy_buffer, 0, sizeof(i2s_dummy_buffer));
  */
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

bool usb_handlers_ismounted(void)
{
	return s_usb_mounted;
}

// Invoked when device is mounted, ie after set configuration.
void tud_mount_cb(void)
{
	s_usb_mounted = true;

#if 0
	if (settings_get()->disable_usb_msc) {
		/* A bit of a hack to disable MSC class if required by settings.
		 * We get here because set configuration has just been acted on. We take
		 * this opportunity to disable MSC endpoints if they are not required.
		 */
		dcd_edpt_close(BOARD_TUD_RHPORT, EPNUM_MSC_OUT);
		dcd_edpt_close(BOARD_TUD_RHPORT, EPNUM_MSC_IN);
	}
#endif
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	s_usb_mounted = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
	s_usb_mounted = false;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
	// leds_set_mounted(true);
	s_usb_mounted = true;
}

/**
 * Return true to signal that we support shutting down of the OTG hardware.
 */
bool dcd_deinit(uint8_t rhport)
{
  (void) rhport;

  // Return true so that tusb shuts down the interface cleanly such that
  // we can reinitialized it:
  return true;
}

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// UAC1 Helper Functions
//--------------------------------------------------------------------+

static bool audio10_set_req_ep(tusb_control_request_t const *p_request, uint8_t *pBuff) {
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);

  switch (ctrlSel) {
    case AUDIO10_EP_CTRL_SAMPLING_FREQ:
      if (p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
        // Request uses 3 bytes
        TU_VERIFY(p_request->wLength == 3);

        sampFreq = tu_unaligned_read32(pBuff) & 0x00FFFFFF;

        TU_LOG2("EP set current freq: %" PRIu32 "\r\n", sampFreq);

        // Only allow them to set the correct sampling rate:
        if (sampFreq == SAMPLE_RATE)
        	return true;
      }
      break;

    // Unknown/Unsupported control
    default:
      TU_BREAKPOINT();
      return false;
  }

  return false;
}

static bool audio10_get_req_ep(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);

  switch (ctrlSel) {
    case AUDIO10_EP_CTRL_SAMPLING_FREQ:
      if (p_request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
        TU_LOG2("EP get current freq\r\n");

        uint8_t freq[3];
        freq[0] = (uint8_t) (sampFreq & 0xFF);
        freq[1] = (uint8_t) ((sampFreq >> 8) & 0xFF);
        freq[2] = (uint8_t) ((sampFreq >> 16) & 0xFF);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
      }
      break;

    // Unknown/Unsupported control
    default:
      TU_BREAKPOINT();
      return false;
  }

  return false;
}

static bool audio10_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // If request is for our feature unit (ID defined in usbd.h)
  if (entityID == 0x02) {
    switch (ctrlSel) {
      case AUDIO10_FU_CTRL_MUTE:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_SET_CUR:
            // Only 1st form is supported
            TU_VERIFY(p_request->wLength ==1);

            mute[channelNum] = pBuff[0];

            TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);

            if (mute[channelNum])
            	gain_reenable();
            else
            	gain_disable();

            return true;

          default:
            return false; // not supported
        }

      case AUDIO10_FU_CTRL_VOLUME:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_SET_CUR:
            // Only 1st form is supported
            TU_VERIFY(p_request->wLength == 2);

            int16_t v = (int16_t)tu_unaligned_read16(pBuff) / 256;
            volume[channelNum] = v;

            TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", v, channelNum);

            gain_set_db(v, mute[channelNum]);	// JM

            return true;

          default:
            return false; // not supported
        }

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // If request is for our feature unit (ID defined in usbd.h)
  if (entityID == 0x02) {
    switch (ctrlSel) {
      case AUDIO10_FU_CTRL_MUTE:
        // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
        // There does not exist a range parameter block for mute
        TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute[channelNum], 1);

      case AUDIO10_FU_CTRL_VOLUME:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_GET_CUR:
            TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
            {
              //int16_t vol = (int16_t) volume[channelNum];
            	// JM: get the actual gain, not the most recently requested:
              int16_t vol = gain_get_db();
              vol = vol * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol, sizeof(vol));
            }

          case AUDIO10_CS_REQ_GET_MIN:
            TU_LOG2("    Get Volume min of channel: %u\r\n", channelNum);
            {
              int16_t min = 0; // dB
              min = min * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &min, sizeof(min));
            }

          case AUDIO10_CS_REQ_GET_MAX:
            TU_LOG2("    Get Volume max of channel: %u\r\n", channelNum);
            {
              int16_t max = 24; // dB
              max = max * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &max, sizeof(max));
            }

          case AUDIO10_CS_REQ_GET_RES:
            TU_LOG2("    Get Volume res of channel: %u\r\n", channelNum);
            {
              int16_t res = 6; // dB
              res = res * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &res, sizeof(res));
            }
            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
        break;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

//--------------------------------------------------------------------+
// UAC2 Helper Functions
//--------------------------------------------------------------------+

#if TUD_OPT_HIGH_SPEED

static bool audio20_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == AUDIO20_CS_REQ_CUR);

  // If request is for our feature unit
  if (entityID == UAC2_ENTITY_FEATURE_UNIT) {
    switch (ctrlSel) {
      case AUDIO20_FU_CTRL_MUTE:
        // Request uses format layout 1
        TU_VERIFY(p_request->wLength == sizeof(audio20_control_cur_1_t));

        mute[channelNum] = ((audio20_control_cur_1_t *) pBuff)->bCur;

        TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);
        return true;

      case AUDIO20_FU_CTRL_VOLUME:
        // Request uses format layout 2
        TU_VERIFY(p_request->wLength == sizeof(audio20_control_cur_2_t));

        volume[channelNum] = (int16_t) ((audio20_control_cur_2_t *) pBuff)->bCur;

        TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", volume[channelNum], channelNum);
        return true;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Clock Source unit
  if (entityID == UAC2_ENTITY_CLOCK) {
    switch (ctrlSel) {
      case AUDIO20_CS_CTRL_SAM_FREQ:
        TU_VERIFY(p_request->wLength == sizeof(audio20_control_cur_4_t));

        sampFreq = (uint32_t) ((audio20_control_cur_4_t *) pBuff)->bCur;

        TU_LOG2("Clock set current freq: %" PRIu32 "\r\n", sampFreq);

        return true;
        break;

      // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

static bool audio20_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // Input terminal (Microphone input)
  if (entityID == UAC2_ENTITY_INPUT_TERMINAL) {
    switch (ctrlSel) {
      case AUDIO20_TE_CTRL_CONNECTOR: {
        // The terminal connector control only has a get request with only the CUR attribute.
        audio20_desc_channel_cluster_t ret;

        // Those are dummy values for now
        ret.bNrChannels = 1;
        ret.bmChannelConfig = 0;
        ret.iChannelNames = 0;

        TU_LOG2("    Get terminal connector\r\n");

        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void *) &ret, sizeof(ret));
      } break;

        // Unknown/Unsupported control selector
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Feature unit
  if (entityID == UAC2_ENTITY_FEATURE_UNIT) {
    switch (ctrlSel) {
      case AUDIO20_FU_CTRL_MUTE:
        // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
        // There does not exist a range parameter block for mute
        TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute[channelNum], 1);

      case AUDIO20_FU_CTRL_VOLUME:
        switch (p_request->bRequest) {
          case AUDIO20_CS_REQ_CUR:
            TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &volume[channelNum], sizeof(volume[channelNum]));

          case AUDIO20_CS_REQ_RANGE:
            TU_LOG2("    Get Volume range of channel: %u\r\n", channelNum);

            // Copy values - only for testing - better is version below
            audio20_control_range_2_n_t(1)
                ret;

            ret.wNumSubRanges = 1;
            ret.subrange[0].bMin = -90;// -90 dB
            ret.subrange[0].bMax = 30; // +30 dB
            ret.subrange[0].bRes = 1;  // 1 dB steps

            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void *) &ret, sizeof(ret));

            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
        break;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Clock Source unit
  if (entityID == UAC2_ENTITY_CLOCK) {
    switch (ctrlSel) {
      case AUDIO20_CS_CTRL_SAM_FREQ:
        // channelNum is always zero in this case
        switch (p_request->bRequest) {
          case AUDIO20_CS_REQ_CUR:
            TU_LOG2("    Get Sample Freq.\r\n");
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &sampFreq, sizeof(sampFreq));

          case AUDIO20_CS_REQ_RANGE: {
            TU_LOG2("    Get Sample Freq. range\r\n");
            audio20_control_range_4_n_t(N_sampleRates) rangef =
                {
                    .wNumSubRanges = tu_htole16(N_sampleRates)};
            TU_LOG1("Clock get %d freq ranges\r\n", N_sampleRates);
            for (uint8_t i = 0; i < N_sampleRates; i++) {
              rangef.subrange[i].bMin = (int32_t) sampleRatesList[i];
              rangef.subrange[i].bMax = (int32_t) sampleRatesList[i];
              rangef.subrange[i].bRes = 0;
              TU_LOG1("Range %d (%d, %d, %d)\r\n", i, (int) rangef.subrange[i].bMin, (int) rangef.subrange[i].bMax, (int) rangef.subrange[i].bRes);
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rangef, sizeof(rangef));
          }
            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
        break;

      case AUDIO20_CS_CTRL_CLK_VALID:
        // Only cur attribute exists for this request
        TU_LOG2("    Get Sample Freq. valid\r\n");
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &clkValid, sizeof(clkValid));

      // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

#endif // TUD_OPT_HIGH_SPEED

//--------------------------------------------------------------------+
// Main Callback Functions
//--------------------------------------------------------------------+

// Invoked when set interface is called, typically on start/stop streaming or format change
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  //uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  // Clear buffer when streaming format is changed
  if (alt != 0) {
    bytesPerSample = bytesPerSampleAltList[alt - 1];
  }
  return true;
}

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
  (void) rhport;
  (void) pBuff;

  if (tud_audio_version() == 1) {
    return audio10_set_req_ep(p_request, pBuff);
  } else if (tud_audio_version() == 2) {
    // We do not support any requests here
  }

  return false;// Yet not implemented
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;

  if (tud_audio_version() == 1) {
    return audio10_get_req_ep(rhport, p_request);
  } else if (tud_audio_version() == 2) {
    // We do not support any requests here
  }

  return false;// Yet not implemented
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
  (void) rhport;

  if (tud_audio_version() == 1) {
    return audio10_set_req_entity(p_request, pBuff);
#if TUD_OPT_HIGH_SPEED
  } else if (tud_audio_version() == 2) {
    return audio20_set_req_entity(p_request, pBuff);
#endif
  }

  return false;// Yet not implemented
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;

  if (tud_audio_version() == 1) {
    return audio10_get_req_entity(rhport, p_request);
#if TUD_OPT_HIGH_SPEED
  } else if (tud_audio_version() == 2) {
    return audio20_get_req_entity(rhport, p_request);
#endif
  }

  return false;// Yet not implemented
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  (void) p_request;
  // startVal = 0;

  return true;
}


#if 0

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

const int csSampleFrequencyControl = 1;

// Invoked when audio class specific set request received for an EP
bool tud_uacv1_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;
  (void) pBuff;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == UACv1_CS_REQ_CUR);		// Returns false if not true.

  // Section 5.2.1.1 in https://www.usb.org/sites/default/files/audio10.pdf:
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t ep = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) ep;

  if (ctrlSel == csSampleFrequencyControl) {
	  // We accept and quietly ignore attempts to set the sampling rate for the moment.
	  return true;
  }

  return true;		// Accept and ignore any attempt to set anything on the endpoint.
}

// Invoked when audio class specific set request received for an interface
bool tud_uacv1_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;
  (void) pBuff;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == UACv1_CS_REQ_CUR);

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) itf;

  return false; 	// Yet not implemented
}

// Invoked when audio class specific set request received for an entity
bool tud_uacv1_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  (void) itf;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == UACv1_CS_REQ_CUR);

  // If request is for our feature unit
  if ( entityID == 2 )
  {
    switch ( ctrlSel )
    {
      case UACv1_FU_CTRL_MUTE:
        // Request uses format layout 1
        TU_VERIFY(p_request->wLength == sizeof(uacv1_control_cur_1_t));

        mute[channelNum] = ((uacv1_control_cur_1_t*) pBuff)->bCur;

        TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);
      return true;

      case UACv1_FU_CTRL_VOLUME:
        // Request uses format layout 2
        TU_VERIFY(p_request->wLength == sizeof(uacv1_control_cur_2_t));

        volume[channelNum] = ((uacv1_control_cur_2_t*) pBuff)->bCur;

        TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", volume[channelNum], channelNum);
      return true;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
      return false;
    }
  }
  return false;    // Yet not implemented
}

// Invoked when audio class specific get request received for an EP
bool tud_uacv1_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Section 5.2.1.1 in UACv1 specification at https://www.usb.org/sites/default/files/audio10.pdf:
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t ep = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) ep;

  if (ctrlSel == csSampleFrequencyControl) {
	  int32_t samplesPerFrame = SAMPLES_PER_FRAME * 1000;

	  // Return the sampling rate as 3 bytes:
	  return tud_control_xfer(rhport, p_request, &samplesPerFrame, 3);
  }
  else {
    return false; 	// Yet not implemented
  }
}

// Invoked when audio class specific get request received for an interface
bool tud_uacv1_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) itf;

  return false; 	// Yet not implemented
}

// Invoked when audio class specific get request received for an entity
bool tud_uacv1_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  // uint8_t itf = TU_U16_LOW(p_request->wIndex); 			// Since we have only one audio function implemented, we do not need the itf value
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // Input terminal (Microphone input)
  if (entityID == 1)
  {
    switch ( ctrlSel )
    {
      case UACv1_TE_CTRL_CONNECTOR:
      {
        // The terminal connector control only has a get request with only the CUR attribute.
        uacv1_desc_channel_cluster_t ret;

        // Those are dummy values for now
        ret.bNrChannels = 1;
        ret.bmChannelConfig = 0;
        ret.iChannelNames = 0;

        TU_LOG2("    Get terminal connector\r\n");

        return tud_uacv1_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));
      }
      break;

        // Unknown/Unsupported control selector
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Feature unit
  if (entityID == 2)
  {
    switch ( ctrlSel )
    {
      case UACv1_FU_CTRL_MUTE:
        // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
        // There does not exist a range parameter block for mute
        TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
        return tud_control_xfer(rhport, p_request, &mute[channelNum], 1);

      case UACv1_FU_CTRL_VOLUME:
        switch ( p_request->bRequest )
        {
          case UACv1_CS_REQ_CUR:
            TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
            return tud_control_xfer(rhport, p_request, &volume[channelNum], sizeof(volume[channelNum]));

          case UACv1_CS_REQ_RANGE:
            TU_LOG2("    Get Volume range of channel: %u\r\n", channelNum);

            // Copy values - only for testing - better is version below
            uacv1_control_range_2_n_t(1) ret;

            ret.wNumSubRanges = 1;
            ret.subrange[0].bMin = -90;           // -90 dB
            ret.subrange[0].bMax = 90;		// +90 dB
            ret.subrange[0].bRes = 1; 		// 1 dB steps

            return tud_uacv1_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));

            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
      break;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Clock Source unit
  if ( entityID == 4 )
  {
    switch ( ctrlSel )
    {
      case UACv1_CS_CTRL_SAM_FREQ:
        // channelNum is always zero in this case
        switch ( p_request->bRequest )
        {
          case UACv1_CS_REQ_CUR:
            TU_LOG2("    Get Sample Freq.\r\n");
            return tud_control_xfer(rhport, p_request, &sampFreq, sizeof(sampFreq));

          case UACv1_CS_REQ_RANGE:
            TU_LOG2("    Get Sample Freq. range\r\n");
            return tud_control_xfer(rhport, p_request, &sampleFreqRng, sizeof(sampleFreqRng));

           // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
      break;

      case UACv1_CS_CTRL_CLK_VALID:
        // Only cur attribute exists for this request
        TU_LOG2("    Get Sample Freq. valid\r\n");
        return tud_control_xfer(rhport, p_request, &clkValid, sizeof(clkValid));

      // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  TU_LOG2("  Unsupported entity: %d\r\n", entityID);
  return false; 	// Yet not implemented
}

#endif // 0

#if 0	// We don't use these, instead feeding the tx FIFO directly from the ADC/DMA interrupt.
bool tud_uacv1_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) itf;
  (void) ep_in;
  (void) cur_alt_setting;

#if CFG_TUD_UACv1_ENABLE_ENCODING
  // Pass in the data to tusb to write.
  for (uint8_t cnt=0; cnt < CFG_TUD_UACv1_FUNC_1_N_TX_SUPP_SW_FIFO; cnt++)	// For each FIFO.
  {
    tud_uacv1_write_support_ff(cnt, i2s_dummy_buffer[cnt], UACv1_SAMPLE_RATE/1000 * CFG_TUD_UACv1_FUNC_1_N_BYTES_PER_SAMPLE_TX * CFG_TUD_UACv1_FUNC_1_CHANNEL_PER_FIFO_TX);
  }
#else
  // A write has completed we have a chance to write more data to the FIFO:
  //

  tud_uacv1_write((const void *) i2s_dummy_buffer[0], 384);

#endif

  return true;
}

bool tud_uacv1_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) n_bytes_copied;
  (void) itf;
  (void) ep_in;
  (void) cur_alt_setting;

  // leds_set_trace();

#if 1
  static uint16_t dataVal = 0;
  static int frame_count = 0;

  if (frame_count++ == 10) {
	  frame_count = 0;
	  dataVal = 0;
  }

  // Generate dummy data
  for (uint16_t cnt = 0; cnt < CFG_TUD_UACv1_FUNC_1_N_TX_SUPP_SW_FIFO; cnt++)
  {
    uint16_t * p_buff = i2s_dummy_buffer[cnt];              // 2 bytes per sample
    // dataVal = 1;
    for (uint16_t cnt2 = 0; cnt2 < UACv1_SAMPLE_RATE/1000; cnt2++)
    {
      for (uint8_t cnt3 = 0; cnt3 < CFG_TUD_UACv1_FUNC_1_CHANNEL_PER_FIFO_TX; cnt3++)
      {
        *p_buff++ = dataVal;
      }
      dataVal++;
    }
  }
#endif

#if 0
  int16_t *p_buff = (int16_t *) i2s_dummy_buffer[0];
  data_processor_uac_getUSBData(p_buff, SAMPLES_PER_FRAME);
#endif

  // leds_reset_trace();

  return true;
}
#endif

#if 0
bool tud_uacv1_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;
  (void) p_request;

  return true;
}
#endif // 0
