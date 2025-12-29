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

#ifndef INC_STORAGE_H_
#define INC_STORAGE_H_

#include <stdbool.h>
#include "fx_api.h"
#include "my_sdmmc.h"

void storage_init(void);
void storage_set_filex_time(void);
FX_MEDIA *storage_mount(storage_write_type_t bandwidth);
void storage_unmount(bool clean_unmount);
void storage_flush(FX_MEDIA *pMedium);
FX_FILE *storage_open_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile, int sampling_rate, const char *trigger);
void storage_close_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile);
void storage_clean_up_wav_file(FX_MEDIA *pMedium, FX_FILE *pFile);
void storage_wav_file_append_data(FX_FILE *pFile, int16_t *pBuffer, int len);
void storage_write_settings(FX_MEDIA *pMedium);
bool storage_sd_card_present(void);
bool storage_get_debounced_sd_present(void);
void storage_main_processing(int);
FX_MEDIA *storage_get_medium(void);
#if 0
void storage_set_trigger_string(const char *trigger);
#endif

#endif /* INC_STORAGE_H_ */
