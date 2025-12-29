#include "data_acquisition.h"
#include "main.h"
#include "adc.h"
#include <arm_math.h>

#include "storage.h"
#include "leds.h"
#include "settings.h"
#include "gain.h"


// Round up a value to a multiple of 32 bytes:
#define ROUNDUP32(x, size) (((x - 1) / (32 / size) + 1) * (32 / size))

#define MAXIMUM_FRAME_LENGTH SAMPLES_PER_FRAME

// Scale down limits need to be rather conservative, because
// the ADC recedes from its limits when heavily overloaded:
#define SCALE_DOWN_DELTA 0x6000	// Conservative so we scale in good time to avoid clipping - perhaps.
#define SCALE_DOWN_THRESHOLD_UPPER (SCALE_DOWN_DELTA)	// Determines when we auto range to less sensitive.
#define SCALE_DOWN_THRESHOLD_LOWER (-SCALE_DOWN_DELTA)

#define BLINK_LEDS 1

/*
 * IMPORTANT:
 *
 *  For the DMA buffer:
 *  * Allow for a canary word (32 bit) at the start and end of the buffer.
 *  * Make sure the buffers are 32 bytes aligned and multiples of 32 bytes long
 *  	so they don't share cache lines with other data, to avoid surprises when
 *  	doing an explicit cache clean or invalidate.
 *  * DMA writes to the buffers behind the cache, so code needs to invalidate cache to get
 *      at the most recent valid data.
 *
 *  See https://community.st.com/s/article/FAQ-DMA-is-not-working-on-STM32H7-devices for more
 *  on data consistency with when using DMA.
 *
 *  The DMA buffers are specifically not in DTCM, as the DMA controllers can't access DTCM.
 */

#define GUARD_VALUE 0x0778			// Recognisable value for guard bytes.

/*
 * We add extra guard elements (32 bits) to the end of each array, so that we can check for overruns.
 */
#define DMABUFFER_GUARD_OFFSET SAMPLES_PER_FRAME
#define DMABUFFER_GUARD_COUNT 2		// 32 bits worth.

RAM_DATA_SECTION dma_buffer_type_t g_dmabuffer1[ROUNDUP32(SAMPLES_PER_FRAME + DMABUFFER_GUARD_COUNT, sizeof(dma_buffer_type_t))] __ALIGNED(32);
// SRAM4_DATA_SECTION dma_buffer_type_t dmabuffer4[ROUNDUP32(SAMPLES_PER_FRAME + DMABUFFER_GUARD_COUNT, sizeof(dma_buffer_type_t))] __ALIGNED(32);

// Stuff relating to DSP using the library CMSIS:

#define DO_BIQUAD 0		// Don't enable this, it will probably result in interrupt contention. We should do it in the main loop.
#if DO_BIQUAD

#define DSP_STAGES 	1		// Number of biquads we apply.
#define HIGH_PRECISION_BIQUAD 0

#if HIGH_PRECISION_BIQUAD
// The Fortnum and Mason's biquad - 32 bit data, 64 bit intermediates.
#define NUM_BIQUAD_COEFFICIENTS (5 * DSP_STAGES)
#define BIQUAD_STATE_VARS (4  * DSP_STAGES)
#define BIQUAD_INIT(a,b,c,d,e) arm_biquad_cas_df1_32x64_init_q31(a,b,c,d,e)
#define BIQUAD_STATE_TYPE q63_t
#define BIQUAD_INSTANCE_TYPE arm_biquad_cas_df1_32x64_ins_q31
#define BIQUAD_PROCESS(a,b,c,d) arm_biquad_cas_df1_32x64_q31(a,b,c,d)
#else
// Tesco value biquad - 32 bit data, compromises for speed. Seems to work OK.
#define NUM_BIQUAD_COEFFICIENTS (5 * DSP_STAGES)
#define BIQUAD_STATE_VARS (4  * DSP_STAGES)
#define BIQUAD_INIT(a,b,c,d,e) arm_biquad_cascade_df1_init_q31(a,b,c,d,e)
#define BIQUAD_STATE_TYPE q31_t
#define BIQUAD_INSTANCE_TYPE arm_biquad_casd_df1_inst_q31
#define BIQUAD_PROCESS(a,b,c,d)  arm_biquad_cascade_df1_fast_q31(a,b,c,d)
#endif

static BIQUAD_STATE_TYPE biquad_state1[BIQUAD_STATE_VARS];
static BIQUAD_INSTANCE_TYPE biquad_hpf_instance;

// Earlevel variable naming. Note that this generates *5* coefficients, as required by
// the precision we are using. If you change the precision, check if this is still correct
// as it differs.
#define BIQUAD_COEFFS(a0, a1, a2, b1, b2, normalizer) \
		a0 / normalizer,		\
		a1 / normalizer,		\
		a2 / normalizer,		\
		-b1 / normalizer,		\
		-b2 / normalizer

static q31_t s_raw_buffer_q31[SAMPLES_PER_FRAME];
static q31_t s_filtered_buffer1_q31[SAMPLES_PER_FRAME];
static q15_t s_filtered_buffer1_q15[SAMPLES_PER_FRAME];

#endif

static sample_type_t s_raw_buffer_q15[SAMPLES_PER_FRAME];

static data_processor_t s_data_processor = NULL;

static int s_signal_offset_correction = 0;
static bool s_enable_capture = false;

static void process_half_frame(bool is_first_half, const dma_buffer_type_t *dmabuffer,
		sample_type_t offset, int leftshift);

volatile sample_type_t *g_raw_half_frame = NULL;
volatile int g_raw_half_frame_counter = 0;
volatile bool g_raw_half_frame_ready = false;

/*
 * Here are the DMA complete and half complete interrupts handlers.
 *
 * Their job is to copy the fresh data from the DMA buffer to another
 * buffer, in DTCM, before it gets overwritten by the next DMA cycle.
 */

static int s_conv_counter = 0;

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
	if (s_enable_capture) {
		if (hadc == &hadc1)
		{
			process_half_frame(true, g_dmabuffer1, ACQUISITION_OFFSET, ACQUISITION_LEFTSHIFT);
		}
#if ADC4_PRESENT
		else if (hadc == &hadc4)
		{
			process_half_frame(true, dmabuffer4, MONITOR_OFFSET, MONITOR_LEFTSHIFT);
		}
#endif
	}
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
	if (s_enable_capture) {
		if (hadc == &hadc1)
		{
			process_half_frame(false, g_dmabuffer1, ACQUISITION_OFFSET, ACQUISITION_LEFTSHIFT);
		}
#if ADC4_PRESENT
		else if (hadc == &hadc4)
		{
			process_half_frame(false, dmabuffer4, MONITOR_OFFSET, MONITOR_LEFTSHIFT);
		}
#endif
	}

	s_conv_counter++;
}

int data_acquisition_get_conv_counter(void)
{
	return s_conv_counter;
}

void data_acquisition_set_signal_offset_correction(int correction)
{
	s_signal_offset_correction = correction;
}

void data_acquisition_enable_capture(bool flag) {
	s_enable_capture = flag;
}

void data_acquisition_init(void)
{
	s_data_processor = NULL;
	data_acquisition_reset();

#if DO_BIQUAD

    // The following two values need to match. They also need to be more conservative that the CMSIS
    // docs suggest, to avoid something like integrator windup and wrapping of output values.
    const int postshift = 1;
    const float32_t coeff_scaling = 2.0;

    static const float32_t float_biquad_coefficients[NUM_BIQUAD_COEFFICIENTS] = {

		/*
		 * y[n] = b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
		 * https://www.earlevel.com/main/2013/10/13/biquad-calculator-v2/
		 * Note that we need to swap a/b and change the sign of the resulting a coeffs to get to CMSIS. The
		 * macro does that for us.
		 */

    		BIQUAD_COEFFS(0.9437902064384665, -1.8875804128769329, 0.9437902064384665, -1.8844183723596442, 0.8907424533942211, coeff_scaling)
			// From filter-design.R based on 5.0 kHz, fs=384000
    };

    // NB the following MUST be static data:
    static q31_t q31_biquad_coefficients[NUM_BIQUAD_COEFFICIENTS];
    arm_float_to_q31(float_biquad_coefficients, q31_biquad_coefficients, NUM_BIQUAD_COEFFICIENTS);
	/*
	 * Multiply by 2 after biquad, as we halved the coefficients.
	 * That means we lost a bit of resolution, but it was almost certainly noise.
	 *
     * We use the Fortnum and Mason version of 32 bit IIR so that we can HPF accurately at a low frequency
     * when the sample rate is high.
	*/
    BIQUAD_INIT(&biquad_hpf_instance, DSP_STAGES, q31_biquad_coefficients, biquad_state1, postshift);
#endif
}

void data_acquisition_reset(void) {
	s_conv_counter = 0;
	s_signal_offset_correction = 0;
	s_enable_capture = false;
	g_raw_half_frame = NULL;
	g_raw_half_frame_counter = 0;
	g_raw_half_frame_ready = false;

	memset(g_dmabuffer1, '\0', sizeof(g_dmabuffer1));
	// memset(dmabuffer4, '\0', sizeof(dmabuffer4));
}

void data_acquisition_set_processor(data_processor_t processor)
{
	s_data_processor = processor;
}

#if 0
static uint16_t v_s = 0;
#endif

static void process_half_frame(bool is_first_half, const dma_buffer_type_t *dmabuffer,
		sample_type_t offset, int leftshift)
{
	// A half DMA buffer is ready for us:
	const int buffer_offset = is_first_half ? 0 : HALF_SAMPLES_PER_FRAME;
	const int samples_to_process = HALF_SAMPLES_PER_FRAME;

	// Basic scale and offset to end up with sample_type_t:
	// TODO consider replacing the following with CMSIS vector operations, or writing our own composite one.
	bool overload_detected = false;
	const dma_buffer_type_t *pSource = dmabuffer + buffer_offset;
	sample_type_t *pDest = s_raw_buffer_q15 + buffer_offset;
	for (int i = 0; i < HALF_SAMPLES_PER_FRAME; i++) {
		uint16_t value = *pSource++;

#if 0
		// Hack for testing with a saw tooth:
		value = v_s++;
#endif
		sample_type_t scaled_value = ((value - (dma_buffer_type_t) offset) << leftshift) - s_signal_offset_correction;
		*pDest++ = scaled_value;
		if (scaled_value > SCALE_DOWN_THRESHOLD_UPPER || scaled_value < SCALE_DOWN_THRESHOLD_LOWER)
			overload_detected = true;
	}

	if (overload_detected) {
#if BLINK_LEDS
		leds_single_blink(LED_RED, 1);
#endif
	}

	// Flag globally that a raw data buffer is ready:
	g_raw_half_frame = s_raw_buffer_q15 + buffer_offset;
	g_raw_half_frame_counter++;
	g_raw_half_frame_ready = true;


#if DO_BIQUAD
	// Do some DSP. Beware - biquad chews CPU.
	arm_q15_to_q31(s_raw_buffer_q15 + buffer_offset, s_raw_buffer_q31 + buffer_offset, samples_to_process);
	BIQUAD_PROCESS(&biquad_hpf_instance, s_raw_buffer_q31 + buffer_offset, s_filtered_buffer1_q31 + buffer_offset, samples_to_process);
	arm_q31_to_q15(s_filtered_buffer1_q31 + buffer_offset, s_filtered_buffer1_q15 + buffer_offset, samples_to_process);
	const sample_type_t *pBufferToUse = s_filtered_buffer1_q15;
#else
	(void) samples_to_process;
	const sample_type_t *pBufferToUse = s_raw_buffer_q15;
#endif

	// Pass the data through to the processor:
	if (s_data_processor != NULL) {
		s_data_processor(pBufferToUse, buffer_offset);
	}

// TODO investigate this further. USB interrupt can show as pending even though it has more priority (0).
#if 0
    uint32_t p = NVIC_GetPriority(USB_IRQn);
    if (p != 0) {
    	MY_BREAKPOINT();
    }

    p = NVIC_GetPriority(GPDMA1_Channel0_IRQn);
    if (p != 1) {
    	MY_BREAKPOINT();
    }

    bool pending = NVIC_GetPendingIRQ(USB_IRQn);
    if (pending != false) {
    	MY_BREAKPOINT();
    }

    pending = NVIC_GetPendingIRQ(GPDMA1_Channel0_IRQn);
    if (pending != false) {
    	MY_BREAKPOINT();
    }
#endif

}
