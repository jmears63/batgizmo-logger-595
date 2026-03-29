/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
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
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "rtc.h"

/* USER CODE BEGIN 0 */

#include <string.h>
#include <time.h>
#include "stm32u5xx_hal_rcc.h"

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

static void rtc_calendar_hardware_init(void)
{
	RTC_PrivilegeStateTypeDef privilegeState = {0};

	hrtc.Instance = RTC;
	hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
	hrtc.Init.AsynchPrediv = 127;
	hrtc.Init.SynchPrediv = 255;
	hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
	hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
	hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
	hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
	hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
	hrtc.Init.BinMode = RTC_BINARY_NONE;
	if (HAL_RTC_Init(&hrtc) != HAL_OK)
	{
		Error_Handler();
	}
	privilegeState.rtcPrivilegeFull = RTC_PRIVILEGE_FULL_NO;
	privilegeState.backupRegisterPrivZone = RTC_PRIVILEGE_BKUP_ZONE_NONE;
	privilegeState.backupRegisterStartZone2 = RTC_BKP_DR0;
	privilegeState.backupRegisterStartZone3 = RTC_BKP_DR0;
	if (HAL_RTCEx_PrivilegeModeSet(&hrtc, &privilegeState) != HAL_OK)
	{
		Error_Handler();
	}
}

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  rtc_calendar_hardware_init();

  /* USER CODE BEGIN Check_RTC_BKUP */

  // Important: return here to bypass the code below that would set the wrong date and time:
  return;

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0x0;
  sAlarm.AlarmTime.Minutes = 0x0;
  sAlarm.AlarmTime.Seconds = 0x0;
  sAlarm.AlarmTime.SubSeconds = 0x0;
  sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLKAM_ENABLE();

    /* RTC interrupt Init */
    HAL_NVIC_SetPriority(RTC_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
    __HAL_RCC_RTCAPB_CLK_DISABLE();
    __HAL_RCC_RTCAPB_CLKAM_DISABLE();

    /* RTC interrupt Deinit */
    HAL_NVIC_DisableIRQ(RTC_IRQn);
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

static bool s_low_noise_rtc_mode;
static time_t s_epoch_ref_at_enter;
static uint32_t s_tick_ms_at_enter;

static time_t rtc_read_epoch_from_hardware_rtc(struct tm *tm_out)
{
	RTC_TimeTypeDef t;
	RTC_DateTypeDef d;

	memset(&t, 0, sizeof(t));
	memset(&d, 0, sizeof(d));
	(void)HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
	(void)HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

	tm_out->tm_sec = t.Seconds;
	tm_out->tm_min = t.Minutes;
	tm_out->tm_hour = t.Hours;
	tm_out->tm_mday = d.Date;
	tm_out->tm_mon = d.Month - 1;
	/* struct tm.tm_year is years since 1900; HAL Year is 0-99 for 2000-2099 */
	tm_out->tm_year = (int)d.Year + 100;
	tm_out->tm_isdst = -1;

	return mktime(tm_out);
}

static void rtc_epoch_to_tm_split(time_t epoch, struct tm *tm_out)
{
	localtime_r(&epoch, tm_out);
}

static void rtc_set_hardware_calendar_from_epoch(time_t epoch)
{
	struct tm tm_split;
	rtc_epoch_to_tm_split(epoch, &tm_split);

	RTC_TimeTypeDef t = {0};
	RTC_DateTypeDef d = {0};
	t.Hours = tm_split.tm_hour;
	t.Minutes = tm_split.tm_min;
	t.Seconds = tm_split.tm_sec;
	t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	t.StoreOperation = RTC_STOREOPERATION_RESET;

	d.WeekDay = (tm_split.tm_wday == 0) ? RTC_WEEKDAY_SUNDAY : (uint8_t)tm_split.tm_wday;
	d.Month = tm_split.tm_mon + 1;
	d.Date = tm_split.tm_mday;
	d.Year = tm_split.tm_year - 100;

	if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)
	{
		Error_Handler();
	}
}

static void rtc_configure_lse(uint32_t lse_state)
{
	RCC_OscInitTypeDef osc = {0};
	osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
	osc.LSEState = lse_state;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK)
	{
		Error_Handler();
	}
}

void rtc_enter_low_noise_mode(void)
{
	if (s_low_noise_rtc_mode)
	{
		return;
	}

	HAL_PWR_EnableBkUpAccess();

	struct tm tm_now;
	s_epoch_ref_at_enter = rtc_read_epoch_from_hardware_rtc(&tm_now);
	s_tick_ms_at_enter = HAL_GetTick();

	if (HAL_RTC_DeInit(&hrtc) != HAL_OK)
	{
		Error_Handler();
	}

	rtc_configure_lse(RCC_LSE_OFF);

	s_low_noise_rtc_mode = true;
}

void rtc_exit_low_noise_mode(void)
{
	if (!s_low_noise_rtc_mode)
	{
		return;
	}

	uint32_t delta_ms = HAL_GetTick() - s_tick_ms_at_enter;
	time_t epoch_now = s_epoch_ref_at_enter + (time_t)(delta_ms / 1000U);

	HAL_PWR_EnableBkUpAccess();

	rtc_configure_lse(RCC_LSE_ON_RTC_ONLY);

	rtc_calendar_hardware_init();
	rtc_set_hardware_calendar_from_epoch(epoch_now);

	s_low_noise_rtc_mode = false;
}

bool rtc_is_low_noise_mode(void)
{
	return s_low_noise_rtc_mode;
}


/**
 * Return the wall clock time, ie the time adjusted for both time zone and DST.
 */
bool rtc_get_effective_time(RTC_TimeTypeDef *t, RTC_DateTypeDef *d)
{
	memset(t, 0, sizeof(*t));
	memset(d, 0, sizeof(*d));

	if (s_low_noise_rtc_mode)
	{
		uint32_t delta_ms = HAL_GetTick() - s_tick_ms_at_enter;
		time_t epoch = s_epoch_ref_at_enter + (time_t)(delta_ms / 1000U);
		struct tm tm_split;
		rtc_epoch_to_tm_split(epoch, &tm_split);
		t->Hours = tm_split.tm_hour;
		t->Minutes = tm_split.tm_min;
		t->Seconds = tm_split.tm_sec;
		t->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
		t->StoreOperation = RTC_STOREOPERATION_RESET;
		d->WeekDay = (tm_split.tm_wday == 0) ? RTC_WEEKDAY_SUNDAY : (uint8_t)tm_split.tm_wday;
		d->Month = tm_split.tm_mon + 1;
		d->Date = tm_split.tm_mday;
		d->Year = tm_split.tm_year - 100;
		return true;
	}
  else {
	  // We *have* to call GetTime and GetDate, otherwise the time is stuck. Duh.
    if (HAL_RTC_GetTime(&hrtc, t, RTC_FORMAT_BIN) != HAL_OK)
    {
      return false;
    }
    if (HAL_RTC_GetDate(&hrtc, d, RTC_FORMAT_BIN) != HAL_OK)
    {
      return false;
    }
    return true;
  }
}

time_t rtc_get_effective_epoch_time(struct tm *now_out)
{
	memset(now_out, 0, sizeof(*now_out));

	if (s_low_noise_rtc_mode)
	{
		uint32_t delta_ms = HAL_GetTick() - s_tick_ms_at_enter;
		time_t epoch = s_epoch_ref_at_enter + (time_t)(delta_ms / 1000U);
		localtime_r(&epoch, now_out);
		return epoch;
	}
  else
  	return rtc_read_epoch_from_hardware_rtc(now_out);
}

/* USER CODE END 1 */
