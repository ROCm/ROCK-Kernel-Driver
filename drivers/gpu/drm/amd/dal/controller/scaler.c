/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "scaler.h"

#define UP_SCALER_RATIO_MAX 16000
#define DOWN_SCALER_RATIO_MAX 250
#define SCALER_RATIO_DIVIDER 1000

static const struct scaler_taps_and_ratio downscaling_data[] = {
	{ 4, 4, 660, 1000 },
	{ 6, 5, 510, 659 },
	{ 6, 6, 460, 509 },
	{ 8, 6, 360, 459 },
	{ 10, 6, 250, 359 }
};

static const struct scaler_taps downscaling_data_fallback[] = {
	{ 6, 6 },
	{ 6, 5 },
	{ 4, 4 },
	{ 4, 3 },
	{ 4, 2 },
	{ 2, 2 }
};

static const struct scaler_taps upscaling_data[] = {
	{ 4, 4 },
	{ 4, 3 },
	{ 4, 2 },
	{ 2, 2 }
};

bool dal_scaler_construct(
		struct scaler *scl,
		struct scaler_init_data *init_data)
{
	if (!init_data)
		return false;

	if (!init_data->bp)
		return false;

	scl->bp = init_data->bp;
	scl->id = init_data->id;
	scl->ctx = init_data->dal_ctx;
	return true;
}

static bool validate_requested_scale_ratio(uint32_t src, uint32_t dst);
static bool get_taps_number(
	enum scaling_type type,
	uint32_t ratio,
	bool horizontal,
	uint32_t *taps);
static enum scaling_type get_scaling_type(
	uint32_t src_size,
	uint32_t dst_size);

enum scaler_validation_code dal_scaler_get_optimal_taps_number(
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps)
{
	struct view *src_view = &params->source_view;
	struct view *dst_view = &params->dest_view;
	enum scaling_type h_type;
	enum scaling_type v_type;
	uint32_t h_ratio;
	uint32_t v_ratio;

	/* Validate the parameters */
	if (src_view->width == 0 || dst_view->width == 0)
		return SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;

	if (src_view->height == 0 || dst_view->height == 0)
		return SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;

	/* There are  9  cases and we grouped this into 3 groups : no scale,
	 * upscale, downscale
	 * 0  Hor & vert no scale

	 * 1. Hor & vert      upscale
	 * 2. Hor down scale, vert up scale
	 * 3. Hor upscale ,   vert down scale
	 * 4. Hor up scale,   vert no scale
	 * 5. Hor no scale,   vert up scale

	 * 6. Hor & vert      down scale
	 * 7. Hor no scale,   vert down scale
	 * 8. Hor down scale, vert no scale

	 * The hw could do down scale up to 4:1 and upscale could be an
	 * unlimited ratio.
	 * However the mode validation goes through this code path, but
	 * when we need to generate the coefficients the same restrictions
	 * should be applied to the ratio .
	 * Then following limitations are applied :
	 * For coefficient generation for upscale ratio 1:4 versa  hw could do
	 * unlimited ratio.
	 * Hw places the restriction in down scale case because it could do 4:1,
	 * but coefficients generation could do 6:1.
	 * We could enlarge the coefficient generation range for upscale case if
	 * we really need this because it would be related to
	 * add more sharpness tables
	 *
	 */

	if (!validate_requested_scale_ratio(src_view->width,
		dst_view->width))
		return SCALER_VALIDATION_SCALING_RATIO_NOT_SUPPORTED;

	if (!validate_requested_scale_ratio(src_view->height,
		dst_view->height))
		return SCALER_VALIDATION_SCALING_RATIO_NOT_SUPPORTED;

	h_type = get_scaling_type(src_view->width, dst_view->width);
	v_type = get_scaling_type(src_view->height, dst_view->height);

	h_ratio = dst_view->width * SCALER_RATIO_DIVIDER
		/ src_view->width;
	v_ratio = dst_view->height * SCALER_RATIO_DIVIDER
		/ src_view->height;

	if (!get_taps_number(h_type, h_ratio, true, &taps->h_taps))
		return SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;

	if (!get_taps_number(v_type, v_ratio, false, &taps->v_taps))
		return SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;

	return SCALER_VALIDATION_OK;
}

enum scaler_validation_code dal_scaler_get_next_lower_taps_number(
	struct scaler_validation_params *params,
	struct scaling_tap_info *taps)
{
	enum scaler_validation_code code =
		SCALER_VALIDATION_INVALID_INPUT_PARAMETERS;
	uint32_t array_size = sizeof(downscaling_data_fallback)
		/ sizeof(downscaling_data_fallback[0]);

	uint32_t i;

	/* we should loop through the predefined array to find lower taps
	 * configuration
	 * we apply the lower taps in down scale and upscale cases identically
	 */
	for (i = 0; i < array_size; ++i) {
		/* find vtaps smaller than we have in parameter and return to
		 * the caller */
		if (taps->v_taps > downscaling_data_fallback[i].v_tap) {
			/* override is allowed when there is scaling on X */
			if (taps->h_taps > 1)
				taps->h_taps =
					downscaling_data_fallback[i].h_tap;

			taps->v_taps = downscaling_data_fallback[i].v_tap;
			code = SCALER_VALIDATION_OK;
			break;
		}
	}

	return code;
}

static bool validate_requested_scale_ratio(uint32_t src, uint32_t dst)
{
	uint32_t ratio = dst * SCALER_RATIO_DIVIDER / src;

	if (dst > src) {
		/* ratio bigger than max allowed?
		 * acc.to coefficient generation capability
		 */
		if (ratio > UP_SCALER_RATIO_MAX)
			return false;
	} else {
		if (ratio < DOWN_SCALER_RATIO_MAX)
			return false;
	}
	return true;
}

static bool get_taps_number(
	enum scaling_type type,
	uint32_t ratio,
	bool horizontal,
	uint32_t *taps)
{
	if (!taps)
		return false;

	if (type == SCALING_TYPE_NO_SCALING)
		*taps = 1;
	else if (type == SCALING_TYPE_UPSCALING) {
		if (horizontal)
			*taps = upscaling_data[0].h_tap;
		else
			*taps = upscaling_data[0].v_tap;
	} else {
		uint32_t size_of_array = ARRAY_SIZE(downscaling_data);
		uint32_t i;

		for (i = 0; i < size_of_array; ++i) {
			if (ratio >= downscaling_data[i].lo_ratio
				&& ratio <= downscaling_data[i].hi_ratio) {
				if (horizontal)
					*taps = downscaling_data[i].h_tap;
				else
					*taps = downscaling_data[i].v_tap;

				return true;
			}
		}

		if (horizontal)
			*taps = downscaling_data[0].h_tap;
		else
			*taps = downscaling_data[0].v_tap;
	}

	return true;
}

static enum scaling_type get_scaling_type(
	uint32_t src_size,
	uint32_t dst_size)
{
	enum scaling_type type = SCALING_TYPE_NO_SCALING;

	if (dst_size > src_size)
		type = SCALING_TYPE_UPSCALING;
	else if (dst_size < src_size)
		type = SCALING_TYPE_DOWNSCALING;
	else
		ASSERT(dst_size == src_size);

	return type;

}

bool dal_scaler_update_viewport(
	struct scaler *scl,
	const struct rect *view_port,
	bool is_fbc_attached)
{
	bool program_req = false;
	struct rect current_view_port;

	if (view_port == NULL)
		return program_req;

	scl->funcs->get_viewport(scl, &current_view_port);

	if (current_view_port.x != view_port->x ||
			current_view_port.y != view_port->y ||
			current_view_port.height != view_port->height ||
			current_view_port.width != view_port->width)
		program_req = true;

	if (program_req) {
		/*underlay viewport is programmed with scaler
		 *program_viewport function pointer is not exposed*/
		if (scl->funcs->program_viewport != NULL)
			scl->funcs->program_viewport(scl, view_port);
	}

	return program_req;
}
