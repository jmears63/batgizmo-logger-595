/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "settings.h"

#define FIRMWARE_VERSION "1.1.0"	// Semantic versioning.

/*
 * When code or data access needs to be fast and deterministic, use these
 * macros to put them into the respective linker section.
 */
//#define ITCM_SECTION 		__attribute__((__section__(".itcm_text")))
//#define DTCM_SECTION 		__attribute__((__section__(".dtcmdata")))
//#define RAM_TEXT_SECTION 	__attribute__((__section__(".RamFunc")))	// Code in RAM section.

// The following match section names in the .ld script:
#define RAM_DATA_SECTION 	__attribute__((__section__(".bss")))
#define SRAM4_DATA_SECTION  __attribute__((section(".sram4")))

#define MY_BREAKPOINT() do                                                                                \
  {                                                                                                         \
    volatile uint32_t* ARM_CM_DHCSR =  ((volatile uint32_t*) 0xE000EDF0UL); /* Cortex M CoreDebug->DHCSR */ \
    if ( (*ARM_CM_DHCSR) & 1UL ) __asm("BKPT #0\n"); /* Only halt mcu if debugger is attached */            \
  } while(0)

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

// The following match section names in the .ld script:
#define RAM_DATA_SECTION 	__attribute__((__section__(".bss")))
#define SRAM4_DATA_SECTION  __attribute__((section(".sram4")))

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

// Must be short enough to keep up with writing data buffers to file - which is
// 16K samples ie 32K data, @384 kHz:
#define MAIN_LOOP_DELAY_MS 20

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define GPIO_Mode_Auto_Pin GPIO_PIN_0
#define GPIO_Mode_Auto_GPIO_Port GPIOC
#define GPIO_Mode_USB_Pin GPIO_PIN_1
#define GPIO_Mode_USB_GPIO_Port GPIOC
#define GPIO_Mode_Manual_Pin GPIO_PIN_2
#define GPIO_Mode_Manual_GPIO_Port GPIOC
#define CMD_PULLUP_Pin GPIO_PIN_3
#define CMD_PULLUP_GPIO_Port GPIOC
#define SD_Power_Enable_Pin GPIO_PIN_13
#define SD_Power_Enable_GPIO_Port GPIOB
#define DAT0_PULLUP_Pin GPIO_PIN_14
#define DAT0_PULLUP_GPIO_Port GPIOB
#define GPIO_VDDA_ENABLE_Pin GPIO_PIN_15
#define GPIO_VDDA_ENABLE_GPIO_Port GPIOB
#define GPIO_SD_DETECT_Pin GPIO_PIN_3
#define GPIO_SD_DETECT_GPIO_Port GPIOB
#define GPIO_LED_R_Pin GPIO_PIN_5
#define GPIO_LED_R_GPIO_Port GPIOB
#define GPIO_LED_Y_Pin GPIO_PIN_6
#define GPIO_LED_Y_GPIO_Port GPIOB
#define GPIO_LED_G_Pin GPIO_PIN_7
#define GPIO_LED_G_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
