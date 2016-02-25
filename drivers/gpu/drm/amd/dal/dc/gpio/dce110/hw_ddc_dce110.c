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
#include "../hw_ddc.h"

/*
 * Header of this unit
 */
#include "hw_ddc_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#define ADDR_DDC_SETUP pin->addr.dc_i2c_ddc_setup
/*
 * This unit
 */
static void destruct(
	struct hw_ddc_dce110 *pin)
{
	dal_hw_ddc_destruct(&pin->base);
}

static void destroy(
	struct hw_gpio_pin **ptr)
{
	struct hw_ddc_dce110 *pin = DDC_DCE110_FROM_BASE(*ptr);

	destruct(pin);

	dm_free(pin);

	*ptr = NULL;
}

struct hw_ddc_dce110_init {
	struct hw_gpio_pin_reg hw_gpio_data_reg;
	struct hw_ddc_mask hw_ddc_mask;
	struct hw_ddc_dce110_addr hw_ddc_dce110_addr;
};

static const struct hw_ddc_dce110_init
	hw_ddc_dce110_init_data[GPIO_DDC_LINE_COUNT] = {
	/* GPIO_DDC_LINE_DDC1 */
	{
		{
			{
				mmDC_GPIO_DDC1_MASK,
				DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC1_A,
				DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC1_EN,
				DC_GPIO_DDC1_EN__DC_GPIO_DDC1DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC1_Y,
				DC_GPIO_DDC1_Y__DC_GPIO_DDC1DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_MASK_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_PD_EN_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_RECV_MASK,
			DC_GPIO_DDC1_MASK__AUX_PAD1_MODE_MASK,
			DC_GPIO_DDC1_MASK__AUX1_POL_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC1_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC2 */
	{
		{
			{
				mmDC_GPIO_DDC2_MASK,
				DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC2_A,
				DC_GPIO_DDC2_A__DC_GPIO_DDC2DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC2_EN,
				DC_GPIO_DDC2_EN__DC_GPIO_DDC2DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC2_Y,
				DC_GPIO_DDC2_Y__DC_GPIO_DDC2DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_MASK_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_PD_EN_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_RECV_MASK,
			DC_GPIO_DDC2_MASK__AUX_PAD2_MODE_MASK,
			DC_GPIO_DDC2_MASK__AUX2_POL_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC2_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC3 */
	{
		{
			{
				mmDC_GPIO_DDC3_MASK,
				DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC3_A,
				DC_GPIO_DDC3_A__DC_GPIO_DDC3DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC3_EN,
				DC_GPIO_DDC3_EN__DC_GPIO_DDC3DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC3_Y,
				DC_GPIO_DDC3_Y__DC_GPIO_DDC3DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_MASK_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_PD_EN_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_RECV_MASK,
			DC_GPIO_DDC3_MASK__AUX_PAD3_MODE_MASK,
			DC_GPIO_DDC3_MASK__AUX3_POL_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC3_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC4 */
	{
		{
			{
				mmDC_GPIO_DDC4_MASK,
				DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC4_A,
				DC_GPIO_DDC4_A__DC_GPIO_DDC4DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC4_EN,
				DC_GPIO_DDC4_EN__DC_GPIO_DDC4DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC4_Y,
				DC_GPIO_DDC4_Y__DC_GPIO_DDC4DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_MASK_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_PD_EN_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_RECV_MASK,
			DC_GPIO_DDC4_MASK__AUX_PAD4_MODE_MASK,
			DC_GPIO_DDC4_MASK__AUX4_POL_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC4_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC5 */
	{
		{
			{
				mmDC_GPIO_DDC5_MASK,
				DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC5_A,
				DC_GPIO_DDC5_A__DC_GPIO_DDC5DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC5_EN,
				DC_GPIO_DDC5_EN__DC_GPIO_DDC5DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC5_Y,
				DC_GPIO_DDC5_Y__DC_GPIO_DDC5DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_MASK_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_PD_EN_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_RECV_MASK,
			DC_GPIO_DDC5_MASK__AUX_PAD5_MODE_MASK,
			DC_GPIO_DDC5_MASK__AUX5_POL_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC5_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC6 */
	{
		{
			{
				mmDC_GPIO_DDC6_MASK,
				DC_GPIO_DDC6_MASK__DC_GPIO_DDC6DATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDC6_A,
				DC_GPIO_DDC6_A__DC_GPIO_DDC6DATA_A_MASK
			},
			{
				mmDC_GPIO_DDC6_EN,
				DC_GPIO_DDC6_EN__DC_GPIO_DDC6DATA_EN_MASK
			},
			{
				mmDC_GPIO_DDC6_Y,
				DC_GPIO_DDC6_Y__DC_GPIO_DDC6DATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6DATA_MASK_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6DATA_PD_EN_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6DATA_RECV_MASK,
			DC_GPIO_DDC6_MASK__AUX_PAD6_MODE_MASK,
			DC_GPIO_DDC6_MASK__AUX6_POL_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC6_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC_VGA */
	{
		{
			{
				mmDC_GPIO_DDCVGA_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_MASK_MASK
			},
			{
				mmDC_GPIO_DDCVGA_A,
				DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGADATA_A_MASK
			},
			{
				mmDC_GPIO_DDCVGA_EN,
				DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGADATA_EN_MASK
			},
			{
				mmDC_GPIO_DDCVGA_Y,
				DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGADATA_Y_MASK
			}
		},
		{
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_MASK_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_PD_EN_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_RECV_MASK,
			DC_GPIO_DDCVGA_MASK__AUX_PADVGA_MODE_MASK,
			DC_GPIO_DDCVGA_MASK__AUXVGA_POL_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_STR_MASK
		},
		{
			mmDC_I2C_DDCVGA_SETUP
		}
	},
	/* GPIO_DDC_LINE_I2CPAD */
	{
		{
			{
				mmDC_GPIO_I2CPAD_MASK,
				DC_GPIO_I2CPAD_MASK__DC_GPIO_SDA_MASK_MASK
			},
			{
				mmDC_GPIO_I2CPAD_A,
				DC_GPIO_I2CPAD_A__DC_GPIO_SDA_A_MASK
			},
			{
				mmDC_GPIO_I2CPAD_EN,
				DC_GPIO_I2CPAD_EN__DC_GPIO_SDA_EN_MASK
			},
			{
				mmDC_GPIO_I2CPAD_Y,
				DC_GPIO_I2CPAD_Y__DC_GPIO_SDA_Y_MASK
			}
		},
		{
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SDA_MASK_MASK,
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SDA_PD_DIS_MASK,
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SDA_RECV_MASK,
			0,
			0,
			0
		},
		{
			0
		}
	}
};

static const struct hw_ddc_dce110_init
	hw_ddc_dce110_init_clock[GPIO_DDC_LINE_COUNT] = {
	/* GPIO_DDC_LINE_DDC1 */
	{
		{
			{
				mmDC_GPIO_DDC1_MASK,
				DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC1_A,
				DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC1_EN,
				DC_GPIO_DDC1_EN__DC_GPIO_DDC1CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC1_Y,
				DC_GPIO_DDC1_Y__DC_GPIO_DDC1CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_MASK_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_PD_EN_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_RECV_MASK,
			DC_GPIO_DDC1_MASK__AUX_PAD1_MODE_MASK,
			DC_GPIO_DDC1_MASK__AUX1_POL_MASK,
			DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC1_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC2 */
	{
		{
			{
				mmDC_GPIO_DDC2_MASK,
				DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC2_A,
				DC_GPIO_DDC2_A__DC_GPIO_DDC2CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC2_EN,
				DC_GPIO_DDC2_EN__DC_GPIO_DDC2CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC2_Y,
				DC_GPIO_DDC2_Y__DC_GPIO_DDC2CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_MASK_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_PD_EN_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_RECV_MASK,
			DC_GPIO_DDC2_MASK__AUX_PAD2_MODE_MASK,
			DC_GPIO_DDC2_MASK__AUX2_POL_MASK,
			DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC2_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC3 */
	{
		{
			{
				mmDC_GPIO_DDC3_MASK,
				DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC3_A,
				DC_GPIO_DDC3_A__DC_GPIO_DDC3CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC3_EN,
				DC_GPIO_DDC3_EN__DC_GPIO_DDC3CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC3_Y,
				DC_GPIO_DDC3_Y__DC_GPIO_DDC3CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_MASK_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_PD_EN_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_RECV_MASK,
			DC_GPIO_DDC3_MASK__AUX_PAD3_MODE_MASK,
			DC_GPIO_DDC3_MASK__AUX3_POL_MASK,
			DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC3_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC4 */
	{
		{
			{
				mmDC_GPIO_DDC4_MASK,
				DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC4_A,
				DC_GPIO_DDC4_A__DC_GPIO_DDC4CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC4_EN,
				DC_GPIO_DDC4_EN__DC_GPIO_DDC4CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC4_Y,
				DC_GPIO_DDC4_Y__DC_GPIO_DDC4CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_MASK_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_PD_EN_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_RECV_MASK,
			DC_GPIO_DDC4_MASK__AUX_PAD4_MODE_MASK,
			DC_GPIO_DDC4_MASK__AUX4_POL_MASK,
			DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC4_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC5 */
	{
		{
			{
				mmDC_GPIO_DDC5_MASK,
				DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC5_A,
				DC_GPIO_DDC5_A__DC_GPIO_DDC5CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC5_EN,
				DC_GPIO_DDC5_EN__DC_GPIO_DDC5CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC5_Y,
				DC_GPIO_DDC5_Y__DC_GPIO_DDC5CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_MASK_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_PD_EN_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_RECV_MASK,
			DC_GPIO_DDC5_MASK__AUX_PAD5_MODE_MASK,
			DC_GPIO_DDC5_MASK__AUX5_POL_MASK,
			DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC5_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC6 */
	{
		{
			{
				mmDC_GPIO_DDC6_MASK,
				DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDC6_A,
				DC_GPIO_DDC6_A__DC_GPIO_DDC6CLK_A_MASK
			},
			{
				mmDC_GPIO_DDC6_EN,
				DC_GPIO_DDC6_EN__DC_GPIO_DDC6CLK_EN_MASK
			},
			{
				mmDC_GPIO_DDC6_Y,
				DC_GPIO_DDC6_Y__DC_GPIO_DDC6CLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_MASK_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_PD_EN_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_RECV_MASK,
			DC_GPIO_DDC6_MASK__AUX_PAD6_MODE_MASK,
			DC_GPIO_DDC6_MASK__AUX6_POL_MASK,
			DC_GPIO_DDC6_MASK__DC_GPIO_DDC6CLK_STR_MASK
		},
		{
			mmDC_I2C_DDC6_SETUP
		}
	},
	/* GPIO_DDC_LINE_DDC_VGA */
	{
		{
			{
				mmDC_GPIO_DDCVGA_MASK,
				DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_MASK_MASK
			},
			{
				mmDC_GPIO_DDCVGA_A,
				DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGACLK_A_MASK
			},
			{
				mmDC_GPIO_DDCVGA_EN,
				DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGACLK_EN_MASK
			},
			{
				mmDC_GPIO_DDCVGA_Y,
				DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGACLK_Y_MASK
			}
		},
		{
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_MASK_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_PD_EN_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_RECV_MASK,
			DC_GPIO_DDCVGA_MASK__AUX_PADVGA_MODE_MASK,
			DC_GPIO_DDCVGA_MASK__AUXVGA_POL_MASK,
			DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_STR_MASK
		},
		{
			mmDC_I2C_DDCVGA_SETUP
		}
	},
	/* GPIO_DDC_LINE_I2CPAD */
	{
		{
			{
				mmDC_GPIO_I2CPAD_MASK,
				DC_GPIO_I2CPAD_MASK__DC_GPIO_SCL_MASK_MASK
			},
			{
				mmDC_GPIO_I2CPAD_A,
				DC_GPIO_I2CPAD_A__DC_GPIO_SCL_A_MASK
			},
			{
				mmDC_GPIO_I2CPAD_EN,
				DC_GPIO_I2CPAD_EN__DC_GPIO_SCL_EN_MASK
			},
			{
				mmDC_GPIO_I2CPAD_Y,
				DC_GPIO_I2CPAD_Y__DC_GPIO_SCL_Y_MASK
			}
		},
		{
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SCL_MASK_MASK,
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SCL_PD_DIS_MASK,
			DC_GPIO_I2CPAD_MASK__DC_GPIO_SCL_RECV_MASK,
			0,
			0,
			0
		},
		{
			0
		}
	}
};

static void setup_i2c_polling(
	struct dc_context *ctx,
	const uint32_t addr,
	bool enable_detect,
	bool detect_mode)
{
	uint32_t value;

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		enable_detect,
		DC_I2C_DDC1_SETUP,
		DC_I2C_DDC1_ENABLE);

	set_reg_field_value(
		value,
		enable_detect,
		DC_I2C_DDC1_SETUP,
		DC_I2C_DDC1_EDID_DETECT_ENABLE);

	if (enable_detect)
		set_reg_field_value(
			value,
			detect_mode,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_EDID_DETECT_MODE);

	dm_write_reg(ctx, addr, value);
}

static enum gpio_result set_config(
	struct hw_gpio_pin *ptr,
	const struct gpio_config_data *config_data)
{
	struct hw_ddc_dce110 *pin = DDC_DCE110_FROM_BASE(ptr);
	struct hw_gpio *hw_gpio = NULL;
	uint32_t addr;
	uint32_t regval;
	uint32_t ddc_data_pd_en = 0;
	uint32_t ddc_clk_pd_en = 0;
	uint32_t aux_pad_mode = 0;

	hw_gpio = &pin->base.base;

	if (hw_gpio == NULL) {
		ASSERT_CRITICAL(false);
		return GPIO_RESULT_NULL_HANDLE;
	}

	/* switch dual mode GPIO to I2C/AUX mode */

	addr = hw_gpio->pin_reg.DC_GPIO_DATA_MASK.addr;

	regval = dm_read_reg(ptr->ctx, addr);

	ddc_data_pd_en = get_reg_field_value(
			regval,
			DC_GPIO_DDC1_MASK,
			DC_GPIO_DDC1DATA_PD_EN);

	ddc_clk_pd_en = get_reg_field_value(
			regval,
			DC_GPIO_DDC1_MASK,
			DC_GPIO_DDC1CLK_PD_EN);

	aux_pad_mode = get_reg_field_value(
			regval,
			DC_GPIO_DDC1_MASK,
			AUX_PAD1_MODE);

	switch (config_data->config.ddc.type) {
	case GPIO_DDC_CONFIG_TYPE_MODE_I2C:
		/* On plug-in, there is a transient level on the pad
		 * which must be discharged through the internal pull-down.
		 * Enable internal pull-down, 2.5msec discharge time
		 * is required for detection of AUX mode */
		if (hw_gpio->base.en != GPIO_DDC_LINE_VIP_PAD) {
			if (!ddc_data_pd_en || !ddc_clk_pd_en) {
				set_reg_field_value(
					regval,
					1,
					DC_GPIO_DDC1_MASK,
					DC_GPIO_DDC1DATA_PD_EN);

				set_reg_field_value(
					regval,
					1,
					DC_GPIO_DDC1_MASK,
					DC_GPIO_DDC1CLK_PD_EN);

				dm_write_reg(ptr->ctx, addr, regval);

				if (config_data->type ==
					GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					/* should not affect normal I2C R/W */
					/* [anaumov] in DAL2, there was
					 * dc_service_delay_in_microseconds(2500); */
					msleep(3);
			}
		} else {
			uint32_t reg2 = regval;
			uint32_t sda_pd_dis = 0;
			uint32_t scl_pd_dis = 0;

			sda_pd_dis = get_reg_field_value(
					reg2,
					DC_GPIO_I2CPAD_MASK,
					DC_GPIO_SDA_PD_DIS);

			scl_pd_dis = get_reg_field_value(
					reg2,
					DC_GPIO_I2CPAD_MASK,
					DC_GPIO_SCL_PD_DIS);

			if (sda_pd_dis) {
				sda_pd_dis = 0;

				dm_write_reg(ptr->ctx, addr, reg2);

				if (config_data->type ==
					GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					/* should not affect normal I2C R/W */
					/* [anaumov] in DAL2, there was
					 * dc_service_delay_in_microseconds(2500); */
					msleep(3);
			}

			if (!scl_pd_dis) {
				scl_pd_dis = 1;

				dm_write_reg(ptr->ctx, addr, reg2);

				if (config_data->type ==
					GPIO_CONFIG_TYPE_I2C_AUX_DUAL_MODE)
					/* should not affect normal I2C R/W */
					/* [anaumov] in DAL2, there was
					 * dc_service_delay_in_microseconds(2500); */
					msleep(3);
			}
		}

		if (aux_pad_mode) {
			/* let pins to get de-asserted
			 * before setting pad to I2C mode */
			if (config_data->config.ddc.data_en_bit_present ||
				config_data->config.ddc.clock_en_bit_present)
				/* [anaumov] in DAL2, there was
				 * dc_service_delay_in_microseconds(2000); */
				msleep(2);

			/* set the I2C pad mode */
			/* read the register again,
			 * some bits may have been changed */
			regval = dm_read_reg(ptr->ctx, addr);

			set_reg_field_value(
				regval,
				0,
				DC_GPIO_DDC1_MASK,
				AUX_PAD1_MODE);

			dm_write_reg(ptr->ctx, addr, regval);
		}

		return GPIO_RESULT_OK;
	case GPIO_DDC_CONFIG_TYPE_MODE_AUX:
		/* set the AUX pad mode */
		if (!aux_pad_mode) {
			set_reg_field_value(
				regval,
				1,
				DC_GPIO_DDC1_MASK,
				AUX_PAD1_MODE);

			dm_write_reg(ptr->ctx, addr, regval);
		}

		return GPIO_RESULT_OK;
	case GPIO_DDC_CONFIG_TYPE_POLL_FOR_CONNECT:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			setup_i2c_polling(
				ptr->ctx, ADDR_DDC_SETUP, 1, 0);
			return GPIO_RESULT_OK;
		}
	break;
	case GPIO_DDC_CONFIG_TYPE_POLL_FOR_DISCONNECT:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			setup_i2c_polling(
				ptr->ctx, ADDR_DDC_SETUP, 1, 1);
			return GPIO_RESULT_OK;
		}
	break;
	case GPIO_DDC_CONFIG_TYPE_DISABLE_POLLING:
		if ((hw_gpio->base.en >= GPIO_DDC_LINE_DDC1) &&
			(hw_gpio->base.en <= GPIO_DDC_LINE_DDC_VGA)) {
			setup_i2c_polling(
				ptr->ctx, ADDR_DDC_SETUP, 0, 0);
			return GPIO_RESULT_OK;
		}
	break;
	}

	BREAK_TO_DEBUGGER();

	return GPIO_RESULT_NON_SPECIFIC_ERROR;
}

static const struct hw_gpio_pin_funcs funcs = {
	.destroy = destroy,
	.open = dal_hw_ddc_open,
	.get_value = dal_hw_gpio_get_value,
	.set_value = dal_hw_gpio_set_value,
	.set_config = set_config,
	.change_mode = dal_hw_gpio_change_mode,
	.close = dal_hw_gpio_close,
};

static bool construct(
	struct hw_ddc_dce110 *pin,
	enum gpio_id id,
	uint32_t en,
	struct dc_context *ctx)
{
	const struct hw_ddc_dce110_init *init;

	if ((en < GPIO_DDC_LINE_MIN) || (en > GPIO_DDC_LINE_MAX)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	if (!dal_hw_ddc_construct(&pin->base, id, en, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	pin->base.base.base.funcs = &funcs;

	switch (id) {
	case GPIO_ID_DDC_DATA:
		init = hw_ddc_dce110_init_data + en;

		pin->base.base.pin_reg = init->hw_gpio_data_reg;
		pin->base.mask = init->hw_ddc_mask;
		pin->addr = init->hw_ddc_dce110_addr;

		return true;
	case GPIO_ID_DDC_CLOCK:
		init = hw_ddc_dce110_init_clock + en;

		pin->base.base.pin_reg = init->hw_gpio_data_reg;
		pin->base.mask = init->hw_ddc_mask;
		pin->addr = init->hw_ddc_dce110_addr;

		return true;
	default:
		ASSERT_CRITICAL(false);
	}

	return false;
}

struct hw_gpio_pin *dal_hw_ddc_dce110_create(
	struct dc_context *ctx,
	enum gpio_id id,
	uint32_t en)
{
	struct hw_ddc_dce110 *pin = dm_alloc(sizeof(struct hw_ddc_dce110));

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
