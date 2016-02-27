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

#ifndef __DAL_ADAPTER_SERVICE_H__
#define __DAL_ADAPTER_SERVICE_H__

/* Include */
#include "dc_bios_types.h"
#include "include/adapter_service_interface.h"
#include "wireless_data_source.h"

/*
 * Forward declaration
 */
struct gpio_service;
struct asic_cap;

/* Adapter service */
struct adapter_service {
	struct dc_context *ctx;
	struct asic_capability *asic_cap;
	struct dc_bios *dcb_internal;/* created by DC */
	struct dc_bios *dcb_override;/* supplied by creator of DC */
	enum dce_environment dce_environment;
	struct gpio_service *gpio_service;
	struct i2caux *i2caux;
	struct wireless_data wireless_data;
	struct hw_ctx_adapter_service *hw_ctx;
	struct integrated_info *integrated_info;
	uint32_t platform_methods_mask;
	uint32_t ac_level_percentage;
	uint32_t dc_level_percentage;
	uint32_t backlight_caps_initialized;
	uint32_t backlight_8bit_lut[SIZEOF_BACKLIGHT_LUT];
};

/* Type of feature with its runtime parameter and default value */
struct feature_source_entry {
	enum adapter_feature_id feature_id;
	uint32_t default_value;
	bool is_boolean_type;
};

/* Stores entire ASIC features by sets */
extern uint32_t adapter_feature_set[];

#endif /* __DAL_ADAPTER_SERVICE_H__ */
