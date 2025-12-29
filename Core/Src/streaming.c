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

#include <data_acquisition.h>
#include "streaming.h"

#include "gain.h"
#include "tim.h"
#include "adc.h"
#include "gpdma.h"
//#include "opamp.h"
#include "spi.h"
#include "tim.h"
#include "spi.h"
#include "settings.h"


void streaming_start(void)
{
	// Enable master clock output (MCO), dividing PLL1 output by 4:
	// 16 * 24 / 10 /4 -> 9.6 MHz.
	// This isn't sufficient, we also need to initialize PLL1 which ioc doesn't do
	// unless MCO is enable in ioc.
	// HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_PLL1CLK, RCC_MCODIV_4);

	// Enable analogue power. Do this early otherwise the PGA is not
	// able to accept data over SPI:
	HAL_GPIO_WritePin(GPIO_VDDA_ENABLE_GPIO_Port, GPIO_VDDA_ENABLE_Pin, GPIO_PIN_SET);	// + 2.5 mA

	// This order of initialization is based on generated code from ioc:
	MX_ADC1_Init();
	MX_SPI1_Init();
	MX_TIM2_Init();

	// An additional delay before sending the gain to the PGA is prudent
	// though seems to be unnecessary as long as the power is enabled
	// early in the sequence above:
	HAL_Delay(10);
	gain_init();
	gain_set(settings_get()->sensitivity_range, settings_get()->sensitivity_disable);

	// Possibly not needed but it seems cleanest not to be triggering the ADC during setup:
	HAL_TIM_Base_Stop(&htim2);

	// uint32_t cal_buffer_before[9];
	// HAL_ADCEx_LinearCalibration_GetValue(&hadc1, cal_buffer_before);

	// Calibrate the ADC whenever we use it:
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET_LINEARITY, ADC_DIFFERENTIAL_ENDED);

	data_acquisition_reset();

	// Start the ADC->DMA:
	HAL_ADC_Start_DMA(&hadc1, (uint32_t *) g_dmabuffer1, SAMPLES_PER_FRAME);

	// Kick off triggering:
	HAL_TIM_Base_Start(&htim2);			// Use HAL_TIM_Base_Start_IT if you want interrupts. Not needed in this design.

	// TODO: offset measurement per PoC code OR high pass IIR filter.

/*
	uint32_t cal_buffer_after[9];
	HAL_ADCEx_LinearCalibration_GetValue(&hadc1, cal_buffer_after);

	memset(cal_buffer_after, 0, sizeof(cal_buffer_after));
	HAL_ADCEx_LinearCalibration_SetValue(&hadc1, cal_buffer_after);
	*/
}

void streaming_stop(void)
{
	// Stop the peripherals:
	HAL_TIM_Base_Stop(&htim2);
	HAL_ADC_Stop_DMA(&hadc1);
	//HAL_OPAMP_Stop(&hopamp1);
	// Disable analogue power:
	HAL_GPIO_WritePin(GPIO_VDDA_ENABLE_GPIO_Port, GPIO_VDDA_ENABLE_Pin, GPIO_PIN_RESET);

	// Deinit the peripherals:
	//HAL_OPAMP_DeInit(&hopamp1);
	HAL_TIM_Base_DeInit(&htim2);
	HAL_SPI_DeInit(&hspi1);
	HAL_ADC_DeInit(&hadc1);
}
