/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "app_filex.h"
#include "gpdma.h"
#include "icache.h"
#include "rtc.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <memory.h>
#include <sd_lowlevel.h>

#include "leds.h"
#include "mode.h"
#include "mode_manual.h"
#include "mode_usb.h"
#include "mode_auto.h"
#include "init.h"
#include "settings.h"
#include "storage.h"
#include "recording.h"
#include "data_processor_uac.h"
#include "data_acquisition.h"
#include "autophasecontrol.h"
#include "tusb_config.h"
#include "trigger.h"
#include "sd_lowlevel.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  // Set up a simple guard value to detect if the stack crashed through the heap:
  uint32_t *pGuard = malloc(sizeof(*pGuard));
  *pGuard = 0xDEADBEEF;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_ICACHE_Init();
  MX_RTC_Init();
  MX_FileX_Init();
  /* USER CODE BEGIN 2 */

  settings_init();
  leds_init();
  mode_init();
  storage_init();
  data_acquisition_init();
  data_processor_buffers_init();
  data_processor_uac_init();
  recording_init();
  usb_handlers_init();
  trigger_init();
  sd_lowlevel_init();

  // Perform the power on startup sequence:
  leds_set(LEDS_ALL, true);
  init_startup();
  leds_set(LEDS_ALL, false);

#if 0// Handy for debugging date and time.
  // See what the date and time is:
  RTC_TimeTypeDef t1, t2;
  RTC_DateTypeDef d1, d2;
  memset(&t1, 0, sizeof(t1));
  memset(&t2, 0, sizeof(t2));
  HAL_RTC_GetTime(&hrtc, &t1, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d1, RTC_FORMAT_BIN);		// We *have* to call GetDate, otherwise the time is stuck. Duh.
  HAL_Delay(3000);
  HAL_RTC_GetTime(&hrtc, &t2, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d2, RTC_FORMAT_BIN);		// We *have* to call GetDate, otherwise the time is stuck. Duh.
#endif

  // We only need one bank of flash, so we can power down the other one. It will automatically
  // power up again we we try to access it. The size of flash has been set to 256k correspondingly
  // in the .ld file.
  HAL_FLASHEx_EnablePowerDown(FLASH_BANK_2);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  int main_tick_count = 0;
  uint32_t next_tick_count = HAL_GetTick() + MAIN_LOOP_DELAY_MS;
  while (1)
  {
	if (*pGuard != 0xDEADBEEF) {
		// The stack seems to have got out of hand:
		leds_set(LEDS_ALL, true);
		MY_BREAKPOINT();
	}

	// Various modules hook the main loop so they can do work in the main
	// thread of execution:
	mode_main_processing(main_tick_count);
	manual_mode_main_processing(main_tick_count);
	usb_mode_main_processing(main_tick_count);
	auto_mode_main_processing(main_tick_count);
	leds_main_processing(main_tick_count);
	storage_main_processing(main_tick_count);
	recording_main_processing(main_tick_count);
	sd_lowlevel_main_processing(main_tick_count);
	main_tick_count++;

	while (HAL_GetTick() < next_tick_count) {
		// Fast loop:
		usb_mode_main_fast_processing(main_tick_count);
		auto_mode_main_fast_processing(main_tick_count);
		sd_lowlevel_main_fast_processing(main_tick_count);
		// Fast loop, so we can process data buffers in time and avoid missed buffers:
		recording_main_processing(main_tick_count);

		// Beware - the following takes significant time and can get in the way of USB
		// handling unless we compile with -Ofast. An alternative is to do this only in
		// auto mode, invoked from auto.c.
		trigger_main_fast_processing(main_tick_count);
		data_processor_buffers_fast_main_processing(main_tick_count);
	}

	// Yes, the tick interval will be a little longer than specified:
	next_tick_count = HAL_GetTick() + MAIN_LOOP_DELAY_MS;

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON_RTC_ONLY;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 38;
  RCC_OscInitStruct.PLL.PLLP = 16;
  RCC_OscInitStruct.PLL.PLLQ = 16;
  RCC_OscInitStruct.PLL.PLLR = 16;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 3277;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** MCO configuration
  */
  __HAL_RCC_PLLCLKOUT_ENABLE(RCC_PLL1_DIVR);
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_PLL1CLK, RCC_MCODIV_4);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
