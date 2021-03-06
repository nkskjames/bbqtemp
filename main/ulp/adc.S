
/* ULP assembly files are passed through C preprocessor first, so include directives
   and C macros may be used in these files 
 */
#include "soc/rtc_cntl_reg.h"
#include "soc/soc_ulp.h"

	.set adc_channel0, 7
	.set adc_channel1, 6


	/* Configure the number of ADC samples to average on each measurement.
	   For convenience, make it a power of 2. */
	.set adc_oversampling_factor_log, 2
	.set adc_oversampling_factor, (1 << adc_oversampling_factor_log)

	/* Define variables, which go into .bss section (zero-initialized data) */
	.bss

	/* Low threshold of ADC reading.
	   Set by the main program. */
	.global low_thr0
low_thr0:
	.long 0

	/* High threshold of ADC reading.
	   Set by the main program. */
	.global high_thr0
high_thr0:
	.long 0

	.global low_thr1
low_thr1:
	.long 0

	/* High threshold of ADC reading.
	   Set by the main program. */
	.global high_thr1
high_thr1:
	.long 0


	/* Counter of measurements done */
	.global sample_counter
sample_counter:
	.long 0

	.global last_result0
last_result0:
	.long 0

	.global last_result1
last_result1:
	.long 0


	/* Code goes into .text section */
	.text
	.global entry0
entry0:
	/* increment sample counter */
	move r3, sample_counter
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0

	/* do measurements using ADC */
	/* r0 will be used as accumulator */
	move r0, 0
	/* initialize the loop counter */
	stage_rst
measure0:
	/* measure and add value to accumulator */
	adc r1, 0, adc_channel0 + 1
	add r0, r0, r1
	/* increment loop counter and check exit condition */
	stage_inc 1
	jumps measure0, adc_oversampling_factor, lt

	/* divide accumulator by adc_oversampling_factor.
	   Since it is chosen as a power of two, use right shift */
	rsh r0, r0, adc_oversampling_factor_log
	/* averaged value is now in r0; store it into last_result */
	move r3, last_result0
	st r0, r3, 0

	/* compare with low_thr; wake up if value < low_thr */
	move r3, low_thr0
	ld r3, r3, 0
	sub r3, r0, r3
	jump wake_up, ov

	/* compare with high_thr; wake up if value > high_thr */
	move r3, high_thr0
	ld r3, r3, 0
	sub r3, r3, r0
	jump wake_up, ov

entry1:
	/* increment sample counter */
	move r3, sample_counter
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0

	/* do measurements using ADC */
	/* r0 will be used as accumulator */
	move r0, 0
	/* initialize the loop counter */
	stage_rst
measure1:
	/* measure and add value to accumulator */
	adc r1, 0, adc_channel1 + 1
	add r0, r0, r1
	/* increment loop counter and check exit condition */
	stage_inc 1
	jumps measure1, adc_oversampling_factor, lt

	/* divide accumulator by adc_oversampling_factor.
	   Since it is chosen as a power of two, use right shift */
	rsh r0, r0, adc_oversampling_factor_log
	/* averaged value is now in r0; store it into last_result */
	move r3, last_result1
	st r0, r3, 0

	/* compare with low_thr; wake up if value < low_thr */
	move r3, low_thr1
	ld r3, r3, 0
	sub r3, r0, r3
	jump wake_up, ov

	/* compare with high_thr; wake up if value > high_thr */
	move r3, high_thr1
	ld r3, r3, 0
	sub r3, r3, r0
	jump wake_up, ov


	/* value within range, end the program */
	.global exit
exit:
	halt

	.global wake_up
wake_up:
	/* Check if the system can be woken up */
	READ_RTC_REG(RTC_CNTL_DIAG0_REG, 19, 1)
	and r0, r0, 1
	jump exit, eq

	/* Wake up the SoC, end program */
	wake
	WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
	halt
