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

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "../i2caux.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"
#include "../i2c_hw_engine.h"

/*
 * Header of this unit
 */
#include "i2caux_dce110.h"

#include "i2c_sw_engine_dce110.h"
#include "i2c_hw_engine_dce110.h"
#include "aux_engine_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */
/*cast pointer to struct i2caux TO pointer to struct i2caux_dce110*/
#define FROM_I2C_AUX(ptr) \
	container_of((ptr), struct i2caux_dce110, base)

static void destruct(
	struct i2caux_dce110 *i2caux_dce110)
{
	dal_i2caux_destruct(&i2caux_dce110->base);
}

static void destroy(
	struct i2caux **i2c_engine)
{
	struct i2caux_dce110 *i2caux_dce110 = FROM_I2C_AUX(*i2c_engine);

	destruct(i2caux_dce110);

	dm_free(i2caux_dce110);

	*i2c_engine = NULL;
}

static struct i2c_engine *acquire_i2c_hw_engine(
	struct i2caux *i2caux,
	struct ddc *ddc)
{
	struct i2caux_dce110 *i2caux_dce110 = FROM_I2C_AUX(i2caux);

	struct i2c_engine *engine = NULL;
	/* generic hw engine is not used for EDID read
	 * It may be needed for external i2c device, like thermal chip,
	 * TODO will be implemented when needed.
	 * check dce80 bool non_generic for generic hw engine;
	 */

	if (!ddc)
		return NULL;

	if (dal_ddc_is_hw_supported(ddc)) {
		enum gpio_ddc_line line = dal_ddc_get_line(ddc);

		if (line < GPIO_DDC_LINE_COUNT)
			engine = i2caux->i2c_hw_engines[line];
	}

	if (!engine)
		return NULL;

	if (!i2caux_dce110->i2c_hw_buffer_in_use &&
		engine->base.funcs->acquire(&engine->base, ddc)) {
		i2caux_dce110->i2c_hw_buffer_in_use = true;
		return engine;
	}

	return NULL;
}

static void release_engine(
	struct i2caux *i2caux,
	struct engine *engine)
{
	struct i2caux_dce110 *i2caux_dce110 = FROM_I2C_AUX(i2caux);

	if (engine->funcs->get_engine_type(engine) ==
		I2CAUX_ENGINE_TYPE_I2C_DDC_HW)
		i2caux_dce110->i2c_hw_buffer_in_use = false;

	dal_i2caux_release_engine(i2caux, engine);
}

static const enum gpio_ddc_line hw_ddc_lines[] = {
	GPIO_DDC_LINE_DDC1,
	GPIO_DDC_LINE_DDC2,
	GPIO_DDC_LINE_DDC3,
	GPIO_DDC_LINE_DDC4,
	GPIO_DDC_LINE_DDC5,
	GPIO_DDC_LINE_DDC6,
};

static const enum gpio_ddc_line hw_aux_lines[] = {
	GPIO_DDC_LINE_DDC1,
	GPIO_DDC_LINE_DDC2,
	GPIO_DDC_LINE_DDC3,
	GPIO_DDC_LINE_DDC4,
	GPIO_DDC_LINE_DDC5,
	GPIO_DDC_LINE_DDC6,
};

/* function table */
static const struct i2caux_funcs i2caux_funcs = {
	.destroy = destroy,
	.acquire_i2c_hw_engine = acquire_i2c_hw_engine,
	.release_engine = release_engine,
	.acquire_i2c_sw_engine = dal_i2caux_acquire_i2c_sw_engine,
	.acquire_aux_engine = dal_i2caux_acquire_aux_engine,
};

static bool construct(
	struct i2caux_dce110 *i2caux_dce110,
	struct adapter_service *as,
	struct dc_context *ctx)
{
	uint32_t i = 0;
	uint32_t reference_frequency = 0;
	bool use_i2c_sw_engine = false;
	struct i2caux *base = NULL;
	/*TODO: For CZ bring up, if dal_i2caux_get_reference_clock
	 * does not return 48KHz, we need hard coded for 48Khz.
	 * Some BIOS setting incorrect cause this
	 * For production, we always get value from BIOS*/
	reference_frequency =
		dal_i2caux_get_reference_clock(as) >> 1;

	use_i2c_sw_engine = dal_adapter_service_is_feature_supported(
		FEATURE_RESTORE_USAGE_I2C_SW_ENGINE);

	base = &i2caux_dce110->base;

	if (!dal_i2caux_construct(base, as, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	i2caux_dce110->base.funcs = &i2caux_funcs;
	i2caux_dce110->i2c_hw_buffer_in_use = false;
	/* Create I2C engines (DDC lines per connector)
	 * different I2C/AUX usage cases, DDC, Generic GPIO, AUX.
	 */
	do {
		enum gpio_ddc_line line_id = hw_ddc_lines[i];

		struct i2c_hw_engine_dce110_create_arg hw_arg_dce110;

		if (use_i2c_sw_engine) {
			struct i2c_sw_engine_dce110_create_arg sw_arg;

			sw_arg.engine_id = i;
			sw_arg.default_speed = base->default_i2c_sw_speed;
			sw_arg.ctx = ctx;
			base->i2c_sw_engines[line_id] =
				dal_i2c_sw_engine_dce110_create(&sw_arg);
		}

		hw_arg_dce110.engine_id = i;
		hw_arg_dce110.reference_frequency = reference_frequency;
		hw_arg_dce110.default_speed = base->default_i2c_hw_speed;
		hw_arg_dce110.ctx = ctx;

		base->i2c_hw_engines[line_id] =
			dal_i2c_hw_engine_dce110_create(&hw_arg_dce110);

		++i;
	} while (i < ARRAY_SIZE(hw_ddc_lines));

	/* Create AUX engines for all lines which has assisted HW AUX
	 * 'i' (loop counter) used as DDC/AUX engine_id */

	i = 0;

	do {
		enum gpio_ddc_line line_id = hw_aux_lines[i];

		struct aux_engine_dce110_init_data aux_init_data;

		aux_init_data.engine_id = i;
		aux_init_data.timeout_period = base->aux_timeout_period;
		aux_init_data.ctx = ctx;

		base->aux_engines[line_id] =
			dal_aux_engine_dce110_create(&aux_init_data);

		++i;
	} while (i < ARRAY_SIZE(hw_aux_lines));

	/*TODO Generic I2C SW and HW*/

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
struct i2caux *dal_i2caux_dce110_create(
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
