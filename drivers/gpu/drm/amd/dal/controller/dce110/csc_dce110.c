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

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/fixed31_32.h"

#include "../csc.h"

#include "csc_dce110.h"
#include "csc_grph_dce110.h"

static bool csc_dce110_construct(
	struct csc_dce110 *csc,
	const struct csc_init_data *init_data);

struct csc *dal_csc_dce110_create(
	const struct csc_init_data *init_data)
{
	struct csc_dce110 *csc = dal_alloc(sizeof(struct csc_dce110));

	if (!csc)
		return NULL;

	if (csc_dce110_construct(csc, init_data))
		return &csc->base;

	dal_free(csc);
	ASSERT_CRITICAL(false);
	return NULL;
}

#define FROM_CSC(csc)\
	container_of(csc, struct csc_dce110, base)

static void csc_dce110_destruct(struct csc_dce110 *csc);

static void destroy(struct csc **csc)
{
	csc_dce110_destruct(FROM_CSC(*csc));
	dal_free(FROM_CSC(*csc));
	*csc = NULL;
}

static void csc_dce110_destruct(struct csc_dce110 *csc)
{
	csc->base.csc_grph->funcs->destroy(csc->base.csc_grph);
	dal_dcp_bit_depth_reduction_dce110_destroy(
		&csc->dcp_bit_depth_reduction);
}

static void set_denormalization(
	struct csc *csc,
	enum csc_color_depth display_color_depth,
	uint32_t lb_color_depth);

static void set_grph_csc_default(
	struct csc *csc,
	const struct default_adjustment *adjust)
{
	csc->csc_grph->funcs->set_grph_csc_default(csc->csc_grph, adjust);

	set_denormalization(csc, adjust->color_depth,
		adjust->lb_color_depth);

	/* program dcp bit depth reduction */
	dal_dcp_bit_depth_reduction_dce110_program(
		FROM_CSC(csc)->dcp_bit_depth_reduction,
		adjust->color_depth);
}

static void set_grph_csc_adjustment(
	struct csc *csc,
	const struct grph_csc_adjustment *adjust)
{
	/* color adjustment */
	csc->csc_grph->funcs->set_grph_csc_adjustment(csc->csc_grph, adjust);

	set_denormalization(csc, adjust->color_depth, adjust->lb_color_depth);

	/* program dcp bit depth reduction */
	dal_dcp_bit_depth_reduction_dce110_program(
		FROM_CSC(csc)->dcp_bit_depth_reduction,
		adjust->color_depth);
}

static void set_overscan_color_black(
	struct csc *csc,
	enum color_space black_color)
{
	csc->csc_grph->funcs->
	set_overscan_color_black(csc->csc_grph, black_color);
}

static void set_ovl_csc_adjustment(
	struct csc *csc,
	const struct ovl_csc_adjustment *adjust,
	enum color_space color_space)
{
}

/*******************************************************************************
 * Method: set_denormalization
 *
 * The method converts the output data from internal floating point format
 * to fixed point determined by the required output color depth.
 *
 *  @param [in] enum csc_color_depth display_color_depth
 *
 *  @return
 *     void
 *
 *  @note
DENORM_MODE 2:0 0x3 De-normalization mode
 POSSIBLE VALUES:
      00 - unity
      01 - 63/64 for 6bit
      02 - 255/256 for 8bit
      03 - 1023/1024 for 10bit
      04 - 2047/2048 for 11bit
      05 - 4095/4096 for 12 bit
      06 - reserved
      07 - reserved
DENORM_14BIT_OUT 4 0x0  POSSIBLE VALUES:
      00 - De-norm output to 12-bit
      01 - De-norm output to 14-bit  *
      @see
      1. If the call goes from set mode context then lbColorDepth has a
       meaningful value 6, 8, 10 or 12.
       We are allowed to change lb format in set mode context
      2. If the call goes from set adjustment context the lbColorDepth is 0
       because we do not change lb format on the fly
 *
 ****************************************************************************/
static void set_denormalization(
	struct csc *csc,
	enum csc_color_depth display_color_depth,
	uint32_t lb_color_depth)
{
	uint32_t value = dal_read_reg(csc->ctx,
			FROM_CSC(csc)->dcp_denorm_control_offset);

	switch (display_color_depth) {
	case CSC_COLOR_DEPTH_666:
		/* 63/64 for 6 bit output color depth */
		set_reg_field_value(
			value,
			1,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_888:
		/* Unity for 8 bit output color depth
		 * because prescale is disabled by default */
		set_reg_field_value(
			value,
			0,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_101010:
		/* 1023/1024 for 10 bit output color depth */
		set_reg_field_value(
			value,
			3,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_111111:
		/* 1023/1024 for 11 bit output color depth */
		set_reg_field_value(
			value,
			4,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_121212:
		/* 4095/4096 for 12 bit output color depth */
		set_reg_field_value(
			value,
			5,
			DENORM_CONTROL,
			DENORM_MODE);
		break;
	case CSC_COLOR_DEPTH_141414:
	case CSC_COLOR_DEPTH_161616:
	default:
		/* not valid used case! */
		break;
	}

	if (display_color_depth == CSC_COLOR_DEPTH_121212 &&
		lb_color_depth == 10) {
		set_reg_field_value(
			value,
			1,
			DENORM_CONTROL,
			DENORM_14BIT_OUT);
	} else {
		set_reg_field_value(
			value,
			0,
			DENORM_CONTROL,
			DENORM_14BIT_OUT);
	}

	dal_write_reg(csc->ctx,
		FROM_CSC(csc)->dcp_denorm_control_offset, value);

}

/*******************************************************************************
 * is_supported_custom_gamut_adjustment
 *
 * @return supported always for NI family
 *
 ******************************************************************************/

static bool is_supported_custom_gamut_adjustment(struct csc *csc)
{
	return true;
}
/*******************************************************************************
 * is_supported_overlay_alfa_adjustment
 *
 * Check if hardware supports ovl alfa blending
 *
 *  @return true always for Evergreen , but for NI false;
 *
 ******************************************************************************/
static bool is_supported_overlay_alpha_adjustment(struct csc *csc)
{
	return false;
}

static const struct csc_funcs csc_dce110_funcs = {
	.set_grph_csc_default = set_grph_csc_default,
	.set_grph_csc_adjustment = set_grph_csc_adjustment,
	.set_overscan_color_black = set_overscan_color_black,
	.set_ovl_csc_adjustment = set_ovl_csc_adjustment,
	.is_supported_custom_gamut_adjustment =
		is_supported_custom_gamut_adjustment,
	.is_supported_overlay_alpha_adjustment =
		is_supported_overlay_alpha_adjustment,
	.set_input_csc = dal_csc_set_input_csc,
	.destroy = destroy,
};

static bool csc_dce110_construct(
	struct csc_dce110 *csc,
	const struct csc_init_data *init_data)
{
	if (!dal_csc_construct(&csc->base, init_data))
		return false;

	switch (init_data->id) {
	case CONTROLLER_ID_D0:
		csc->dcp_denorm_control_offset = mmDCP0_DENORM_CONTROL;
		break;
	case CONTROLLER_ID_D1:
		csc->dcp_denorm_control_offset = mmDCP1_DENORM_CONTROL;
		break;
	case CONTROLLER_ID_D2:
		csc->dcp_denorm_control_offset = mmDCP2_DENORM_CONTROL;
		break;
	default:
		ASSERT_CRITICAL(false); /* invalid matrix id ! */
		return false;
	}

	csc->dcp_bit_depth_reduction =
		dal_dcp_bit_depth_reduction_dce110_create(
			init_data->id, csc->base.ctx, init_data->as);

	if (!csc->dcp_bit_depth_reduction)
		return false;

	{
		struct csc_grph_init_data cg_init_data;

		cg_init_data.ctx = csc->base.ctx;
		cg_init_data.id = init_data->id;
		csc->base.csc_grph =
			dal_csc_grph_dce110_create(&cg_init_data);
	}

	if (!csc->base.csc_grph)
			goto fail_csc_grph;

	csc->base.funcs = &csc_dce110_funcs;
	return true;

fail_csc_grph:
	dal_dcp_bit_depth_reduction_dce110_destroy(
		&csc->dcp_bit_depth_reduction);
	return false;
}
