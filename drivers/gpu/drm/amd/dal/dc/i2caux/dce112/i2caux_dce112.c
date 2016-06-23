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

#include "dm_services.h"

#include "include/i2caux_interface.h"
#include "../i2caux.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"
#include "../i2c_hw_engine.h"

#include "../dce110/i2caux_dce110.h"
#include "i2caux_dce112.h"

#include "i2c_hw_engine_dce112.h"

static const enum gpio_ddc_line hw_ddc_lines[] = {
	GPIO_DDC_LINE_DDC1,
	GPIO_DDC_LINE_DDC2,
	GPIO_DDC_LINE_DDC3,
	GPIO_DDC_LINE_DDC4,
	GPIO_DDC_LINE_DDC5,
	GPIO_DDC_LINE_DDC6,
};

static bool construct(
	struct i2caux_dce110 *i2caux_dce110,
	struct adapter_service *as,
	struct dc_context *ctx)
{
	int i = 0;
	uint32_t reference_frequency = 0;
	struct i2caux *base = NULL;

	if (!dal_i2caux_dce110_construct(i2caux_dce110, as, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	/*TODO: For CZ bring up, if dal_i2caux_get_reference_clock
	 * does not return 48KHz, we need hard coded for 48Khz.
	 * Some BIOS setting incorrect cause this
	 * For production, we always get value from BIOS*/
	reference_frequency =
		dal_i2caux_get_reference_clock(as) >> 1;

	base = &i2caux_dce110->base;

	/* Create I2C engines (DDC lines per connector)
	 * different I2C/AUX usage cases, DDC, Generic GPIO, AUX.
	 */
	do {
		enum gpio_ddc_line line_id = hw_ddc_lines[i];

		struct i2c_hw_engine_dce110_create_arg hw_arg_dce110;

		hw_arg_dce110.engine_id = i;
		hw_arg_dce110.reference_frequency = reference_frequency;
		hw_arg_dce110.default_speed = base->default_i2c_hw_speed;
		hw_arg_dce110.ctx = ctx;

		if (base->i2c_hw_engines[line_id])
			base->i2c_hw_engines[line_id]->funcs->destroy(&base->i2c_hw_engines[line_id]);

		base->i2c_hw_engines[line_id] =
			dal_i2c_hw_engine_dce112_create(&hw_arg_dce110);

		++i;
	} while (i < ARRAY_SIZE(hw_ddc_lines));

	return true;
}

/*
 * dal_i2caux_dce110_create
 *
 * @brief
 * public interface to allocate memory for DCE11 I2CAUX
 *
 * @param
 * struct adapter_service *as - [in]
 * struct dc_context *ctx - [in]
 *
 * @return
 * pointer to the base struct of DCE11 I2CAUX
 */
struct i2caux *dal_i2caux_dce112_create(
	struct adapter_service *as,
	struct dc_context *ctx)
{
	struct i2caux_dce110 *i2caux_dce110 =
		dm_alloc(sizeof(struct i2caux_dce110));

	if (!i2caux_dce110) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(i2caux_dce110, as, ctx))
		return &i2caux_dce110->base;

	ASSERT_CRITICAL(false);

	dm_free(i2caux_dce110);

	return NULL;
}
