/* Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dal_services.h"

#include "calc_pll_clock_source.h"
#include "include/bios_parser_interface.h"
#include "include/logger_interface.h"

/**
* Function: calculate_fb_and_fractional_fb_divider
*
* * DESCRIPTION: Calculates feedback and fractional feedback dividers values
*
*PARAMETERS:
* targetPixelClock             Desired frequency in 10 KHz
* ref_divider                  Reference divider (already known)
* postDivider                  Post Divider (already known)
* feedback_divider_param       Pointer where to store
*					calculated feedback divider value
* fract_feedback_divider_param Pointer where to store
*					calculated fract feedback divider value
*
*RETURNS:
* It fills the locations pointed by feedback_divider_param
*					and fract_feedback_divider_param
* It returns	- true if feedback divider not 0
*		- false should never happen)
*/
static bool calculate_fb_and_fractional_fb_divider(
		struct calc_pll_clock_source *calc_pll_cs,
		uint32_t target_pix_clk_khz,
		uint32_t ref_divider,
		uint32_t post_divider,
		uint32_t *feedback_divider_param,
		uint32_t *fract_feedback_divider_param)
{
	uint64_t feedback_divider;

	feedback_divider =
		(uint64_t)(target_pix_clk_khz * ref_divider * post_divider);
	feedback_divider *= 10;
	/* additional factor, since we divide by 10 afterwards */
	feedback_divider *= (uint64_t)(calc_pll_cs->fract_fb_divider_factor);
	feedback_divider = div_u64(feedback_divider, calc_pll_cs->ref_freq_khz);

/*Round to the number of precision
 * The following code replace the old code (ullfeedbackDivider + 5)/10
 * for example if the difference between the number
 * of fractional feedback decimal point and the fractional FB Divider precision
 * is 2 then the equation becomes (ullfeedbackDivider + 5*100) / (10*100))*/

	feedback_divider += (uint64_t)
			(5 * calc_pll_cs->fract_fb_divider_precision_factor);
	feedback_divider =
		div_u64(feedback_divider,
			calc_pll_cs->fract_fb_divider_precision_factor * 10);
	feedback_divider *= (uint64_t)
			(calc_pll_cs->fract_fb_divider_precision_factor);

	*feedback_divider_param =
		div_u64_rem(
			feedback_divider,
			calc_pll_cs->fract_fb_divider_factor,
			fract_feedback_divider_param);

	if (*feedback_divider_param != 0)
		return true;
	return false;
}

/**
*calc_fb_divider_checking_tolerance
*
*DESCRIPTION: Calculates Feedback and Fractional Feedback divider values
*		for passed Reference and Post divider, checking for tolerance.
*PARAMETERS:
* pll_settings		Pointer to structure
* ref_divider		Reference divider (already known)
* postDivider		Post Divider (already known)
* tolerance		Tolerance for Calculated Pixel Clock to be within
*
*RETURNS:
* It fills the PLLSettings structure with PLL Dividers values
* if calculated values are within required tolerance
* It returns	- true if eror is within tolerance
*		- false if eror is not within tolerance
*/
static bool calc_fb_divider_checking_tolerance(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings,
		uint32_t ref_divider,
		uint32_t post_divider,
		uint32_t tolerance)
{
	uint32_t feedback_divider;
	uint32_t fract_feedback_divider;
	uint32_t actual_calculated_clock_khz;
	uint32_t abs_err;
	uint64_t actual_calc_clk_khz;

	calculate_fb_and_fractional_fb_divider(
			calc_pll_cs,
			pll_settings->adjusted_pix_clk,
			ref_divider,
			post_divider,
			&feedback_divider,
			&fract_feedback_divider);

	/*Actual calculated value*/
	actual_calc_clk_khz = (uint64_t)(feedback_divider *
					calc_pll_cs->fract_fb_divider_factor) +
							fract_feedback_divider;
	actual_calc_clk_khz *= calc_pll_cs->ref_freq_khz;
	actual_calc_clk_khz =
		div_u64(actual_calc_clk_khz,
			ref_divider * post_divider *
				calc_pll_cs->fract_fb_divider_factor);

	actual_calculated_clock_khz = (uint32_t)(actual_calc_clk_khz);

	abs_err = (actual_calculated_clock_khz >
					pll_settings->adjusted_pix_clk)
			? actual_calculated_clock_khz -
					pll_settings->adjusted_pix_clk
			: pll_settings->adjusted_pix_clk -
						actual_calculated_clock_khz;

	if (abs_err <= tolerance) {
		/*found good values*/
		pll_settings->reference_freq = calc_pll_cs->ref_freq_khz;
		pll_settings->reference_divider = ref_divider;
		pll_settings->feedback_divider = feedback_divider;
		pll_settings->fract_feedback_divider = fract_feedback_divider;
		pll_settings->pix_clk_post_divider = post_divider;
		pll_settings->calculated_pix_clk =
			actual_calculated_clock_khz;
		pll_settings->vco_freq =
			actual_calculated_clock_khz * post_divider;
		return true;
	}
	return false;
}

static bool calc_pll_dividers_in_range(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings,
		uint32_t min_ref_divider,
		uint32_t max_ref_divider,
		uint32_t min_post_divider,
		uint32_t max_post_divider,
		uint32_t err_tolerance)
{
	uint32_t ref_divider;
	uint32_t post_divider;
	uint32_t tolerance;

/* This is err_tolerance / 10000 = 0.0025 - acceptable error of 0.25%
 * This is errorTolerance / 10000 = 0.0001 - acceptable error of 0.01%*/
	tolerance = (pll_settings->adjusted_pix_clk * err_tolerance) /
									10000;
	if (tolerance < CALC_PLL_CLK_SRC_ERR_TOLERANCE)
		tolerance = CALC_PLL_CLK_SRC_ERR_TOLERANCE;

	for (
			post_divider = max_post_divider;
			post_divider >= min_post_divider;
			--post_divider) {
		for (
				ref_divider = min_ref_divider;
				ref_divider <= max_ref_divider;
				++ref_divider) {
			if (calc_fb_divider_checking_tolerance(
					calc_pll_cs,
					pll_settings,
					ref_divider,
					post_divider,
					tolerance)) {
				return true;
			}
		}
	}

	return false;
}

uint32_t dal_clock_source_calculate_pixel_clock_pll_dividers(
		struct calc_pll_clock_source *calc_pll_cs,
		struct pll_settings *pll_settings)
{
	uint32_t err_tolerance;
	uint32_t min_post_divider;
	uint32_t max_post_divider;
	uint32_t min_ref_divider;
	uint32_t max_ref_divider;

	if (pll_settings->adjusted_pix_clk == 0) {
		dal_logger_write(calc_pll_cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s Bad requested pixel clock", __func__);
		return MAX_PLL_CALC_ERROR;
	}

/* 1) Find Post divider ranges */
	if (pll_settings->pix_clk_post_divider) {
		min_post_divider = pll_settings->pix_clk_post_divider;
		max_post_divider = pll_settings->pix_clk_post_divider;
	} else {
		min_post_divider = calc_pll_cs->min_pix_clock_pll_post_divider;
		if (min_post_divider * pll_settings->adjusted_pix_clk <
						calc_pll_cs->min_vco_khz) {
			min_post_divider = calc_pll_cs->min_vco_khz /
					pll_settings->adjusted_pix_clk;
			if ((min_post_divider *
					pll_settings->adjusted_pix_clk) <
						calc_pll_cs->min_vco_khz)
				min_post_divider++;
		}

		max_post_divider = calc_pll_cs->max_pix_clock_pll_post_divider;
		if (max_post_divider * pll_settings->adjusted_pix_clk
				> calc_pll_cs->max_vco_khz)
			max_post_divider = calc_pll_cs->max_vco_khz /
					pll_settings->adjusted_pix_clk;
	}

/* 2) Find Reference divider ranges
 * When SS is enabled, or for Display Port even without SS,
 * pll_settings->referenceDivider is not zero.
 * So calculate PPLL FB and fractional FB divider
 * using the passed reference divider*/

	if (pll_settings->reference_divider) {
		min_ref_divider = pll_settings->reference_divider;
		max_ref_divider = pll_settings->reference_divider;
	} else {
		min_ref_divider = ((calc_pll_cs->ref_freq_khz
				/ calc_pll_cs->max_pll_input_freq_khz)
				> calc_pll_cs->min_pll_ref_divider)
			? calc_pll_cs->ref_freq_khz
					/ calc_pll_cs->max_pll_input_freq_khz
			: calc_pll_cs->min_pll_ref_divider;

		max_ref_divider = ((calc_pll_cs->ref_freq_khz
				/ calc_pll_cs->min_pll_input_freq_khz)
				< calc_pll_cs->max_pll_ref_divider)
			? calc_pll_cs->ref_freq_khz /
					calc_pll_cs->min_pll_input_freq_khz
			: calc_pll_cs->max_pll_ref_divider;
	}

/* If some parameters are invalid we could have scenario when  "min">"max"
 * which produced endless loop later.
 * We should investigate why we get the wrong parameters.
 * But to follow the similar logic when "adjustedPixelClock" is set to be 0
 * it is better to return here than cause system hang/watchdog timeout later.
 *  ## SVS Wed 15 Jul 2009 */

	if (min_post_divider > max_post_divider) {
		dal_logger_write(calc_pll_cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s Post divider range is invalid", __func__);
		return MAX_PLL_CALC_ERROR;
	}

	if (min_ref_divider > max_ref_divider) {
		dal_logger_write(calc_pll_cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"%s Reference divider range is invalid", __func__);
		return MAX_PLL_CALC_ERROR;
	}

/* 3) Try to find PLL dividers given ranges
 * starting with minimal error tolerance.
 * Increase error tolerance until PLL dividers found*/
	err_tolerance = MAX_PLL_CALC_ERROR;

	while (!calc_pll_dividers_in_range(
			calc_pll_cs,
			pll_settings,
			min_ref_divider,
			max_ref_divider,
			min_post_divider,
			max_post_divider,
			err_tolerance))
		err_tolerance += (err_tolerance > 10)
				? (err_tolerance / 10)
				: 1;

	return err_tolerance;
}

static bool calc_pll_clock_source_max_vco_construct(
			struct calc_pll_clock_source *calc_pll_cs,
			struct calc_pll_clock_source_init_data *init_data)
{

	uint32_t i;
	struct firmware_info fw_info = { { 0 } };
	if (calc_pll_cs == NULL ||
			init_data == NULL ||
			init_data->bp == NULL)
		return false;

	if (dal_bios_parser_get_firmware_info(
				init_data->bp,
				&fw_info) != BP_RESULT_OK)
		return false;

	calc_pll_cs->ctx = init_data->ctx;
	calc_pll_cs->ref_freq_khz = fw_info.pll_info.crystal_frequency;
	calc_pll_cs->min_vco_khz =
			fw_info.pll_info.min_output_pxl_clk_pll_frequency;
	calc_pll_cs->max_vco_khz =
			fw_info.pll_info.max_output_pxl_clk_pll_frequency;

	if (init_data->max_override_input_pxl_clk_pll_freq_khz != 0)
		calc_pll_cs->max_pll_input_freq_khz =
			init_data->max_override_input_pxl_clk_pll_freq_khz;
	else
		calc_pll_cs->max_pll_input_freq_khz =
			fw_info.pll_info.max_input_pxl_clk_pll_frequency;

	if (init_data->min_override_input_pxl_clk_pll_freq_khz != 0)
		calc_pll_cs->min_pll_input_freq_khz =
			init_data->min_override_input_pxl_clk_pll_freq_khz;
	else
		calc_pll_cs->min_pll_input_freq_khz =
			fw_info.pll_info.min_input_pxl_clk_pll_frequency;

	calc_pll_cs->min_pix_clock_pll_post_divider =
			init_data->min_pix_clk_pll_post_divider;
	calc_pll_cs->max_pix_clock_pll_post_divider =
			init_data->max_pix_clk_pll_post_divider;
	calc_pll_cs->min_pll_ref_divider =
			init_data->min_pll_ref_divider;
	calc_pll_cs->max_pll_ref_divider =
			init_data->max_pll_ref_divider;

	if (init_data->num_fract_fb_divider_decimal_point == 0 ||
		init_data->num_fract_fb_divider_decimal_point_precision >
				init_data->num_fract_fb_divider_decimal_point) {
		dal_logger_write(calc_pll_cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"The dec point num or precision is incorrect!");
		return false;
	}
	if (init_data->num_fract_fb_divider_decimal_point_precision == 0) {
		dal_logger_write(calc_pll_cs->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_GPU,
			"Incorrect fract feedback divider precision num!");
		return false;
	}

	calc_pll_cs->fract_fb_divider_decimal_points_num =
				init_data->num_fract_fb_divider_decimal_point;
	calc_pll_cs->fract_fb_divider_precision =
			init_data->num_fract_fb_divider_decimal_point_precision;
	calc_pll_cs->fract_fb_divider_factor = 1;
	for (i = 0; i < calc_pll_cs->fract_fb_divider_decimal_points_num; ++i)
		calc_pll_cs->fract_fb_divider_factor *= 10;

	calc_pll_cs->fract_fb_divider_precision_factor = 1;
	for (
		i = 0;
		i < (calc_pll_cs->fract_fb_divider_decimal_points_num -
				calc_pll_cs->fract_fb_divider_precision);
		++i)
		calc_pll_cs->fract_fb_divider_precision_factor *= 10;

	return true;
}

bool dal_calc_pll_clock_source_max_vco_init(
		struct calc_pll_clock_source *calc_pll_cs,
		struct calc_pll_clock_source_init_data *init_data)
{
	return calc_pll_clock_source_max_vco_construct(
			calc_pll_cs, init_data);
}

