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

#include "dc_bios_types.h"

#include "include/adapter_service_interface.h"
#include "include/i2caux_interface.h"
#include "include/asic_capability_types.h"
#include "include/gpio_service_interface.h"
#include "include/asic_capability_interface.h"
#include "include/logger_interface.h"

#include "adapter_service.h"
#include "hw_ctx_adapter_service.h"
#include "wireless_data_source.h"

#include "atom.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/hw_ctx_adapter_service_dce110.h"
#endif

#include "diagnostics/hw_ctx_adapter_service_diag.h"

/*
 * Adapter service feature entry table.
 *
 * This is an array of features that is used to generate feature set. Each
 * entry consists three element:
 *
 * Feature name, default value, and if this feature is a boolean type. A
 * feature can only be a boolean or int type.
 *
 * Example 1: a boolean type feature
 * FEATURE_ENABLE_HW_EDID_POLLING, false, true
 *
 * First element is feature name: EATURE_ENABLE_HW_EDID_POLLING, it has a
 * default value 0, and it is a boolean feature.
 *
 * Example 2: an int type feature
 * FEATURE_DCP_PROGRAMMING_WA, 0x1FF7, false
 *
 * In this case, the default value is 0x1FF7 and not a boolean type, which
 * makes it an int type.
 */

static struct feature_source_entry feature_entry_table[] = {
	/* Feature name | default value | is boolean type */
	{FEATURE_ENABLE_HW_EDID_POLLING, false, true},
	{FEATURE_DP_SINK_DETECT_POLL_DATA_PIN, false, true},
	{FEATURE_UNDERFLOW_INTERRUPT, false, true},
	{FEATURE_ALLOW_WATERMARK_ADJUSTMENT, false, true},
	{FEATURE_LIGHT_SLEEP, false, true},
	{FEATURE_DCP_DITHER_FRAME_RANDOM_ENABLE, false, true},
	{FEATURE_DCP_DITHER_RGB_RANDOM_ENABLE, false, true},
	{FEATURE_DCP_DITHER_HIGH_PASS_RANDOM_ENABLE, false, true},
	{FEATURE_LINE_BUFFER_ENHANCED_PIXEL_DEPTH, false, true},
	{FEATURE_MAXIMIZE_URGENCY_WATERMARKS, false, true},
	{FEATURE_MAXIMIZE_STUTTER_MARKS, false, true},
	{FEATURE_MAXIMIZE_NBP_MARKS, false, true},
	/*
	 * We meet HW I2C issue when test S3 resume on KB.
	 * An EPR is created for debug the issue.
	 * Make Test has already been implemented
	 * with HW I2C. The work load for revert back to SW I2C in make test
	 * is big. Below is workaround for this issue.
	 * Driver uses SW I2C.
	 * Make Test uses HW I2C.
	 */
	{FEATURE_RESTORE_USAGE_I2C_SW_ENGINE, true, true},
	{FEATURE_USE_MAX_DISPLAY_CLK, false, true},
	{FEATURE_ALLOW_EDP_RESOURCE_SHARING, false, true},
	{FEATURE_SUPPORT_DP_YUV, false, true},
	{FEATURE_SUPPORT_DP_Y_ONLY, false, true},
	{FEATURE_DISABLE_DP_GTC_SYNC, true, true},
	{FEATURE_MODIFY_TIMINGS_FOR_WIRELESS, false, true},
	{FEATURE_DCP_BIT_DEPTH_REDUCTION_MODE, 0, false},
	{FEATURE_DCP_DITHER_MODE, 0, false},
	{FEATURE_DCP_PROGRAMMING_WA, 0, false},
	{FEATURE_NO_HPD_LOW_POLLING_VCC_OFF, false, true},
	{FEATURE_ENABLE_DFS_BYPASS, false, true},
	{FEATURE_WIRELESS_FULL_TIMING_ADJUSTMENT, false, true},
	{FEATURE_MAX_COFUNC_NON_DP_DISPLAYS, 2, false},
	{FEATURE_WIRELESS_LIMIT_720P, false, true},
	{FEATURE_MODIFY_TIMINGS_FOR_WIRELESS, false, true},
	{FEATURE_SUPPORTED_HDMI_CONNECTION_NUM, 0, false},
	{FEATURE_DETECT_REQUIRE_HPD_HIGH, false, true},
	{FEATURE_NO_HPD_LOW_POLLING_VCC_OFF, false, true},
	{FEATURE_LB_HIGH_RESOLUTION, false, true},
	{FEATURE_MAX_CONTROLLER_NUM, 0, false},
	{FEATURE_DRR_SUPPORT, AS_DRR_SUPPORT_ENABLED, false},
	{FEATURE_STUTTER_MODE, 15, false},
	{FEATURE_DP_DISPLAY_FORCE_SS_ENABLE, false, true},
	{FEATURE_REPORT_CE_MODE_ONLY, false, true},
	{FEATURE_ALLOW_OPTIMIZED_MODE_AS_DEFAULT, false, true},
	{FEATURE_DDC_READ_FORCE_REPEATED_START, false, true},
	{FEATURE_FORCE_TIMING_RESYNC, false, true},
	{FEATURE_TMDS_DISABLE_DITHERING, false, true},
	{FEATURE_HDMI_DISABLE_DITHERING, false, true},
	{FEATURE_DP_DISABLE_DITHERING, false, true},
	{FEATURE_EMBEDDED_DISABLE_DITHERING, true, true},
	{FEATURE_ALLOW_SELF_REFRESH, false, true},
	{FEATURE_ALLOW_DYNAMIC_PIXEL_ENCODING_CHANGE, false, true},
	{FEATURE_ALLOW_HSYNC_VSYNC_ADJUSTMENT, false, true},
	{FEATURE_FORCE_PSR, false, true},
	{FEATURE_PSR_SETUP_TIME_TEST, 0, false},
	{FEATURE_POWER_GATING_PIPE_IN_TILE, true, true},
	{FEATURE_POWER_GATING_LB_PORTION, true, true},
	{FEATURE_PREFER_3D_TIMING, false, true},
	{FEATURE_VARI_BRIGHT_ENABLE, true, true},
	{FEATURE_PSR_ENABLE, false, true},
	{FEATURE_WIRELESS_ENABLE_COMPRESSED_AUDIO, false, true},
	{FEATURE_WIRELESS_INCLUDE_UNVERIFIED_TIMINGS, true, true},
	{FEATURE_EDID_STRESS_READ, false, true},
	{FEATURE_DP_FRAME_PACK_STEREO3D, false, true},
	{FEATURE_DISPLAY_PREFERRED_VIEW, 0, false},
	{FEATURE_ALLOW_HDMI_WITHOUT_AUDIO, false, true},
	{FEATURE_ABM_2_0, false, true},
	{FEATURE_SUPPORT_MIRABILIS, false, true},
	{FEATURE_OPTIMIZATION, 0xFFFF, false},
	{FEATURE_PERF_MEASURE, 0, false},
	{FEATURE_MIN_BACKLIGHT_LEVEL, 0, false},
	{FEATURE_MAX_BACKLIGHT_LEVEL, 255, false},
	{FEATURE_LOAD_DMCU_FIRMWARE, true, true},
	{FEATURE_DISABLE_AZ_CLOCK_GATING, false, true},
	{FEATURE_ENABLE_GPU_SCALING, false, true},
	{FEATURE_DONGLE_SINK_COUNT_CHECK, true, true},
	{FEATURE_INSTANT_UP_SCALE_DOWN_SCALE, false, true},
	{FEATURE_TILED_DISPLAY, false, true},
	{FEATURE_PREFERRED_ABM_CONFIG_SET, 0, false},
	{FEATURE_CHANGE_SW_I2C_SPEED, 50, false},
	{FEATURE_CHANGE_HW_I2C_SPEED, 50, false},
	{FEATURE_CHANGE_I2C_SPEED_CONTROL, false, true},
	{FEATURE_DEFAULT_PSR_LEVEL, 0, false},
	{FEATURE_MAX_CLOCK_SOURCE_NUM, 0, false},
	{FEATURE_REPORT_SINGLE_SELECTED_TIMING, false, true},
	{FEATURE_ALLOW_HDMI_HIGH_CLK_DP_DONGLE, true, true},
	{FEATURE_SUPPORT_EXTERNAL_PANEL_DRR, false, true},
	{FEATURE_LVDS_SAFE_PIXEL_CLOCK_RANGE, 0, false},
	{FEATURE_ABM_CONFIG, 0, false},
	{FEATURE_WIRELESS_ENABLE, false, true},
	{FEATURE_ALLOW_DIRECT_MEMORY_ACCESS_TRIG, false, true},
	{FEATURE_FORCE_STATIC_SCREEN_EVENT_TRIGGERS, 0, false},
	{FEATURE_USE_PPLIB, true, true},
	{FEATURE_DISABLE_LPT_SUPPORT, false, true},
	{FEATURE_DUMMY_FBC_BACKEND, false, true},
	{FEATURE_DPMS_AUDIO_ENDPOINT_CONTROL, true, true},
	{FEATURE_DISABLE_FBC_COMP_CLK_GATE, false, true},
	{FEATURE_PIXEL_PERFECT_OUTPUT, false, true},
	{FEATURE_8BPP_SUPPORTED, false, true}
};


/* Stores entire ASIC features by sets */
uint32_t adapter_feature_set[FEATURE_MAXIMUM/32];

enum {
	LEGACY_MAX_NUM_OF_CONTROLLERS = 2,
	DEFAULT_NUM_COFUNC_NON_DP_DISPLAYS = 2
};

/*
 * get_feature_entries_num
 *
 * Get number of feature entries
 */
static inline uint32_t get_feature_entries_num(void)
{
	return ARRAY_SIZE(feature_entry_table);
}

static void get_platform_info_methods(
		struct adapter_service *as)
{
	struct platform_info_params params;
	uint32_t mask = 0;

	params.data = &mask;
	params.method = PM_GET_AVAILABLE_METHODS;

	if (dal_get_platform_info(as->ctx, &params))
		as->platform_methods_mask = mask;


}

static void initialize_backlight_caps(
		struct adapter_service *as)
{
	struct firmware_info fw_info;
	struct embedded_panel_info panel_info;
	struct platform_info_ext_brightness_caps caps;
	struct platform_info_params params;
	bool custom_curve_present = false;
	bool custom_min_max_present = false;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (!(PM_GET_EXTENDED_BRIGHNESS_CAPS & as->platform_methods_mask)) {
			dal_logger_write(as->ctx->logger,
					LOG_MAJOR_BACKLIGHT,
					LOG_MINOR_BACKLIGHT_BRIGHTESS_CAPS,
					"This method is not supported\n");
			return;
	}

	if (dcb->funcs->get_firmware_info(dcb, &fw_info) != BP_RESULT_OK ||
		dcb->funcs->get_embedded_panel_info(dcb, &panel_info) != BP_RESULT_OK)
		return;

	params.data = &caps;
	params.method = PM_GET_EXTENDED_BRIGHNESS_CAPS;

	if (dal_get_platform_info(as->ctx, &params)) {
		as->ac_level_percentage = caps.basic_caps.ac_level_percentage;
		as->dc_level_percentage = caps.basic_caps.dc_level_percentage;
		custom_curve_present = (caps.data_points_num > 0);
		custom_min_max_present = true;
	} else
		return;
	/* Choose minimum backlight level base on priority:
	 * extended caps,VBIOS,default */
	if (custom_min_max_present)
		as->backlight_8bit_lut[0] = caps.min_input_signal;

	else if (fw_info.min_allowed_bl_level > 0)
		as->backlight_8bit_lut[0] = fw_info.min_allowed_bl_level;

	else
		as->backlight_8bit_lut[0] = DEFAULT_MIN_BACKLIGHT;

	/* Choose maximum backlight level base on priority:
	 * extended caps,default */
	if (custom_min_max_present)
		as->backlight_8bit_lut[100] = caps.max_input_signal;

	else
		as->backlight_8bit_lut[100] = DEFAULT_MAX_BACKLIGHT;

	if (as->backlight_8bit_lut[100] > ABSOLUTE_BACKLIGHT_MAX)
		as->backlight_8bit_lut[100] = ABSOLUTE_BACKLIGHT_MAX;

	if (as->backlight_8bit_lut[0] > as->backlight_8bit_lut[100])
		as->backlight_8bit_lut[0] = as->backlight_8bit_lut[100];

	if (custom_curve_present) {
		uint16_t index = 1;
		uint16_t i;
		uint16_t num_of_data_points = (caps.data_points_num <= 99 ?
				caps.data_points_num : 99);
		/* Filling translation table from data points -
		 * between every two provided data points we
		 * lineary interpolate missing values
		 */
		for (i = 0 ; i < num_of_data_points; i++) {
			uint16_t luminance = caps.data_points[i].luminance;
			uint16_t signal_level =
					caps.data_points[i].signal_level;

			if (signal_level < as->backlight_8bit_lut[0])
				signal_level = as->backlight_8bit_lut[0];

			if (signal_level > as->backlight_8bit_lut[100])
				signal_level = as->backlight_8bit_lut[100];

			/* Lineary interpolate missing values */
			if (index < luminance) {
				uint16_t base_value =
						as->backlight_8bit_lut[index-1];
				uint16_t delta_signal =
						signal_level - base_value;
				uint16_t delta_luma = luminance - index + 1;
				uint16_t step = delta_signal;

				for (; index < luminance ; index++) {
					as->backlight_8bit_lut[index] =
							base_value +
							(step / delta_luma);
					step += delta_signal;
				}
			}
			/* Now [index == luminance], so we can add
			 * data point to the translation table */
			as->backlight_8bit_lut[index++] = signal_level;
		}
		/* Complete the final segment of interpolation -
		 * between last datapoint and maximum value */
		if (index < 100) {
			uint16_t base_value = as->backlight_8bit_lut[index-1];
			uint16_t delta_signal =
					as->backlight_8bit_lut[100]-base_value;
			uint16_t delta_luma = 100 - index + 1;
			uint16_t step = delta_signal;

			for (; index < 100 ; index++) {
				as->backlight_8bit_lut[index] = base_value +
						(step / delta_luma);
				step += delta_signal;
			}
		}
	}
	/* build backlight translation table based on default curve */
	else {
		/* Default backlight curve can be defined by
		 * polinomial F(x) = A(x*x) + Bx + C.
		 * Backlight curve should always  satisfy
		 * F(0) = min, F(100) = max, so polinomial coefficients are:
		 * A is 0.0255 - B/100 - min/10000 -
		 * (255-max)/10000 = (max - min)/10000 - B/100
		 * B is adjustable factor to modify the curve.
		 * Bigger B results in less concave curve.
		 * B range is [0..(max-min)/100]
		 * C is backlight minimum
		 */
		uint16_t delta = as->backlight_8bit_lut[100] -
				as->backlight_8bit_lut[0];
		uint16_t coeffc = as->backlight_8bit_lut[0];
		uint16_t coeffb = (BACKLIGHT_CURVE_COEFFB < delta ?
				BACKLIGHT_CURVE_COEFFB : delta);
		uint16_t coeffa = delta - coeffb;
		uint16_t i;
		uint32_t temp;

		for (i = 1; i < 100 ; i++) {
			temp = (coeffa * i * i) / BACKLIGHT_CURVE_COEFFA_FACTOR;
			as->backlight_8bit_lut[i] = temp + (coeffb * i) /
					BACKLIGHT_CURVE_COEFFB_FACTOR + coeffc;
		}
	}
	as->backlight_caps_initialized = true;
}

static void log_overriden_features(
	struct adapter_service *as,
	const char *feature_name,
	enum adapter_feature_id id,
	bool bool_feature,
	uint32_t value)
{
	if (bool_feature)
		dal_logger_write(as->ctx->logger,
			LOG_MAJOR_FEATURE_OVERRIDE,
			LOG_MINOR_FEATURE_OVERRIDE,
			"Overridden %s is %s now\n",
			feature_name,
			(value == 0) ? "disabled" : "enabled");
	else
		dal_logger_write(as->ctx->logger,
			LOG_MAJOR_FEATURE_OVERRIDE,
			LOG_MINOR_FEATURE_OVERRIDE,
			"Overridden %s new value: %d\n",
			feature_name,
			value);
}

/*************************************
 * Local static functions definition *
 *************************************/

#define check_bool_feature(feature) \
case FEATURE_ ## feature: \
	if (param->bool_param_enable_mask & \
		(1 << DAL_PARAM_ ## feature)) { \
		*data = param->bool_param_values & \
		(1 << DAL_PARAM_ ## feature); \
		ret = true; \
		feature_name = "FEATURE_" #feature; \
	} \
	break

#define check_int_feature(feature) \
case FEATURE_ ## feature: \
	if (param->int_param_values[DAL_PARAM_ ## feature] != \
		DAL_PARAM_INVALID_INT) { \
		*data = param->int_param_values[DAL_PARAM_ ## feature];\
		ret = true;\
		bool_feature = false;\
		feature_name = "FEATURE_" #feature;\
	} \
	break

/*
 * override_default_parameters
 *
 * Override features (from runtime parameter)
 * corresponding to Adapter Service Feature ID
 */
static bool override_default_parameters(
	struct adapter_service *as,
	const struct dal_override_parameters *param,
	const uint32_t idx,
	uint32_t *data)
{
	bool ret = false;
	bool bool_feature = true;
	char *feature_name;

	if (idx >= get_feature_entries_num()) {
		ASSERT_CRITICAL(false);
		return false;
	}

	switch (feature_entry_table[idx].feature_id) {
	check_int_feature(MAX_COFUNC_NON_DP_DISPLAYS);
	check_int_feature(DRR_SUPPORT);
	check_bool_feature(LIGHT_SLEEP);
	check_bool_feature(MAXIMIZE_STUTTER_MARKS);
	check_bool_feature(MAXIMIZE_URGENCY_WATERMARKS);
	check_bool_feature(USE_MAX_DISPLAY_CLK);
	check_bool_feature(ENABLE_DFS_BYPASS);
	check_bool_feature(POWER_GATING_PIPE_IN_TILE);
	check_bool_feature(POWER_GATING_LB_PORTION);
	check_bool_feature(PSR_ENABLE);
	check_bool_feature(VARI_BRIGHT_ENABLE);
	check_bool_feature(USE_PPLIB);
	check_bool_feature(DISABLE_LPT_SUPPORT);
	check_bool_feature(DUMMY_FBC_BACKEND);
	check_bool_feature(ENABLE_GPU_SCALING);
	default:
		return false;
	}
	if (ret)
		log_overriden_features(
			as,
			feature_name,
			feature_entry_table[idx].feature_id,
			bool_feature,
			*data);

	return ret;
}

/*
 * get_feature_value_from_data_sources
 *
 * For a given feature, determine its value from ASIC cap and wireless
 * data source.
 * idx : index of feature_entry_table for the feature id.
 */
static bool get_feature_value_from_data_sources(
		const struct adapter_service *as,
		const uint32_t idx,
		uint32_t *data)
{
	if (idx >= get_feature_entries_num()) {
		ASSERT_CRITICAL(false);
		return false;
	}

	switch (feature_entry_table[idx].feature_id) {
	case FEATURE_MAX_COFUNC_NON_DP_DISPLAYS:
		*data = as->asic_cap->data[ASIC_DATA_MAX_COFUNC_NONDP_DISPLAYS];
		break;

	case FEATURE_WIRELESS_LIMIT_720P:
		*data = as->asic_cap->caps.WIRELESS_LIMIT_TO_720P;
		break;

	case FEATURE_WIRELESS_FULL_TIMING_ADJUSTMENT:
		*data = as->asic_cap->caps.WIRELESS_FULL_TIMING_ADJUSTMENT;
		break;

	case FEATURE_MODIFY_TIMINGS_FOR_WIRELESS:
		*data = as->asic_cap->caps.WIRELESS_TIMING_ADJUSTMENT;
		break;

	case FEATURE_SUPPORTED_HDMI_CONNECTION_NUM:
		*data =
		as->asic_cap->data[ASIC_DATA_SUPPORTED_HDMI_CONNECTION_NUM];
		break;

	case FEATURE_DETECT_REQUIRE_HPD_HIGH:
		*data = as->asic_cap->caps.HPD_CHECK_FOR_EDID;
		break;

	case FEATURE_NO_HPD_LOW_POLLING_VCC_OFF:
		*data = as->asic_cap->caps.NO_VCC_OFF_HPD_POLLING;
		break;

	case FEATURE_STUTTER_MODE:
		*data = as->asic_cap->data[ASIC_DATA_STUTTERMODE];
		break;

	case FEATURE_WIRELESS_ENABLE:
		*data = as->wireless_data.wireless_enable;
		break;

	case FEATURE_8BPP_SUPPORTED:
		*data = as->asic_cap->caps.SUPPORT_8BPP;
		break;

	default:
		return false;
	}

	return true;
}

/* get_bool_value
 *
 * Get the boolean value of a given feature
 */
static bool get_bool_value(
	const uint32_t set,
	const uint32_t idx)
{
	if (idx >= 32) {
		ASSERT_CRITICAL(false);
		return false;
	}

	return ((set & (1 << idx)) != 0);
}

/*
 * get_hpd_info
 *
 * Get HPD information from BIOS
 */
static bool get_hpd_info(struct adapter_service *as,
	struct graphics_object_id id,
	struct graphics_object_hpd_info *info)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	return BP_RESULT_OK == dcb->funcs->get_hpd_info(dcb, id, info);
}

/*
 * lookup_feature_entry
 *
 * Find the entry index of a given feature in feature table
 */
static uint32_t lookup_feature_entry(
	enum adapter_feature_id feature_id)
{
	uint32_t entries_num = get_feature_entries_num();
	uint32_t i = 0;

	while (i != entries_num) {
		if (feature_entry_table[i].feature_id == feature_id)
			break;

		++i;
	}

	return i;
}

/*
 * set_bool_value
 *
 * Set the boolean value of a given feature
 */
static void set_bool_value(
	uint32_t *set,
	const uint32_t idx,
	bool value)
{
	if (idx >= 32) {
		ASSERT_CRITICAL(false);
		return;
	}

	if (value)
		*set |= (1 << idx);
	else
		*set &= ~(1 << idx);
}

/*
 * generate_feature_set
 *
 * Generate the internal feature set from multiple data sources
 */
static bool generate_feature_set(
		struct adapter_service *as,
		const struct dal_override_parameters *param)
{
	uint32_t i = 0;
	uint32_t value = 0;
	uint32_t set_idx = 0;
	uint32_t internal_idx = 0;
	uint32_t entry_num = 0;
	const struct feature_source_entry *entry = NULL;

	dc_service_memset(adapter_feature_set, 0, sizeof(adapter_feature_set));
	entry_num = get_feature_entries_num();


	while (i != entry_num) {
		entry = &feature_entry_table[i];

		if (entry->feature_id <= FEATURE_UNKNOWN ||
				entry->feature_id >= FEATURE_MAXIMUM) {
			ASSERT_CRITICAL(false);
			return false;
		}

		set_idx = (uint32_t)((entry->feature_id - 1) / 32);
		internal_idx = (uint32_t)((entry->feature_id - 1) % 32);

		/* TODO: wireless, runtime parameter, vbios */
		if (!override_default_parameters(as, param, i, &value)) {
			if (!get_feature_value_from_data_sources(
					as, i, &value)) {
				/*
				 * Can't find feature values from
				 * above data sources
				 * Assign default value
				 */
				value = entry->default_value;
			}
		}

		if (entry->is_boolean_type)
			set_bool_value(&adapter_feature_set[set_idx],
					internal_idx,
					value != 0);
		else
			adapter_feature_set[set_idx] = value;

		i++;
	}

	return true;
}


/*
 * create_hw_ctx
 *
 * Create HW context for adapter service. This is DCE specific.
 */
static struct hw_ctx_adapter_service *create_hw_ctx(
	enum dce_version dce_version,
	enum dce_environment dce_environment,
	struct dc_context *ctx)
{
	switch (dce_environment) {
	case DCE_ENV_DIAG_FPGA_MAXIMUS:
		return dal_adapter_service_create_hw_ctx_diag(ctx);
	default:
		break;
	}

	switch (dce_version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
		return dal_adapter_service_create_hw_ctx_dce110(ctx);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dal_adapter_service_create_hw_ctx_dce110(ctx);
#endif
	default:
		ASSERT_CRITICAL(false);
		return NULL;
	}
}

/*
 * adapter_service_destruct
 *
 * Release memory of objects in adapter service
 */
static void adapter_service_destruct(
	struct adapter_service *as)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	dal_adapter_service_destroy_hw_ctx(&as->hw_ctx);
	dal_i2caux_destroy(&as->i2caux);
	dal_gpio_service_destroy(&as->gpio_service);
	dal_asic_capability_destroy(&as->asic_cap);

	dcb->funcs->destroy_integrated_info(dcb, &as->integrated_info);

	if (as->dcb_internal) {
		/* We are responsible only for destruction of Internal BIOS.
		 * The External one will be destroyed by its creator. */
		dal_bios_parser_destroy(&as->dcb_internal);
	}
}

/*
 * adapter_service_construct
 *
 * Construct the derived type of adapter service
 */
static bool adapter_service_construct(
	struct adapter_service *as,
	struct as_init_data *init_data)
{
	struct dc_bios *dcb;
	enum dce_version dce_version;

	if (!init_data)
		return false;

	/* Create ASIC capability */
	as->ctx = init_data->ctx;
	as->asic_cap = dal_asic_capability_create(
			&init_data->hw_init_data, as->ctx);

	if (!as->asic_cap) {
		ASSERT_CRITICAL(false);
		return false;
	}

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	if (dal_adapter_service_get_dce_version(as) == DCE_VERSION_11_0) {
		uint32_t i;

		for (i = 0; i < ARRAY_SIZE(feature_entry_table); i++) {
			enum adapter_feature_id id =
				feature_entry_table[i].feature_id;
			if (id == FEATURE_MAXIMIZE_URGENCY_WATERMARKS ||
				id == FEATURE_MAXIMIZE_STUTTER_MARKS ||
				id == FEATURE_MAXIMIZE_NBP_MARKS)
				feature_entry_table[i].default_value = true;
		}
	}
#endif

	/* Generate feature set table */
	if (!generate_feature_set(as, init_data->display_param)) {
		ASSERT_CRITICAL(false);
		goto failed_to_generate_features;
	}

	as->dce_environment = init_data->dce_environment;

	if (init_data->vbios_override)
		as->dcb_override = init_data->vbios_override;
	else {
		/* Create BIOS parser */
		init_data->bp_init_data.ctx = init_data->ctx;

		as->dcb_internal = dal_bios_parser_create(
				&init_data->bp_init_data, as);

		if (!as->dcb_internal) {
			ASSERT_CRITICAL(false);
			goto failed_to_create_bios_parser;
		}
	}

	dcb = dal_adapter_service_get_bios_parser(as);

	dce_version = dal_adapter_service_get_dce_version(as);

	/* Create GPIO service */
	as->gpio_service = dal_gpio_service_create(
			dce_version,
			as->dce_environment,
			as->ctx);

	if (!as->gpio_service) {
		ASSERT_CRITICAL(false);
		goto failed_to_create_gpio_service;
	}

	/* Create I2C AUX */
	as->i2caux = dal_i2caux_create(as, as->ctx);

	if (!as->i2caux) {
		ASSERT_CRITICAL(false);
		goto failed_to_create_i2caux;
	}

	/* Create Adapter Service HW Context*/
	as->hw_ctx = create_hw_ctx(
			dce_version,
			as->dce_environment,
			as->ctx);

	if (!as->hw_ctx) {
		ASSERT_CRITICAL(false);
		goto failed_to_create_hw_ctx;
	}

	/* Avoid wireless encoder creation in upstream branch. */

	/* Integrated info is not provided on discrete ASIC. NULL is allowed */
	as->integrated_info = dcb->funcs->create_integrated_info(dcb);

	dcb->funcs->post_init(dcb);

	/* Generate backlight translation table and initializes
			  other brightness properties */
	as->backlight_caps_initialized = false;

	get_platform_info_methods(as);

	initialize_backlight_caps(as);

	return true;

failed_to_generate_features:
	dal_adapter_service_destroy_hw_ctx(&as->hw_ctx);

failed_to_create_hw_ctx:
	dal_i2caux_destroy(&as->i2caux);

failed_to_create_i2caux:
	dal_gpio_service_destroy(&as->gpio_service);

failed_to_create_gpio_service:
	if (as->dcb_internal)
		dal_bios_parser_destroy(&as->dcb_internal);

failed_to_create_bios_parser:
	dal_asic_capability_destroy(&as->asic_cap);

	return false;
}

/*
 * Global function definition
 */

/*
 * dal_adapter_service_create
 *
 * Create adapter service
 */
struct adapter_service *dal_adapter_service_create(
	struct as_init_data *init_data)
{
	struct adapter_service *as;

	as = dc_service_alloc(init_data->ctx, sizeof(struct adapter_service));

	if (!as) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (adapter_service_construct(as, init_data))
		return as;

	ASSERT_CRITICAL(false);

	dc_service_free(init_data->ctx, as);

	return NULL;
}

/*
 * dal_adapter_service_destroy
 *
 * Destroy adapter service and objects it contains
 */
void dal_adapter_service_destroy(
	struct adapter_service **as)
{
	if (!as) {
		ASSERT_CRITICAL(false);
		return;
	}

	if (!*as) {
		ASSERT_CRITICAL(false);
		return;
	}

	adapter_service_destruct(*as);

	dc_service_free((*as)->ctx, *as);

	*as = NULL;
}

/*
 * dal_adapter_service_get_dce_version
 *
 * Get the DCE version of current ASIC
 */
enum dce_version dal_adapter_service_get_dce_version(
	const struct adapter_service *as)
{
	uint32_t version = as->asic_cap->data[ASIC_DATA_DCE_VERSION];

	switch (version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case 0x110:
		return DCE_VERSION_11_0;
#endif
	default:
		ASSERT_CRITICAL(false);
		return DCE_VERSION_UNKNOWN;
	}
}

enum dce_environment dal_adapter_service_get_dce_environment(
	const struct adapter_service *as)
{
	return as->dce_environment;
}


/*
 * dal_adapter_service_get_controllers_num
 *
 * Get number of controllers
 */
uint8_t dal_adapter_service_get_controllers_num(
	struct adapter_service *as)
{
	uint32_t result = as->asic_cap->data[ASIC_DATA_CONTROLLERS_NUM];

	/* Check the "max num of controllers" feature,
	 * use it for debugging purposes only */
	/* TODO implement
	 * dal_adapter_service_get_feature_value(as, ) */

	return result;
}

/** Get total number of connectors.
 *
 * \param as	Adapter Service
 *
 * \return Total number of connectors. It is up-to-the caller to decide
 *	if the number is valid.
 */
uint8_t dal_adapter_service_get_connectors_num(
	struct adapter_service *as)
{
	uint8_t vbios_connectors_num = 0;
	uint8_t wireless_connectors_num = 0;
	struct dc_bios *dcb;

	dcb = dal_adapter_service_get_bios_parser(as);

	vbios_connectors_num = dcb->funcs->get_connectors_number(dcb);

	wireless_connectors_num = wireless_get_connectors_num(as);

	return vbios_connectors_num + wireless_connectors_num;
}

static bool is_wireless_object(struct graphics_object_id id)
{
	if ((id.type == OBJECT_TYPE_ENCODER &&
		id.id == ENCODER_ID_INTERNAL_WIRELESS) ||
		(id.type == OBJECT_TYPE_CONNECTOR && id.id ==
			CONNECTOR_ID_WIRELESS) ||
		(id.type == OBJECT_TYPE_CONNECTOR && id.id ==
			CONNECTOR_ID_MIRACAST))
		return true;
	return false;
}

/**
 * Get the number of source objects of an object
 *
 * \param [in] as: Adapter Service
 *
 * \param [in] id: The graphics object id
 *
 * \return
 *     The number of the source objects of an object
 */
uint32_t dal_adapter_service_get_src_num(
	struct adapter_service *as, struct graphics_object_id id)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (is_wireless_object(id))
		return wireless_get_srcs_num(as, id);
	else
		return dcb->funcs->get_src_number(dcb, id);
}

/**
 * Get the source objects of an object
 *
 * \param [in] id      The graphics object id
 * \param [in] index   Enumerating index which starts at 0
 *
 * \return If enumerating successfully, return the VALID source object id,
 *	otherwise, returns "zeroed out" object id.
 *	Client should call dal_graphics_object_id_is_valid() to check
 *	weather the id is valid.
 */
struct graphics_object_id dal_adapter_service_get_src_obj(
	struct adapter_service *as,
	struct graphics_object_id id,
	uint32_t index)
{
	struct graphics_object_id src_object_id;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (is_wireless_object(id))
		src_object_id = wireless_get_src_obj_id(as, id, index);
	else {
		if (BP_RESULT_OK != dcb->funcs->get_src_obj(dcb, id, index,
				&src_object_id)) {
			src_object_id =
				dal_graphics_object_id_init(
					0,
					ENUM_ID_UNKNOWN,
					OBJECT_TYPE_UNKNOWN);
		}
	}

	return src_object_id;
}

/** Get connector object id associated with a connector index.
 *
 * \param as	Adapter Service
 *
 * \param connector_index Index of connector between zero and total number
 *	returned by dal_adapter_service_get_connectors_num()
 *
 * \return graphics object id corresponding to the connector_index.
 */
struct graphics_object_id dal_adapter_service_get_connector_obj_id(
		struct adapter_service *as,
		uint8_t connector_index)
{
	struct dc_bios *dcb;
	uint8_t bios_connectors_num;

	dcb = dal_adapter_service_get_bios_parser(as);

	bios_connectors_num = dcb->funcs->get_connectors_number(dcb);

	if (connector_index >= bios_connectors_num)
		return wireless_get_connector_id(as, connector_index);
	else
		return dcb->funcs->get_connector_id(dcb, connector_index);
}

bool dal_adapter_service_get_device_tag(
		struct adapter_service *as,
		struct graphics_object_id connector_object_id,
		uint32_t device_tag_index,
		struct connector_device_tag_info *info)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (BP_RESULT_OK == dcb->funcs->get_device_tag(dcb,
			connector_object_id, device_tag_index, info))
		return true;
	else
		return false;
}

/* Check if DeviceId is supported by ATOM_OBJECT_HEADER support info */
bool dal_adapter_service_is_device_id_supported(struct adapter_service *as,
		struct device_id id)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	return dcb->funcs->is_device_id_supported(dcb, id);
}

bool dal_adapter_service_is_meet_underscan_req(struct adapter_service *as)
{
	struct firmware_info fw_info;
	enum bp_result bp_result = dal_adapter_service_get_firmware_info(
		as, &fw_info);
	uint32_t disp_clk_limit =
		as->asic_cap->data[ASIC_DATA_MIN_DISPCLK_FOR_UNDERSCAN];
	if (BP_RESULT_OK == bp_result) {
		dal_logger_write(as->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_ADAPTER_SERVICE,
			"Read firmware is NULL");
		return false;
	}
	if (fw_info.default_display_engine_pll_frequency < disp_clk_limit)
		return false;
	return true;
}

bool dal_adapter_service_underscan_for_hdmi_only(struct adapter_service *as)
{
	return as->asic_cap->caps.UNDERSCAN_FOR_HDMI_ONLY;
}
/*
 * dal_adapter_service_get_clock_sources_num
 *
 * Get number of clock sources
 */
uint8_t dal_adapter_service_get_clock_sources_num(
	struct adapter_service *as)
{
	struct firmware_info fw_info;
	uint32_t max_clk_src = 0;
	uint32_t num = as->asic_cap->data[ASIC_DATA_CLOCKSOURCES_NUM];
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	/*
	 * Check is system supports the use of the External clock source
	 * as a clock source for DP
	 */
	enum bp_result bp_result = dcb->funcs->get_firmware_info(dcb, &fw_info);

	if (BP_RESULT_OK == bp_result &&
			fw_info.external_clock_source_frequency_for_dp != 0)
		++num;

	/*
	 * Add clock source for wireless if supported
	 */
	num += (uint32_t)wireless_get_clocks_num(as);

	/* Check the "max number of clock sources" feature */
	if (dal_adapter_service_get_feature_value(
			FEATURE_MAX_CLOCK_SOURCE_NUM,
			&max_clk_src,
			sizeof(uint32_t)))
		if ((max_clk_src != 0) && (max_clk_src < num))
			num = max_clk_src;

	return num;
}

/*
 * dal_adapter_service_get_func_controllers_num
 *
 * Get number of controllers
 */
uint8_t dal_adapter_service_get_func_controllers_num(
	struct adapter_service *as)
{
	uint32_t result =
		as->asic_cap->data[ASIC_DATA_FUNCTIONAL_CONTROLLERS_NUM];

	/* Check the "max num of controllers" feature,
	 * use it for debugging purposes only */

	/* Limit number of controllers by OS */

	struct asic_feature_flags flags;

	flags.raw = as->asic_cap->data[ASIC_DATA_FEATURE_FLAGS];

	if (flags.bits.LEGACY_CLIENT &&
		(result > LEGACY_MAX_NUM_OF_CONTROLLERS))
		result = LEGACY_MAX_NUM_OF_CONTROLLERS;

	return result;
}

/*
 * dal_adapter_service_is_feature_supported
 *
 * Return if a given feature is supported by the ASIC. The feature has to be
 * a boolean type.
 */
bool dal_adapter_service_is_feature_supported(
	enum adapter_feature_id feature_id)
{
	bool data = 0;

	dal_adapter_service_get_feature_value(feature_id, &data, sizeof(bool));

	return data;
}

/**
 * Reports maximum number of confunctional non-DP displays.
 * Value can be overriden if FEATURE_REPORT_SINGLE_SELECTED_TIMING feature is
 * enabled.
 *
 * \return
 *     Maximum number of confunctional non-DP displays
 */
uint32_t dal_adapter_service_get_max_cofunc_non_dp_displays(void)
{
	uint32_t non_dp_displays = DEFAULT_NUM_COFUNC_NON_DP_DISPLAYS;

	if (true == dal_adapter_service_get_feature_value(
			FEATURE_MAX_COFUNC_NON_DP_DISPLAYS,
			&non_dp_displays,
			sizeof(non_dp_displays))) {
		/* the cached value exist */
		/* TODO: add more logic as per-DAL2 */
	}

	return non_dp_displays;
}

uint32_t dal_adapter_service_get_single_selected_timing_signals(void)
{
	uint32_t signals_bitmap = 0;

	if (dal_adapter_service_is_feature_supported(
			FEATURE_REPORT_SINGLE_SELECTED_TIMING)) {
		/* the cached value exist */
		/* TODO: add more logic as per-DAL2 */
		signals_bitmap = 0;
	}

	return signals_bitmap;
}

/*
 * dal_adapter_service_get_i2c_info
 *
 * Get I2C information from BIOS
 */
bool dal_adapter_service_get_i2c_info(
	struct adapter_service *as,
	struct graphics_object_id id,
	struct graphics_object_i2c_info *i2c_info)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (!i2c_info) {
		ASSERT_CRITICAL(false);
		return false;
	}

	return BP_RESULT_OK == dcb->funcs->get_i2c_info(dcb, id, i2c_info);
}

/*
 * dal_adapter_service_obtain_ddc
 *
 * Obtain DDC
 */
struct ddc *dal_adapter_service_obtain_ddc(
	struct adapter_service *as,
	struct graphics_object_id id)
{
	struct graphics_object_i2c_info i2c_info;
	struct gpio_ddc_hw_info hw_info;


	if (!dal_adapter_service_get_i2c_info(as, id, &i2c_info))
		return NULL;

	hw_info.ddc_channel = i2c_info.i2c_line;
	hw_info.hw_supported = i2c_info.i2c_hw_assist;

	return dal_gpio_service_create_ddc(
		as->gpio_service,
		i2c_info.gpio_info.clk_a_register_index,
		1 << i2c_info.gpio_info.clk_a_shift,
		&hw_info);
}

/*
 * dal_adapter_service_release_ddc
 *
 * Release DDC
 */
void dal_adapter_service_release_ddc(
	struct adapter_service *as,
	struct ddc *ddc)
{
	dal_gpio_service_destroy_ddc(&ddc);
}

/*
 * dal_adapter_service_obtain_hpd_irq
 *
 * Obtain HPD interrupt request
 */
struct irq *dal_adapter_service_obtain_hpd_irq(
	struct adapter_service *as,
	struct graphics_object_id id)
{
	enum bp_result bp_result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);
	struct graphics_object_hpd_info hpd_info;
	struct gpio_pin_info pin_info;

	if (!get_hpd_info(as, id, &hpd_info))
		return NULL;

	bp_result = dcb->funcs->get_gpio_pin_info(dcb,
		hpd_info.hpd_int_gpio_uid, &pin_info);

	if (bp_result != BP_RESULT_OK) {
		ASSERT(bp_result == BP_RESULT_NORECORD);
		return NULL;
	}

	return dal_gpio_service_create_irq(
		as->gpio_service,
		pin_info.offset,
		pin_info.mask);
}

/*
 * dal_adapter_service_release_irq
 *
 * Release interrupt request
 */
void dal_adapter_service_release_irq(
	struct adapter_service *as,
	struct irq *irq)
{
	dal_gpio_service_destroy_irq(&irq);
}

/*
 * dal_adapter_service_get_ss_info_num
 *
 * Get number of spread spectrum entries from BIOS
 */
uint32_t dal_adapter_service_get_ss_info_num(
	struct adapter_service *as,
	enum as_signal_type signal)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	return dcb->funcs->get_ss_entry_number(dcb, signal);
}

/*
 * dal_adapter_service_get_ss_info
 *
 * Get spread spectrum info from BIOS
 */
bool dal_adapter_service_get_ss_info(
	struct adapter_service *as,
	enum as_signal_type signal,
	uint32_t idx,
	struct spread_spectrum_info *info)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	enum bp_result bp_result = dcb->funcs->get_spread_spectrum_info(dcb,
			signal, idx, info);

	return BP_RESULT_OK == bp_result;
}

/*
 * dal_adapter_service_get_integrated_info
 *
 * Get integrated information on BIOS
 */
bool dal_adapter_service_get_integrated_info(
	struct adapter_service *as,
	struct integrated_info *info)
{
	if (info == NULL || as->integrated_info == NULL)
		return false;

	dc_service_memmove(info, as->integrated_info, sizeof(struct integrated_info));

	return true;
}

/*
 * dal_adapter_service_is_dfs_bypass_enabled
 *
 * Check if DFS bypass is enabled
 */
bool dal_adapter_service_is_dfs_bypass_enabled(
	struct adapter_service *as)
{
	if (as->integrated_info == NULL)
		return false;
	if ((as->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE) &&
		dal_adapter_service_is_feature_supported(
			FEATURE_ENABLE_DFS_BYPASS))
		return true;
	else
		return false;
}

/*
 * dal_adapter_service_get_sw_i2c_speed
 *
 * Get SW I2C speed
 */
uint32_t dal_adapter_service_get_sw_i2c_speed(
	struct adapter_service *as)
{
	/* TODO: only from ASIC caps. Feature key is not implemented*/
	return as->asic_cap->data[ASIC_DATA_DEFAULT_I2C_SPEED_IN_KHZ];
}

/*
 * dal_adapter_service_get_hw_i2c_speed
 *
 * Get HW I2C speed
 */
uint32_t dal_adapter_service_get_hw_i2c_speed(
	struct adapter_service *as)
{
	/* TODO: only from ASIC caps. Feature key is not implemented*/
	return as->asic_cap->data[ASIC_DATA_DEFAULT_I2C_SPEED_IN_KHZ];
}

/*
 * dal_adapter_service_get_mc_latency
 *
 * Get memory controller latency
 */
uint32_t dal_adapter_service_get_mc_latency(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_MC_LATENCY];
}

/*
 * dal_adapter_service_get_asic_vram_bit_width
 *
 * Get the video RAM bit width set on the ASIC
 */
uint32_t dal_adapter_service_get_asic_vram_bit_width(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_VRAM_BITWIDTH];
}

/*
 * dal_adapter_service_get_asic_bugs
 *
 * Get the bug flags set on this ASIC
 */
struct asic_bugs dal_adapter_service_get_asic_bugs(
	struct adapter_service *as)
{
	return as->asic_cap->bugs;
}


struct dal_asic_runtime_flags dal_adapter_service_get_asic_runtime_flags(
		struct adapter_service *as)
{
	return as->asic_cap->runtime_flags;
}

/*
 * dal_adapter_service_get_line_buffer_size
 *
 * Get line buffer size
 */
uint32_t dal_adapter_service_get_line_buffer_size(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_LINEBUFFER_SIZE];
}

/*
 * dal_adapter_service_get_bandwidth_tuning_params
 *
 * Get parameters for bandwidth tuning
 */
bool dal_adapter_service_get_bandwidth_tuning_params(
	struct adapter_service *as,
	union bandwidth_tuning_params *params)
{
	/* TODO: add implementation */
	/* note: data comes from runtime parameters */
	return false;
}

/*
 * dal_adapter_service_get_feature_flags
 *
 * Get a copy of ASIC feature flags
 */
struct asic_feature_flags dal_adapter_service_get_feature_flags(
	struct adapter_service *as)
{
	struct asic_feature_flags result = { { 0 } };

	if (!as) {
		ASSERT_CRITICAL(false);
		return result;
	}

	result.raw = as->asic_cap->data[ASIC_DATA_FEATURE_FLAGS];

	return result;
}

/*
 * dal_adapter_service_get_dram_bandwidth_efficiency
 *
 * Get efficiency of DRAM
 */
uint32_t dal_adapter_service_get_dram_bandwidth_efficiency(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_DRAM_BANDWIDTH_EFFICIENCY];
}

/*
 * dal_adapter_service_obtain_gpio
 *
 * Obtain GPIO
 */
struct gpio *dal_adapter_service_obtain_gpio(
	struct adapter_service *as,
	enum gpio_id id,
	uint32_t en)
{
	return dal_gpio_service_create_gpio_ex(
		as->gpio_service, id, en,
		GPIO_PIN_OUTPUT_STATE_DEFAULT);
}

/*
 * dal_adapter_service_obtain_stereo_gpio
 *
 * Obtain GPIO for stereo3D
 */
struct gpio *dal_adapter_service_obtain_stereo_gpio(
	struct adapter_service *as)
{
	const bool have_param_stereo_gpio = false;

	struct asic_feature_flags result;

	result.raw = as->asic_cap->data[ASIC_DATA_FEATURE_FLAGS];

	/* Case 1 : Workstation stereo */
	if (result.bits.WORKSTATION_STEREO) {
		/* "active low" <--> "default 3d right eye polarity" = false */
		return dal_gpio_service_create_gpio_ex(as->gpio_service,
				GPIO_ID_GENERIC, GPIO_GENERIC_A,
				GPIO_PIN_OUTPUT_STATE_ACTIVE_LOW);
	/* Case 2 : runtime parameter override for sideband stereo */
	} else if (have_param_stereo_gpio) {
		/* TODO implement */
		return NULL;
		/* Case 3 : VBIOS gives us GPIO for sideband stereo */
	} else {
		const struct graphics_object_id id =
				dal_graphics_object_id_init(GENERIC_ID_STEREO,
						ENUM_ID_1, OBJECT_TYPE_GENERIC);

		struct bp_gpio_cntl_info cntl_info;
		struct gpio_pin_info pin_info;
		struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

		/* Get GPIO record for this object.
		 * Stereo GPIO record should have exactly one entry
		 * where active state defines stereosync polarity */
		if (1 != dcb->funcs->get_gpio_record(
						dcb, id, &cntl_info,
						1)) {
			return NULL;
		} else if (BP_RESULT_OK
				!= dcb->funcs->get_gpio_pin_info(
						dcb, cntl_info.id,
						&pin_info)) {
			/*ASSERT_CRITICAL(false);*/
			return NULL;
		} else {
			return dal_gpio_service_create_gpio_ex(as->gpio_service,
					pin_info.offset, pin_info.mask,
					cntl_info.state);
		}
	}
}

/*
 * dal_adapter_service_release_gpio
 *
 * Release GPIO
 */
void dal_adapter_service_release_gpio(
	struct adapter_service *as,
	struct gpio *gpio)
{
	dal_gpio_service_destroy_gpio(&gpio);
}

/*
 * dal_adapter_service_get_firmware_info
 *
 * Get firmware information from BIOS
 */
bool dal_adapter_service_get_firmware_info(
	struct adapter_service *as,
	struct firmware_info *info)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	return dcb->funcs->get_firmware_info(dcb, info) == BP_RESULT_OK;
}

/*
 * dal_adapter_service_get_audio_support
 *
 * Get information on audio support
 */
union audio_support dal_adapter_service_get_audio_support(
	struct adapter_service *as)
{
	return dal_adapter_service_hw_ctx_get_audio_support(as->hw_ctx);
}

/*
 * dal_adapter_service_get_stream_engines_num
 *
 * Get number of stream engines
 */
uint8_t dal_adapter_service_get_stream_engines_num(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_DIGFE_NUM];
}

/*
 * dal_adapter_service_get_feature_value
 *
 * Get the cached value of a given feature. This value can be a boolean, int,
 * or characters.
 */
bool dal_adapter_service_get_feature_value(
	const enum adapter_feature_id feature_id,
	void *data,
	uint32_t size)
{
	uint32_t entry_idx = 0;
	uint32_t set_idx = 0;
	uint32_t set_internal_idx = 0;

	if (feature_id >= FEATURE_MAXIMUM || feature_id <= FEATURE_UNKNOWN) {
		ASSERT_CRITICAL(false);
		return false;
	}

	if (data == NULL) {
		ASSERT_CRITICAL(false);
		return false;
	}

	entry_idx = lookup_feature_entry(feature_id);
	set_idx = (uint32_t)((feature_id - 1)/32);
	set_internal_idx = (uint32_t)((feature_id - 1) % 32);

	if (entry_idx >= get_feature_entries_num()) {
		/* Cannot find this entry */
		ASSERT_CRITICAL(false);
		return false;
	}

	if (feature_entry_table[entry_idx].is_boolean_type) {
		if (size != sizeof(bool)) {
			ASSERT_CRITICAL(false);
			return false;
		}

		*(bool *)data = get_bool_value(adapter_feature_set[set_idx],
				set_internal_idx);
	} else {
		if (size != sizeof(uint32_t)) {
			ASSERT_CRITICAL(false);
			return false;
		}

		*(uint32_t *)data = adapter_feature_set[set_idx];
	}

	return true;
}

/*
 * dal_adapter_service_get_memory_type_multiplier
 *
 * Get multiplier for the memory type
 */
uint32_t dal_adapter_service_get_memory_type_multiplier(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_MEMORYTYPE_MULTIPLIER];
}

/*
 * dal_adapter_service_get_bios_parser
 *
 * Get BIOS parser handler
 */
struct dc_bios *dal_adapter_service_get_bios_parser(
	struct adapter_service *as)
{
	return as->dcb_override ? as->dcb_override : as->dcb_internal;
}

/*
 * dal_adapter_service_get_i2caux
 *
 * Get i2c aux handler
 */
struct i2caux *dal_adapter_service_get_i2caux(
	struct adapter_service *as)
{
	return as->i2caux;
}

bool dal_adapter_service_initialize_hw_data(
	struct adapter_service *as)
{
	return as->hw_ctx->funcs->power_up(as->hw_ctx);
}

struct graphics_object_id dal_adapter_service_enum_fake_path_resource(
	struct adapter_service *as,
	uint32_t index)
{
	return as->hw_ctx->funcs->enum_fake_path_resource(as->hw_ctx, index);
}

struct graphics_object_id dal_adapter_service_enum_stereo_sync_object(
	struct adapter_service *as,
	uint32_t index)
{
	return as->hw_ctx->funcs->enum_stereo_sync_object(as->hw_ctx, index);
}

struct graphics_object_id dal_adapter_service_enum_sync_output_object(
	struct adapter_service *as,
	uint32_t index)
{
	return as->hw_ctx->funcs->enum_sync_output_object(as->hw_ctx, index);
}

struct graphics_object_id dal_adapter_service_enum_audio_object(
	struct adapter_service *as,
	uint32_t index)
{
	return as->hw_ctx->funcs->enum_audio_object(as->hw_ctx, index);
}


void dal_adapter_service_update_audio_connectivity(
	struct adapter_service *as,
	uint32_t number_of_audio_capable_display_path)
{
	as->hw_ctx->funcs->update_audio_connectivity(
		as->hw_ctx,
		number_of_audio_capable_display_path,
		dal_adapter_service_get_controllers_num(as));
}

bool dal_adapter_service_has_embedded_display_connector(
	struct adapter_service *as)
{
	uint8_t index;
	uint8_t num_connectors = dal_adapter_service_get_connectors_num(as);

	if (num_connectors == 0 || num_connectors > ENUM_ID_COUNT)
		return false;

	for (index = 0; index < num_connectors; index++) {
		struct graphics_object_id obj_id =
			dal_adapter_service_get_connector_obj_id(as, index);
		enum connector_id connector_id =
			dal_graphics_object_id_get_connector_id(obj_id);

		if ((connector_id == CONNECTOR_ID_LVDS) ||
				(connector_id == CONNECTOR_ID_EDP))
			return true;
	}

	return false;
}

bool dal_adapter_service_get_embedded_panel_info(
	struct adapter_service *as,
	struct embedded_panel_info *info)
{
	enum bp_result result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (info == NULL)
		/*TODO: add DALASSERT_MSG here*/
		return false;

	result = dcb->funcs->get_embedded_panel_info(dcb, info);

	return result == BP_RESULT_OK;
}

bool dal_adapter_service_enum_embedded_panel_patch_mode(
	struct adapter_service *as,
	uint32_t index,
	struct embedded_panel_patch_mode *mode)
{
	enum bp_result result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (mode == NULL)
		/*TODO: add DALASSERT_MSG here*/
		return false;

	result = dcb->funcs->enum_embedded_panel_patch_mode(dcb, index, mode);

	return result == BP_RESULT_OK;
}

bool dal_adapter_service_get_faked_edid_len(
	struct adapter_service *as,
	uint32_t *len)
{
	enum bp_result result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	result = dcb->funcs->get_faked_edid_len(dcb, len);

	return result == BP_RESULT_OK;
}

bool dal_adapter_service_get_faked_edid_buf(
	struct adapter_service *as,
	uint8_t *buf,
	uint32_t len)
{
	enum bp_result result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	result = dcb->funcs->get_faked_edid_buf(dcb, buf, len);

	return result == BP_RESULT_OK;

}

/*
 * dal_adapter_service_is_fusion
 *
 * Is this Fusion ASIC
 */
bool dal_adapter_service_is_fusion(struct adapter_service *as)
{
	return as->asic_cap->caps.IS_FUSION;
}

/*
 * dal_adapter_service_is_dfsbyass_dynamic
 *
 *
 **/
bool dal_adapter_service_is_dfsbyass_dynamic(struct adapter_service *as)
{
	return as->asic_cap->caps.DFSBYPASS_DYNAMIC_SUPPORT;
}

/*
 * dal_adapter_service_should_optimize
 *
 * @brief Reports whether driver settings allow requested optimization
 *
 * @param
 * as: adapter service handler
 * feature: for which optimization is validated
 *
 * @return
 * true if requested feature can be optimized
 */
bool dal_adapter_service_should_optimize(
		struct adapter_service *as, enum optimization_feature feature)
{
	uint32_t supported_optimization = 0;
	struct dal_asic_runtime_flags flags;

	if (!dal_adapter_service_get_feature_value(FEATURE_OPTIMIZATION,
			&supported_optimization, sizeof(uint32_t)))
		return false;

	/* Retrieve ASIC runtime flags */
	flags = dal_adapter_service_get_asic_runtime_flags(as);

	/* Check runtime flags against different optimization features */
	switch (feature) {
	case OF_SKIP_HW_PROGRAMMING_ON_ENABLED_EMBEDDED_DISPLAY:
		if (!flags.flags.bits.OPTIMIZED_DISPLAY_PROGRAMMING_ON_BOOT)
			return false;
		break;

	case OF_SKIP_RESET_OF_ALL_HW_ON_S3RESUME:
		if (as->integrated_info == NULL ||
				!flags.flags.bits.SKIP_POWER_DOWN_ON_RESUME)
			return false;
		break;
	case OF_SKIP_POWER_DOWN_INACTIVE_ENCODER:
		if (!dal_adapter_service_get_asic_runtime_flags(as).flags.bits.
			SKIP_POWER_DOWN_ON_RESUME)
			return false;
		break;
	default:
		break;
	}

	return (supported_optimization & feature) != 0;
}

/*
 * dal_adapter_service_is_in_accelerated_mode
 *
 * @brief Determine if driver is in accelerated mode
 *
 * @param
 * as: Adapter Service handler
 *
 * @out
 * True if driver is in accelerated mode, false otherwise.
 */
bool dal_adapter_service_is_in_accelerated_mode(struct adapter_service *as)
{
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	return dcb->funcs->is_accelerated_mode(dcb);
}

struct ddc *dal_adapter_service_obtain_ddc_from_i2c_info(
	struct adapter_service *as,
	struct graphics_object_i2c_info *info)
{
	struct gpio_ddc_hw_info hw_info = {
		info->i2c_hw_assist,
		info->i2c_line };
	return dal_gpio_service_create_ddc(as->gpio_service,
		info->gpio_info.clk_a_register_index,
		(1 << info->gpio_info.clk_a_shift), &hw_info);
}

struct bdf_info dal_adapter_service_get_adapter_info(struct adapter_service *as)
{
	return as->bdf_info;
}

/*
 * dal_adapter_service_should_psr_skip_wait_for_pll_lock
 *
 * @brief Determine if this ASIC needs to wait on PLL lock bit
 *
 * @param
 * as: Adapter Service handle
 *
 * @out
 * True if ASIC does not need to wait for PLL lock bit, i.e. skip the wait.
 */
bool dal_adapter_service_should_psr_skip_wait_for_pll_lock(
	struct adapter_service *as)
{
	return as->asic_cap->caps.SKIP_PSR_WAIT_FOR_PLL_LOCK_BIT;
}

bool dal_adapter_service_is_lid_open(struct adapter_service *as)
{
	bool is_lid_open = false;
	struct platform_info_params params;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	params.data = &is_lid_open;
	params.method = PM_GET_LID_STATE;

	if ((PM_GET_LID_STATE & as->platform_methods_mask) &&
		dal_get_platform_info(as->ctx, &params))
		return is_lid_open;

#if defined(CONFIG_DRM_AMD_DAL_VBIOS_PRESENT)
	return dcb->funcs->is_lid_open(dcb);
#else
	return false;
#endif
}

bool dal_adapter_service_get_panel_backlight_default_levels(
	struct adapter_service *as,
	struct panel_backlight_levels *levels)
{
	if (!as->backlight_caps_initialized)
		return false;

	levels->ac_level_percentage = as->ac_level_percentage;
	levels->dc_level_percentage = as->dc_level_percentage;
	return true;
}

bool dal_adapter_service_get_panel_backlight_boundaries(
	struct adapter_service *as,
	struct panel_backlight_boundaries *boundaries)
{
	if (!as->backlight_caps_initialized)
		return false;
	if (boundaries != NULL) {
		boundaries->min_signal_level = as->backlight_8bit_lut[0];
		boundaries->max_signal_level =
			as->backlight_8bit_lut[SIZEOF_BACKLIGHT_LUT - 1];
		return true;
	}
	return false;
}


uint32_t dal_adapter_service_get_view_port_pixel_granularity(
	struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_VIEWPORT_PIXEL_GRANULARITY];
}

/**
 * Get number of paths per DP 1.2 connector from the runtime parameter if it
 * exists.
 * A check to see if MST is supported for the generation of ASIC is done
 *
 * \return
 *    Number of paths per DP 1.2 connector is exists in runtime parameters
 *    or ASIC cap
 */
uint32_t dal_adapter_service_get_num_of_path_per_dp_mst_connector(
		struct adapter_service *as)
{
	if (as->asic_cap->caps.DP_MST_SUPPORTED == 0) {
		/* ASIC doesn't support DP MST at all */
		return 0;
	}

	return as->asic_cap->data[ASIC_DATA_PATH_NUM_PER_DPMST_CONNECTOR];
}

uint32_t dal_adapter_service_get_num_of_underlays(
		struct adapter_service *as)
{
	return as->asic_cap->data[ASIC_DATA_NUM_OF_VIDEO_PLANES];
}

bool dal_adapter_service_get_encoder_cap_info(
		struct adapter_service *as,
		struct graphics_object_id id,
		struct graphics_object_encoder_cap_info *info)
{
	struct bp_encoder_cap_info bp_cap_info = {0};
	enum bp_result result;
	struct dc_bios *dcb = dal_adapter_service_get_bios_parser(as);

	if (NULL == info) {
		ASSERT_CRITICAL(false);
		return false;
	}

	/*
	 * Retrieve Encoder Capability Information from VBIOS and store the
	 * call result (success or fail)
	 * Info from VBIOS about HBR2 has two fields:
	 *
	 * - dpHbr2Cap: indicates supported/not supported by HW Encoder
	 * - dpHbr2En : indicates DP spec compliant/not compliant
	 */
	result = dcb->funcs->get_encoder_cap_info(dcb, id, &bp_cap_info);

	/* Set dp_hbr2_validated flag (it's equal to Enable) */
	info->dp_hbr2_validated = bp_cap_info.DP_HBR2_EN;

	if (result == BP_RESULT_OK) {
		info->dp_hbr2_cap = bp_cap_info.DP_HBR2_CAP;
		return true;
	}

	return false;
}

bool dal_adapter_service_is_mc_tuning_req(struct adapter_service *as)
{
	return as->asic_cap->caps.NEED_MC_TUNING ? true : false;
}
