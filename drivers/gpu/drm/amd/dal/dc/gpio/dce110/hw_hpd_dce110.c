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
#include "include/gpio_types.h"
#include "../hw_gpio_pin.h"
#include "../hw_gpio.h"
#include "../hw_hpd.h"

/*
 * Header of this unit
 */
#include "hw_hpd_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

/*
 * This unit
 */

static void destruct(
	struct hw_hpd_dce110 *pin)
{
	dal_hw_hpd_destruct(&pin->base);
}

static void destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_hpd_dce110 *pin = HPD_DCE110_FROM_BASE(*ptr);

	destruct(pin);

	dm_free(pin);

	*ptr = NULL;
}

struct hw_gpio_generic_dce110_init {
	struct hw_gpio_pin_reg hw_gpio_data_reg;
	struct hw_hpd_dce110_addr addr;
};

static const struct hw_gpio_generic_dce110_init
	hw_gpio_generic_dce110_init[GPIO_HPD_COUNT] = {
	/* GPIO_HPD_1 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD1_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD1_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD1_Y_MASK
			}
		},
		{
			mmHPD0_DC_HPD_INT_STATUS,
			mmHPD0_DC_HPD_TOGGLE_FILT_CNTL
		}
	},
	/* GPIO_HPD_2 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD2_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD2_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD2_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD2_Y_MASK
			}
		},
		{
			mmHPD1_DC_HPD_INT_STATUS,
			mmHPD1_DC_HPD_TOGGLE_FILT_CNTL
		}
	},
	/* GPIO_HPD_3 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD3_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD3_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD3_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD3_Y_MASK
			}
		},
		{
			mmHPD2_DC_HPD_INT_STATUS,
			mmHPD2_DC_HPD_TOGGLE_FILT_CNTL
		}
	},
	/* GPIO_HPD_4 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD4_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD4_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD4_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD4_Y_MASK
			}
		},
		{
			mmHPD3_DC_HPD_INT_STATUS,
			mmHPD3_DC_HPD_TOGGLE_FILT_CNTL
		}
	},
	/* GPIO_HPD_5 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD5_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD5_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD5_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD5_Y_MASK
			}
		},
		{
			mmHPD4_DC_HPD_INT_STATUS,
			mmHPD4_DC_HPD_TOGGLE_FILT_CNTL
		}
	},
	/* GPIO_HPD_6 */
	{
		{
			{
				mmDC_GPIO_HPD_MASK,
				DC_GPIO_HPD_MASK__DC_GPIO_HPD6_MASK_MASK
			},
			{
				mmDC_GPIO_HPD_A,
				DC_GPIO_HPD_A__DC_GPIO_HPD6_A_MASK
			},
			{
				mmDC_GPIO_HPD_EN,
				DC_GPIO_HPD_EN__DC_GPIO_HPD6_EN_MASK
			},
			{
				mmDC_GPIO_HPD_Y,
				DC_GPIO_HPD_Y__DC_GPIO_HPD6_Y_MASK
			}
		},
		{
			mmHPD5_DC_HPD_INT_STATUS,
			mmHPD5_DC_HPD_TOGGLE_FILT_CNTL
		}
	}
};

static enum gpio_result get_value(
	const struct hw_gpio_pin *ptr,
	uint32_t *value)
{
	struct hw_hpd_dce110 *pin = HPD_DCE110_FROM_BASE(ptr);

	/* in Interrupt mode we ask for SENSE bit */

	if (ptr->mode == GPIO_MODE_INTERRUPT) {
		uint32_t regval;
		uint32_t hpd_delayed = 0;
		uint32_t hpd_sense = 0;

		regval = dm_read_reg(
				ptr->ctx,
				pin->addr.DC_HPD_INT_STATUS);

		hpd_delayed = get_reg_field_value(
				regval,
				DC_HPD_INT_STATUS,
				DC_HPD_SENSE_DELAYED);

		hpd_sense = get_reg_field_value(
				regval,
				DC_HPD_INT_STATUS,
				DC_HPD_SENSE);

		*value = hpd_delayed;
		return GPIO_RESULT_OK;
	}

	/* in any other modes, operate as normal GPIO */

	return dal_hw_gpio_get_value(ptr, value);
}

static enum gpio_result set_config(
	struct hw_gpio_pin *ptr,
	const struct gpio_config_data *config_data)
{
	struct hw_hpd_dce110 *pin = HPD_DCE110_FROM_BASE(ptr);

	if (!config_data)
		return GPIO_RESULT_INVALID_DATA;

	{
		uint32_t value;

		value = dm_read_reg(
			ptr->ctx,
			pin->addr.DC_HPD_TOGGLE_FILT_CNTL);

		set_reg_field_value(
			value,
			config_data->config.hpd.delay_on_connect / 10,
			DC_HPD_TOGGLE_FILT_CNTL,
			DC_HPD_CONNECT_INT_DELAY);

		set_reg_field_value(
			value,
			config_data->config.hpd.delay_on_disconnect / 10,
			DC_HPD_TOGGLE_FILT_CNTL,
			DC_HPD_DISCONNECT_INT_DELAY);

		dm_write_reg(
			ptr->ctx,
			pin->addr.DC_HPD_TOGGLE_FILT_CNTL,
			value);

	}

	return GPIO_RESULT_OK;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = destroy,
	.open = dal_hw_gpio_open,
	.get_value = get_value,
	.set_value = dal_hw_gpio_set_value,
	.set_config = set_config,
	.change_mode = dal_hw_gpio_change_mode,
	.close = dal_hw_gpio_close,
};

static bool construct(
	struct hw_hpd_dce110 *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	const struct hw_gpio_generic_dce110_init *init;

	if (id != GPIO_ID_HPD) {
		ASSERT_CRITICAL(false);
		return false;
	}

	if ((en < GPIO_HPD_MIN) || (en > GPIO_HPD_MAX)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	if (!dal_hw_hpd_construct(&pin->base, id, en, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	pin->base.base.base.funcs = &funcs;

	init = hw_gpio_generic_dce110_init + en;

	pin->base.base.pin_reg = init->hw_gpio_data_reg;

	pin->addr = init->addr;

	return true;
}

struct hw_gpio_pin *dal_hw_hpd_dce110_create(
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	struct hw_hpd_dce110 *pin = dm_alloc(sizeof(struct hw_hpd_dce110));

	if (!pin) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(pin, id, en, ctx))
		return &pin->base.base.base;

	ASSERT_CRITICAL(false);

	dm_free(pin);

	return NULL;
}
