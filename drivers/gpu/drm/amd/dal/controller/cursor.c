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

#include "cursor.h"

bool dal_cursor_construct(
		struct cursor *cur,
		struct cursor_init_data *init_data)
{
	if (!init_data)
		return false;

	cur->id = init_data->id;
	cur->ctx = init_data->dal_ctx;

	cur->is_enabled = false;

	return true;
}

bool dal_cursor_set_position(
	struct cursor *cur,
	const struct cursor_position *position)
{
	/* lock cursor registers */
	cur->funcs->lock(cur, true);

	/* Flag passed in structure differentiates cursor enable/disable. */
	/* Update if it differs from cached state. */
	cur->funcs->enable(cur, position->enable);

	cur->funcs->program_position(cur, position->x, position->y);

	if (position->hot_spot_enable)
		cur->funcs->program_hotspot(
				cur,
				position->x_origin,
				position->y_origin);

	/* unlock cursor registers */
	cur->funcs->lock(cur, false);

	return true;
}

bool dal_cursor_set_attributes(
	struct cursor *cur,
	const struct cursor_attributes *attributes)
{
	/* Lock cursor registers */
	cur->funcs->lock(cur, true);

	/* Program cursor control */
	cur->funcs->program_control(
		cur,
		attributes->color_format,
		attributes->attribute_flags.bits.ENABLE_MAGNIFICATION,
		attributes->attribute_flags.bits.INVERSE_TRANSPARENT_CLAMPING);

	/* Program hot spot coordinates */
	cur->funcs->program_hotspot(cur, attributes->x_hot, attributes->y_hot);

	/*
	 * Program cursor size -- NOTE: HW spec specifies that HW register
	 * stores size as (height - 1, width - 1)
	 */
	cur->funcs->program_size(cur, attributes->width, attributes->height);

	/* Program cursor surface address */
	cur->funcs->program_address(cur, attributes->address);

	/* Unlock Cursor registers. */
	cur->funcs->lock(cur, false);

	return true;
}
