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

#include <data_processor_buffers.h>
#include <stdio.h>

#include "my_sdmmc.h"
#include "gpio.h"
#include "rtc.h"
#include "app_filex.h"
#include "fx_api.h"
#include "fx_stm32_sd_driver.h"
#include "stm32u5xx_hal_sd.h"		// For BLOCKSIZE.

#include "storage.h"
#include "buffer.h"
#include "settings.h"
#include "gain.h"
#include "sd_lowlevel.h"

typedef int16_t wav_data_type_t;

static int s_wav_total_data_count = 0;

static int wav_offset_to_cksize1 = 0;
static int wav_offset_to_cksize2 = 0;
static int wav_offset_to_guano = 0;

static int s_bytes_per_sample = sizeof(wav_data_type_t);
static uint16_t s_num_channels = 1;    // Type matches what we need for the wav file.

// Support for logic for debouncing SD card presence detection:
static bool s_debounced_sd_present = false;
static int s_sd_present_count = 0;
#define DEBOUNCE_COUNT 20

#define TEMP_FILE_NAME ".temp.wav"

#define TRIGGER_LEN 32

typedef struct {
	int sampling_rate;
	char trigger[TRIGGER_LEN];
	RTC_TimeTypeDef time;
	RTC_DateTypeDef date;
	double latitude, longitude;
	bool location_present;
} guano_data_t;

guano_data_t s_guano_data;

/*
	The following buffer is used as a sector cache by FileX for both data and FAT.
	Measurements show no difference in cache hits between 8192 and 32786 bytes when
	writing 1s of data, so leaving it at 8192.

	Note that FileX only uses caching for handling specific cases such as a appending
	data to a sector that is already parly written to. So there is no real value
	in increasing the follow much.
 */
static char s_filex_working_memory[8192];

/*
 * We use reference counting to track mounts and unmounts from multiple modules.
 */
static FX_MEDIA s_fx_medium;		// Represents the SD card.
static int s_mount_ref_count = 0;	// We used reference counting so that multiple modules can mount and unmount
									// without falling over each other.

static const char *get_guano_string(const guano_data_t *data);

void storage_init(void)
{
	bool sd_present = HAL_GPIO_ReadPin(GPIO_SD_DETECT_GPIO_Port, GPIO_SD_DETECT_Pin) == GPIO_PIN_RESET;
	s_debounced_sd_present = sd_present;		// Initialize to the current state.
	s_sd_present_count = 0;
	memset(&s_fx_medium, 0, sizeof(s_fx_medium));
	s_mount_ref_count = 0;
	memset(&s_guano_data, 0, sizeof(s_guano_data));
}

/**
 * Tell FileX what time it is so that file timestamps are correct, relative to
 * the RTC.
 */
void storage_set_filex_time(void)
{
	RTC_TimeTypeDef t;
	RTC_DateTypeDef d;
	memset(&t, 0, sizeof(t));
	memset(&d, 0, sizeof(d));
	bool ok = HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) == HAL_OK;

	// We *have* to call GetDate, otherwise the time is stuck. Duh.
	ok = (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) == HAL_OK) && ok;

	if (ok) {
		fx_system_time_set(t.Hours, t.Minutes, t.Seconds);
		fx_system_date_set(d.Year + 2000, d.Month, d.Date);
	}
}

static void write_guano_data(FX_FILE *pFile, const guano_data_t *data)
{
	const char *guano_string = get_guano_string(data);
	fx_file_write(pFile, "guan", 4);

	uint32_t cksize = strlen(guano_string);
	fx_file_write(pFile, &cksize, sizeof(cksize));
	fx_file_write(pFile, (void*) guano_string, cksize);
	if ((cksize & 1) == 1) {
		// The WAV standard says to pad odd numbered data sections with a 0 byte:
		uint8_t b = 0;
		fx_file_write(pFile, (void*) &b, 1);
	}
}

static void write_wav_header(FX_FILE *pFile, int sampling_rate, const char *trigger)
{
	// https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
	// https://docs.fileformat.com/audio/wav/

	/*
  ckID  4 Chunk ID: RIFF
  cksize  4 Chunk size: 4 + 24 + (8 + M*Nc*Ns + (0 or 1)      (pad to even length)
  WAVEID  4 WAVE ID: WAVE
  ckID  4 Chunk ID: fmt
  cksize  4 Chunk size: 16
  wFormatTag  2 WAVE_FORMAT_PCM
  nChannels 2 Nc
  nSamplesPerSec  4 F
  nAvgBytesPerSec 4 F*M*Nc
  nBlockAlign 2 M*Nc
  wBitsPerSample  2 rounds up to 8*M
  ckID  4 Chunk ID: data
  cksize  4 Chunk size: M*Nc*Ns
  sampled data  M*Nc*Ns Nc*Ns channel-interleaved M-byte samples
  pad byte  0 or 1  Padding byte if M*Nc*Ns is odd
	 */

	int num_samples = 0;          // We will provide this later.

	fx_file_write(pFile, "RIFF", 4);

	// This needs to be the file size in bytes - 8, ie the remaining file size.
	wav_offset_to_cksize1 = pFile->fx_file_current_file_offset;
	uint32_t cksize = 4 + 24 + 8 + s_bytes_per_sample * s_num_channels * num_samples;  // This has to be even so no padding required.
	fx_file_write(pFile, &cksize, sizeof(cksize));

	fx_file_write(pFile, "WAVE", 4);
	fx_file_write(pFile, "fmt ", 4);
	cksize = 16;
	fx_file_write(pFile, &cksize, sizeof(cksize));

	uint16_t WAVE_FORMAT_PCM = 0x0001;
	fx_file_write(pFile, &WAVE_FORMAT_PCM, sizeof(WAVE_FORMAT_PCM));

	fx_file_write(pFile, &s_num_channels, sizeof(s_num_channels));

	uint32_t samples_per_second = sampling_rate;
	fx_file_write(pFile, &samples_per_second, sizeof(samples_per_second));

	uint32_t bytes_per_second = sampling_rate * s_bytes_per_sample * s_num_channels;
	fx_file_write(pFile, &bytes_per_second, sizeof(bytes_per_second));

	uint16_t block_align = s_bytes_per_sample * s_num_channels;
	fx_file_write(pFile, &block_align, sizeof(block_align));

	uint16_t bits_per_sample = s_bytes_per_sample * 8;
	fx_file_write(pFile, &bits_per_sample, sizeof(bits_per_sample));


	// Write a guano section that we will overwrite after acquisition once everything
	// is known:
	wav_offset_to_guano = pFile->fx_file_current_file_offset;
	write_guano_data(pFile, &s_guano_data);


#define CLUSTER_ALIGNMENT_HACK 1
#if CLUSTER_ALIGNMENT_HACK
	unsigned long header_length = pFile->fx_file_current_file_offset;

	// This is a slightly hacky way to make sure the data is sent as blocks aligning
	// with 32K cluster sizes, for efficiency. Readers of the file *should* ignore the
	// unexpected pad section.

	fx_file_write(pFile, "pad ", 4);
	cksize = 32768 - header_length - 8 /* padding chunk name and length */ - 8 /* data chunk header */;
	fx_file_write(pFile, &cksize, sizeof(cksize));

	static char buf[BLOCKSIZE];
	memset(buf, '/', sizeof(buf));
	int bytes_written = 0;
	for (int i = cksize; i > 0; ) {
		int bytes_to_write = i > sizeof(buf) ? sizeof(buf) : i;
		fx_file_write(pFile, buf, bytes_to_write);
		bytes_written += bytes_to_write;
		i -= bytes_to_write;
	}
#endif

	fx_file_write(pFile, "data", 4);

	wav_offset_to_cksize2 = pFile->fx_file_current_file_offset;
	cksize = s_bytes_per_sample * s_num_channels * num_samples;
	fx_file_write(pFile, &cksize, sizeof(cksize));
}

static const char *get_guano_string(const guano_data_t *data)
{
	/*
		IMPORTANT: the guano data as text must be a fixed length, because we will
		overwrite it after data acquisition is complete, and it must precede the data
		in the wav file, so we can cope with wav files with incorrect data lengths in their headers.
	*/
	snprintf(g_2k_char_buffer, LEN_2K_BUFFER,
			"GUANO|Version: 1.0\n"
			"Timestamp: %04d%02d%02dT%02d:%02d:%02d\n"
			"Samplerate: %06d\n"
			"Make: BatGizmo\n"
			"Model: Logger\n"
			"Firmware Version: %s\n"
			"BatGizmo|GainIndex: %d\n"
			"BatGizmo|Trigger: %*s\n",	// Trailing \n matters.
			data->date.Year + 2000, data->date.Month, data->date.Date, data->time.Hours, data->time.Minutes, data->time.Seconds,
			data->sampling_rate,
			FIRMWARE_VERSION,
			gain_get_range(),
			TRIGGER_LEN, (char*) data->trigger
	);

	if (data->location_present) {
		// Zero padding to achieve fixed string length:
		snprintf(g_128bytes_char_buffer, LEN_128BYTES_BUFFER, "Loc Position: %3.6lf %3.6lf\n", data->latitude, data->longitude);
		strncat(g_2k_char_buffer, g_128bytes_char_buffer, LEN_2K_BUFFER - 1);
	}

	return g_2k_char_buffer;
}

static void patch_wav_header(FX_FILE *pFile, int sample_count)
{
	if (fx_file_seek(pFile, wav_offset_to_cksize1) == FX_SUCCESS) {
		uint32_t cksize = 4 + 24 + 8 + s_bytes_per_sample * s_num_channels * sample_count;
		fx_file_write(pFile, &cksize, sizeof(cksize));
	}

	if (fx_file_seek(pFile, wav_offset_to_cksize2) == FX_SUCCESS) {
		uint32_t cksize = s_bytes_per_sample * s_num_channels * sample_count;
		fx_file_write(pFile, &cksize, sizeof(cksize));
	}
}

/**
 *  Do everything needed to access the SD card, and return the FX media structure
 *  if we were successful, otherwise NULL.
 *
 *  The caller must call storage_unmount in due course if this succeeded.
 *
 *  Four bit bandwidth results in snappier data transfer, obviously at the cost of greater
 *  noise generated by SDIO. So during acquisition, it should be set to 1 bit.
 *
 *  Note that write_type is only res

 *  pected on the call of this method that increments
 *  reference counts from 0 to 1.
 *
 */
FX_MEDIA *storage_mount(storage_write_type_t write_type)
{
	memset(&s_fx_medium, 0, sizeof(s_fx_medium));

	if (s_mount_ref_count == 0) {
		if (sd_lowlevel_open(write_type)) {
			MX_FileX_Init();
			if (hsd1.ErrorCode == HAL_SD_ERROR_NONE) {
				// From a quick skim of the FileX source code the media name is only used in trace.
				UINT status = fx_media_open(&s_fx_medium, "STM32_SD",
						fx_stm32_sd_driver,	0, s_filex_working_memory, sizeof(s_filex_working_memory));
				if (status == FX_SUCCESS) {
					s_mount_ref_count++;
					return &s_fx_medium;
				}
			}
		}

		// If we get here we failed, so we need to clean up:
		storage_unmount(false);
		return NULL;
	}
	else
	{
		s_mount_ref_count++;
		return &s_fx_medium;
	}

}

void storage_unmount(bool clean_unmount)
{
	if (s_mount_ref_count > 0)
		s_mount_ref_count--;

	if (s_mount_ref_count == 0) {
		if (clean_unmount) {
			// It's OK to call this when the media isn't open:
			fx_media_close(&s_fx_medium);
		}

		sd_lowlevel_close();
	}
}

void storage_flush(FX_MEDIA *pMedium)
{
	fx_media_flush(pMedium);
}

static void get_base_name(char *buf, size_t buflen) {

	snprintf(buf, buflen, "data");		// Fallback if we fail to read date/time from RTC.

	// Create a file name based on date and time: YYYYMMDD_HHMMss
	RTC_TimeTypeDef t;
	RTC_DateTypeDef d;
	memset(&t, 0, sizeof(t));
	memset(&d, 0, sizeof(d));
	bool ok = HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) == HAL_OK;
	// We *have* to call GetDate, otherwise the time is stuck. Duh.
	ok = (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) == HAL_OK) && ok;
	if (ok) {
		snprintf(buf, buflen, "%04d%02d%02d_%02d%02d%02d",
				d.Year + 2000, d.Month, d.Date,
				t.Hours, t.Minutes, t.Seconds);
	}
}

static void note_guano_data(int sampling_rate, const char *trigger)
{
	memset(&s_guano_data, 0, sizeof(s_guano_data));

	s_guano_data.sampling_rate = sampling_rate;
	strncpy(s_guano_data.trigger, trigger, TRIGGER_LEN);
	s_guano_data.trigger[TRIGGER_LEN - 1] = '\0';
	memset(&s_guano_data.time, 0, sizeof(s_guano_data.time));
	memset(&s_guano_data.date, 0, sizeof(s_guano_data.date));
	HAL_RTC_GetTime(&hrtc, &s_guano_data.time, RTC_FORMAT_BIN);
	// We *have* to call GetDate, otherwise the time is stuck. Duh.
	HAL_RTC_GetDate(&hrtc, &s_guano_data.date, RTC_FORMAT_BIN);

	const settings_t *pSettings = settings_get();
	s_guano_data.location_present = pSettings->_location_present;
	s_guano_data.latitude = pSettings->latitude;
	s_guano_data.longitude = pSettings->longitude;
}

FX_FILE *storage_open_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile, int sampling_rate, const char *trigger)
{

	memset(pFile, 0, sizeof(*pFile));

	storage_set_filex_time();		// So the file timestamp is right for the file we create.

	UINT status = fx_file_create(&s_fx_medium, TEMP_FILE_NAME);
	if (status != FX_SUCCESS && status != FX_ALREADY_CREATED)
		return NULL;

	if (fx_file_open(pMedium, pFile, TEMP_FILE_NAME, FX_OPEN_FOR_WRITE) != FX_SUCCESS)
		return NULL;

	// Truncate the file if it already exists:
	if (fx_file_seek(pFile, 0) != FX_SUCCESS)
		return NULL;

	s_wav_total_data_count = 0;

	/*
		We must record guano data at the point we open the wav file, before we write the headers including
		the guano header, so that the guano header length doesn't change before we update it at the
		end of data recording.
	*/
	note_guano_data(sampling_rate, trigger);

	write_wav_header(pFile, sampling_rate, trigger);

	return pFile;
}

#if 0
static int s_append_data_count = 0;
#endif

void storage_wav_file_append_data(FX_FILE *pFile, int16_t *pBuffer, int len)
{
	s_wav_total_data_count += len;
#if 0
	s_append_data_count++;
#endif

#if USE_FIFO_OLD
	// Do a buffered write so that writes to FileX are lazy, ie at the
	// deferred as late as possible:
	buffered_write(pFile, pBuffer, len * sizeof(*pBuffer));
#else
	fx_file_write(pFile, pBuffer, len * sizeof(*pBuffer));
#endif
}

#if 0
void storage_set_trigger_string(const char *trigger)
{
	strncpy(s_guano_data.trigger, trigger, TRIGGER_LEN);
	s_guano_data.trigger[TRIGGER_LEN - 1] = '\0';
}
#endif

void storage_close_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile)
{
	// Now we know how much data there is, we can patch that back into the WAV header:
	patch_wav_header(pFile, s_wav_total_data_count);

	/*
	 *  Update the guano data now that we have the data. This works because we take care
	 *  that the guano data is a fixed length.
	 */
	if (fx_file_seek(pFile, wav_offset_to_guano) == FX_SUCCESS) {
		write_guano_data(pFile, &s_guano_data);
	}

	fx_file_close(pFile);

	// Rename the file we just closed to the correct name based on time:
	get_base_name(g_128bytes_char_buffer, LEN_128BYTES_BUFFER);

	const char *pExt = ".wav";
	snprintf(g_2k_char_buffer, LEN_2K_BUFFER, "%s%s", g_128bytes_char_buffer, pExt);

	// Ignoring failure - what can we do?
	UINT status = fx_file_rename(pMedium, TEMP_FILE_NAME, g_2k_char_buffer);
	(void) status;

	// Flush to SD to reduce risk of data loss:
	fx_media_flush(pMedium);
}

/**
 * Close the file and remove it from storage.
 */
void storage_clean_up_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile) {
	fx_file_close(pFile);
	fx_file_delete(pMedium, TEMP_FILE_NAME);
	// Flush to keep the SD file system consistent:
	fx_media_flush(pMedium);
}

void storage_write_settings(FX_MEDIA *pMedium)
{
	storage_set_filex_time();		// So the file timestamp is right for the file we create.

	get_base_name(g_128bytes_char_buffer, LEN_128BYTES_BUFFER);

	const char *pExt = ".json";
	UINT status = FX_SUCCESS;
	snprintf(g_2k_char_buffer, LEN_2K_BUFFER, "%s-settings%s", g_128bytes_char_buffer, pExt);
	for (int i = 0; i < 100; i++) {
		status = fx_file_create(&s_fx_medium, g_2k_char_buffer);
		if (FX_SUCCESS != status && FX_ALREADY_CREATED != status)
			return;

		if (status == FX_SUCCESS) {
			break;
		}
		else if (status == FX_ALREADY_CREATED) {
			// Already exists: try adding a suffix:
			snprintf(g_2k_char_buffer, LEN_2K_BUFFER, "%s-%d%s", g_128bytes_char_buffer, i + 1, pExt);
		}
	}

	// If we get here, we either created the file successfully or ran out of suffixes to try:
	if (status != FX_SUCCESS)
		return;

	FX_FILE file;
	if (fx_file_open(pMedium, &file, g_2k_char_buffer, FX_OPEN_FOR_WRITE) == FX_SUCCESS) {
		// This overwrites the filename in the buffer:
		size_t json_len = settings_get_json_settings_string(g_2k_char_buffer, LEN_2K_BUFFER);
		fx_file_write(&file, g_2k_char_buffer, json_len);
		fx_file_close(&file);
	}
}

bool storage_capacity(uint32_t* block_count, uint16_t* block_size)
{
  if (s_mount_ref_count > 0)
  {
    *block_size = s_fx_medium.fx_media_bytes_per_sector;
    *block_count = s_fx_medium.fx_media_total_sectors;
    return true;
  }
  else
    return false;
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

void storage_main_processing(int)
{
	do_sd_present();
}
