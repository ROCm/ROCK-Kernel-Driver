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

#ifndef __DAL_ADAPTER_SERVICE_INTERFACE_H__
#define __DAL_ADAPTER_SERVICE_INTERFACE_H__

#include "grph_object_ctrl_defs.h"
#include "gpio_interface.h"
#include "ddc_interface.h"
#include "irq_interface.h"
#include "bios_parser_interface.h"
#include "adapter_service_types.h"
#include "dal_types.h"
#include "asic_capability_types.h"

/* forward declaration */
struct i2caux;
struct adapter_service;

/*
 * enum adapter_feature_id
 *
 * Definition of all adapter features
 *
 * The enumeration defines the IDs of all the adapter features. The enum
 * organizes all the features into several feature sets. The range of feature
 * set N is from ((N-1)*32+1) to (N*32). Because there may be three value-type
 * feature, boolean-type, unsigned char-type and unsinged int-type, the number
 * of features should be 32, 4 and 1 in the feature set accordingly.
 *
 * In a boolean-type feature set N, the enumeration value of the feature should
 * be ((N-1)*32+1), ((N-1)*32+2), ..., (N*32).
 *
 * In an unsigned char-type feature set N, the enumeration value of the
 * feature should be ((N-1)*32+1), ((N-1)*32+8), ((N-1)*32+16) and (N*32).
 *
 * In an unsigned int-type feature set N, the enumeration value of the feature
 * should be ((N-1)*32+1)
 */
enum adapter_feature_id {
	FEATURE_UNKNOWN = 0,

	/* Boolean set, up to 32 entries */
	FEATURE_ENABLE_HW_EDID_POLLING = 1,
	FEATURE_SET_01_START = FEATURE_ENABLE_HW_EDID_POLLING,
	FEATURE_DP_SINK_DETECT_POLL_DATA_PIN,
	FEATURE_UNDERFLOW_INTERRUPT,
	FEATURE_ALLOW_WATERMARK_ADJUSTMENT,
	FEATURE_LIGHT_SLEEP,
	FEATURE_DCP_DITHER_FRAME_RANDOM_ENABLE,
	FEATURE_DCP_DITHER_RGB_RANDOM_ENABLE,
	FEATURE_DCP_DITHER_HIGH_PASS_RANDOM_ENABLE,
	FEATURE_DETECT_REQUIRE_HPD_HIGH,
	FEATURE_LINE_BUFFER_ENHANCED_PIXEL_DEPTH, /* 10th */
	FEATURE_MAXIMIZE_URGENCY_WATERMARKS,
	FEATURE_MAXIMIZE_STUTTER_MARKS,
	FEATURE_MAXIMIZE_NBP_MARKS,
	FEATURE_RESTORE_USAGE_I2C_SW_ENGINE,
	FEATURE_USE_MAX_DISPLAY_CLK,
	FEATURE_ALLOW_EDP_RESOURCE_SHARING,
	FEATURE_SUPPORT_DP_YUV,
	FEATURE_SUPPORT_DP_Y_ONLY,
	FEATURE_DISABLE_DP_GTC_SYNC,
	FEATURE_NO_HPD_LOW_POLLING_VCC_OFF, /* 20th */
	FEATURE_ENABLE_DFS_BYPASS,
	FEATURE_LB_HIGH_RESOLUTION,
	FEATURE_DP_DISPLAY_FORCE_SS_ENABLE,
	FEATURE_REPORT_CE_MODE_ONLY,
	FEATURE_ALLOW_OPTIMIZED_MODE_AS_DEFAULT,
	FEATURE_DDC_READ_FORCE_REPEATED_START,
	FEATURE_FORCE_TIMING_RESYNC,
	FEATURE_TMDS_DISABLE_DITHERING,
	FEATURE_HDMI_DISABLE_DITHERING,
	FEATURE_DP_DISABLE_DITHERING, /* 30th */
	FEATURE_EMBEDDED_DISABLE_DITHERING,
	FEATURE_DISABLE_AZ_CLOCK_GATING, /* 32th. This set is full */
	FEATURE_SET_01_END = FEATURE_SET_01_START + 31,

	/* Boolean set, up to 32 entries */
	FEATURE_WIRELESS_ENABLE = FEATURE_SET_01_END + 1,
	FEATURE_SET_02_START = FEATURE_WIRELESS_ENABLE,
	FEATURE_WIRELESS_FULL_TIMING_ADJUSTMENT,
	FEATURE_WIRELESS_LIMIT_720P,
	FEATURE_WIRELESS_ENABLE_COMPRESSED_AUDIO,
	FEATURE_WIRELESS_INCLUDE_UNVERIFIED_TIMINGS,
	FEATURE_MODIFY_TIMINGS_FOR_WIRELESS,
	FEATURE_ALLOW_SELF_REFRESH,
	FEATURE_ALLOW_DYNAMIC_PIXEL_ENCODING_CHANGE,
	FEATURE_ALLOW_HSYNC_VSYNC_ADJUSTMENT,
	FEATURE_FORCE_PSR, /* 10th */
	FEATURE_PREFER_3D_TIMING,
	FEATURE_VARI_BRIGHT_ENABLE,
	FEATURE_PSR_ENABLE,
	FEATURE_EDID_STRESS_READ,
	FEATURE_DP_FRAME_PACK_STEREO3D,
	FEATURE_ALLOW_HDMI_WITHOUT_AUDIO,
	FEATURE_RESTORE_USAGE_I2C_SW_ENGING,
	FEATURE_ABM_2_0,
	FEATURE_SUPPORT_MIRABILIS,
	FEATURE_LOAD_DMCU_FIRMWARE, /* 20th */
	FEATURE_ENABLE_GPU_SCALING,
	FEATURE_DONGLE_SINK_COUNT_CHECK,
	FEATURE_INSTANT_UP_SCALE_DOWN_SCALE,
	FEATURE_TILED_DISPLAY,
	FEATURE_CHANGE_I2C_SPEED_CONTROL,
	FEATURE_REPORT_SINGLE_SELECTED_TIMING,
	FEATURE_ALLOW_HDMI_HIGH_CLK_DP_DONGLE,
	FEATURE_SUPPORT_EXTERNAL_PANEL_DRR,
	FEATURE_SUPPORT_SMOOTH_BRIGHTNESS,
	FEATURE_ALLOW_DIRECT_MEMORY_ACCESS_TRIG, /* 30th */
	FEATURE_POWER_GATING_LB_PORTION, /* 31nd. One more left. */
	FEATURE_SET_02_END = FEATURE_SET_02_START + 31,

	/* UInt set, 1 entry: DCP Bit Depth Reduction Mode */
	FEATURE_DCP_BIT_DEPTH_REDUCTION_MODE = FEATURE_SET_02_END + 1,
	FEATURE_SET_03_START = FEATURE_DCP_BIT_DEPTH_REDUCTION_MODE,
	FEATURE_SET_03_END = FEATURE_SET_03_START + 31,

	/* UInt set, 1 entry: DCP Dither Mode */
	FEATURE_DCP_DITHER_MODE = FEATURE_SET_03_END + 1,
	FEATURE_SET_04_START = FEATURE_DCP_DITHER_MODE,
	FEATURE_SET_04_END = FEATURE_SET_04_START + 31,

	/* UInt set, 1 entry: DCP Programming WA(workaround) */
	FEATURE_DCP_PROGRAMMING_WA = FEATURE_SET_04_END + 1,
	FEATURE_SET_06_START = FEATURE_DCP_PROGRAMMING_WA,
	FEATURE_SET_06_END = FEATURE_SET_06_START + 31,

	/* UInt set, 1 entry: Maximum co-functional non-DP displays */
	FEATURE_MAX_COFUNC_NON_DP_DISPLAYS = FEATURE_SET_06_END + 1,
	FEATURE_SET_07_START = FEATURE_MAX_COFUNC_NON_DP_DISPLAYS,
	FEATURE_SET_07_END = FEATURE_SET_07_START + 31,

	/* UInt set, 1 entry: Number of supported HDMI connection */
	FEATURE_SUPPORTED_HDMI_CONNECTION_NUM = FEATURE_SET_07_END + 1,
	FEATURE_SET_08_START = FEATURE_SUPPORTED_HDMI_CONNECTION_NUM,
	FEATURE_SET_08_END = FEATURE_SET_08_START + 31,

	/* UInt set, 1 entry: Maximum number of controllers */
	FEATURE_MAX_CONTROLLER_NUM = FEATURE_SET_08_END + 1,
	FEATURE_SET_09_START = FEATURE_MAX_CONTROLLER_NUM,
	FEATURE_SET_09_END = FEATURE_SET_09_START + 31,

	/* UInt set, 1 entry: Type of DRR support */
	FEATURE_DRR_SUPPORT = FEATURE_SET_09_END + 1,
	FEATURE_SET_10_START = FEATURE_DRR_SUPPORT,
	FEATURE_SET_10_END = FEATURE_SET_10_START + 31,

	/* UInt set, 1 entry: Stutter mode support */
	FEATURE_STUTTER_MODE = FEATURE_SET_10_END + 1,
	FEATURE_SET_11_START = FEATURE_STUTTER_MODE,
	FEATURE_SET_11_END = FEATURE_SET_11_START + 31,

	/* UInt set, 1 entry: Measure PSR setup time */
	FEATURE_PSR_SETUP_TIME_TEST = FEATURE_SET_11_END + 1,
	FEATURE_SET_12_START = FEATURE_PSR_SETUP_TIME_TEST,
	FEATURE_SET_12_END = FEATURE_SET_12_START + 31,

	/* Boolean set, up to 32 entries */
	FEATURE_POWER_GATING_PIPE_IN_TILE = FEATURE_SET_12_END + 1,
	FEATURE_SET_13_START = FEATURE_POWER_GATING_PIPE_IN_TILE,
	FEATURE_USE_PPLIB,
	FEATURE_DISABLE_LPT_SUPPORT,
	FEATURE_DUMMY_FBC_BACKEND,
	FEATURE_DISABLE_FBC_COMP_CLK_GATE,
	FEATURE_DPMS_AUDIO_ENDPOINT_CONTROL,
	FEATURE_PIXEL_PERFECT_OUTPUT,
	FEATURE_8BPP_SUPPORTED,
	FEATURE_SET_13_END = FEATURE_SET_13_START + 31,

	/* UInt set, 1 entry: Display preferred view
	 * 0: no preferred view
	 * 1: native and preferred timing of embedded display will have high
	 *    priority, so other displays will support it always
	 */
	FEATURE_DISPLAY_PREFERRED_VIEW = FEATURE_SET_13_END + 1,
	FEATURE_SET_15_START = FEATURE_DISPLAY_PREFERRED_VIEW,
	FEATURE_SET_15_END = FEATURE_SET_15_START + 31,

	/* UInt set, 1 entry: DAL optimization */
	FEATURE_OPTIMIZATION = FEATURE_SET_15_END + 1,
	FEATURE_SET_16_START = FEATURE_OPTIMIZATION,
	FEATURE_SET_16_END = FEATURE_SET_16_START + 31,

	/* UInt set, 1 entry: Performance measurement */
	FEATURE_PERF_MEASURE = FEATURE_SET_16_END + 1,
	FEATURE_SET_17_START = FEATURE_PERF_MEASURE,
	FEATURE_SET_17_END = FEATURE_SET_17_START + 31,

	/* UInt set, 1 entry: Minimum backlight value [0-255] */
	FEATURE_MIN_BACKLIGHT_LEVEL = FEATURE_SET_17_END + 1,
	FEATURE_SET_18_START = FEATURE_MIN_BACKLIGHT_LEVEL,
	FEATURE_SET_18_END = FEATURE_SET_18_START + 31,

	/* UInt set, 1 entry: Maximum backlight value [0-255] */
	FEATURE_MAX_BACKLIGHT_LEVEL = FEATURE_SET_18_END + 1,
	FEATURE_SET_19_START = FEATURE_MAX_BACKLIGHT_LEVEL,
	FEATURE_SET_19_END = FEATURE_SET_19_START + 31,

	/* UInt set, 1 entry: AMB setting
	 *
	 * Each byte will control the ABM configuration to use for a specific
	 * ABM level.
	 *
	 * HW team provided 12 different ABM min/max reduction pairs to choose
	 * between for each ABM level.
	 *
	 * ABM level Byte Setting
	 *       1    0   Default = 0 (setting 3), can be override to 1-12
	 *       2    1   Default = 0 (setting 7), can be override to 1-12
	 *       3    2   Default = 0 (setting 8), can be override to 1-12
	 *       4    3   Default = 0 (setting 10), can be override to 1-12
	 *
	 * For example,
	 * FEATURE_PREFERRED_ABM_CONFIG_SET = 0x0C060500, this represents:
	 * ABM level 1 use default setting (setting 3)
	 * ABM level 2 uses setting 5
	 * ABM level 3 uses setting 6
	 * ABM level 4 uses setting 12
	 * Internal use only!
	 */
	FEATURE_PREFERRED_ABM_CONFIG_SET = FEATURE_SET_19_END + 1,
	FEATURE_SET_20_START = FEATURE_PREFERRED_ABM_CONFIG_SET,
	FEATURE_SET_20_END = FEATURE_SET_20_START + 31,

	/* UInt set, 1 entry: Change SW I2C speed */
	FEATURE_CHANGE_SW_I2C_SPEED = FEATURE_SET_20_END + 1,
	FEATURE_SET_21_START = FEATURE_CHANGE_SW_I2C_SPEED,
	FEATURE_SET_21_END = FEATURE_SET_21_START + 31,

	/* UInt set, 1 entry: Change HW I2C speed */
	FEATURE_CHANGE_HW_I2C_SPEED = FEATURE_SET_21_END + 1,
	FEATURE_SET_22_START = FEATURE_CHANGE_HW_I2C_SPEED,
	FEATURE_SET_22_END = FEATURE_SET_22_START + 31,

	/* UInt set, 1 entry:
	 * When PSR issue occurs, it is sometimes hard to debug since the
	 * failure occurs immediately at boot. Use this setting to skip or
	 * postpone PSR functionality and re-enable through DSAT. */
	FEATURE_DEFAULT_PSR_LEVEL = FEATURE_SET_22_END + 1,
	FEATURE_SET_23_START = FEATURE_DEFAULT_PSR_LEVEL,
	FEATURE_SET_23_END = FEATURE_SET_23_START + 31,

	/* UInt set, 1 entry: Allowed pixel clock range for LVDS */
	FEATURE_LVDS_SAFE_PIXEL_CLOCK_RANGE = FEATURE_SET_23_END + 1,
	FEATURE_SET_24_START = FEATURE_LVDS_SAFE_PIXEL_CLOCK_RANGE,
	FEATURE_SET_24_END = FEATURE_SET_24_START + 31,

	/* UInt set, 1 entry: Max number of clock sources */
	FEATURE_MAX_CLOCK_SOURCE_NUM = FEATURE_SET_24_END + 1,
	FEATURE_SET_25_START = FEATURE_MAX_CLOCK_SOURCE_NUM,
	FEATURE_SET_25_END = FEATURE_SET_25_START + 31,

	/* UInt set, 1 entry: Select the ABM configuration to use.
	 *
	 * This feature set is used to allow packaging option to be defined
	 * to allow OEM to select between the default ABM configuration or
	 * alternative predefined configurations that may be more aggressive.
	 *
	 * Note that this regkey is meant for external use to select the
	 * configuration OEM wants. Whereas the other PREFERRED_ABM_CONFIG_SET
	 * key is only used for internal use and allows full reconfiguration.
	 */
	FEATURE_ABM_CONFIG = FEATURE_SET_25_END + 1,
	FEATURE_SET_26_START = FEATURE_ABM_CONFIG,
	FEATURE_SET_26_END = FEATURE_SET_26_START + 31,

	/* UInt set, 1 entry: Select the default speed in which smooth
	 * brightness feature should converge towards target backlight level.
	 *
	 * For example, a setting of 500 means it takes 500ms to transition
	 * from current backlight level to the new requested backlight level.
	 */
	FEATURE_SMOOTH_BRTN_ADJ_TIME_IN_MS = FEATURE_SET_26_END + 1,
	FEATURE_SET_27_START = FEATURE_SMOOTH_BRTN_ADJ_TIME_IN_MS,
	FEATURE_SET_27_END = FEATURE_SET_27_START + 31,

	/* Set 28: UInt set, 1 entry: Allow runtime parameter to force specific
	 * Static Screen Event triggers for test purposes. */
	FEATURE_FORCE_STATIC_SCREEN_EVENT_TRIGGERS = FEATURE_SET_27_END + 1,
	FEATURE_SET_28_START = FEATURE_FORCE_STATIC_SCREEN_EVENT_TRIGGERS,
	FEATURE_SET_28_END = FEATURE_SET_28_START + 31,

	FEATURE_MAXIMUM
};

/* Adapter Service type of DRR support*/
enum as_drr_support {
	AS_DRR_SUPPORT_DISABLED = 0x0,
	AS_DRR_SUPPORT_ENABLED = 0x1,
	AS_DRR_SUPPORT_MIN_FORCED_FPS = 0xA
};

/* Adapter service initialize data structure*/
struct as_init_data {
	struct hw_asic_id hw_init_data;
	struct bp_init_data bp_init_data;
	struct dc_context *ctx;
	const struct dal_override_parameters *display_param;
	struct dc_bios *vbios_override;
	enum dce_environment dce_environment;
};

/* Create adapter service */
struct adapter_service *dal_adapter_service_create(
	struct as_init_data *init_data);

/* Destroy adapter service and objects it contains */
void dal_adapter_service_destroy(
	struct adapter_service **as);

/* Get the DCE version of current ASIC */
enum dce_version dal_adapter_service_get_dce_version(
	const struct adapter_service *as);

enum dce_environment dal_adapter_service_get_dce_environment(
	const struct adapter_service *as);

/* Get firmware information from BIOS */
bool dal_adapter_service_get_firmware_info(
	struct adapter_service *as,
	struct firmware_info *info);

/* functions to get a total number of objects of specific type */
uint8_t dal_adapter_service_get_connectors_num(
	struct adapter_service *as);

/* Get number of controllers */
uint8_t dal_adapter_service_get_controllers_num(
	struct adapter_service *as);

/* Get number of clock sources */
uint8_t dal_adapter_service_get_clock_sources_num(
	struct adapter_service *as);

/* Get number of controllers */
uint8_t dal_adapter_service_get_func_controllers_num(
	struct adapter_service *as);

/* Get number of stream engines */
uint8_t dal_adapter_service_get_stream_engines_num(
	struct adapter_service *as);

/* functions to get object id based on object index */
struct graphics_object_id dal_adapter_service_get_connector_obj_id(
	struct adapter_service *as,
	uint8_t connector_index);

/* Get number of spread spectrum entries from BIOS */
uint32_t dal_adapter_service_get_ss_info_num(
	struct adapter_service *as,
	enum as_signal_type signal);

/* Get spread spectrum info from BIOS */
bool dal_adapter_service_get_ss_info(
	struct adapter_service *as,
	enum as_signal_type signal,
	uint32_t idx,
	struct spread_spectrum_info *info);

/* Check if DFS bypass is enabled */
bool dal_adapter_service_is_dfs_bypass_enabled(struct adapter_service *as);

/* Get memory controller latency */
uint32_t dal_adapter_service_get_mc_latency(
	struct adapter_service *as);

/* Get the video RAM bit width set on the ASIC */
uint32_t dal_adapter_service_get_asic_vram_bit_width(
	struct adapter_service *as);

/* Get the bug flags set on this ASIC */
struct asic_bugs dal_adapter_service_get_asic_bugs(
	struct adapter_service *as);

/* Get efficiency of DRAM */
uint32_t dal_adapter_service_get_dram_bandwidth_efficiency(
	struct adapter_service *as);

/* Get multiplier for the memory type */
uint32_t dal_adapter_service_get_memory_type_multiplier(
	struct adapter_service *as);

/* Get parameters for bandwidth tuning */
bool dal_adapter_service_get_bandwidth_tuning_params(
	struct adapter_service *as,
	union bandwidth_tuning_params *params);

/* Get integrated information on BIOS */
bool dal_adapter_service_get_integrated_info(
	struct adapter_service *as,
	struct integrated_info *info);

/* Return if a given feature is supported by the ASIC */
bool dal_adapter_service_is_feature_supported(
	enum adapter_feature_id feature_id);

/* Get the cached value of a given feature */
bool dal_adapter_service_get_feature_value(
	const enum adapter_feature_id feature_id,
	void *data,
	uint32_t size);

/* Get a copy of ASIC feature flags */
struct asic_feature_flags dal_adapter_service_get_feature_flags(
	struct adapter_service *as);

/* Obtain DDC */
struct ddc *dal_adapter_service_obtain_ddc(
	struct adapter_service *as,
	struct graphics_object_id id);

/* Release DDC */
void dal_adapter_service_release_ddc(
	struct adapter_service *as,
	struct ddc *ddc);

/* Obtain HPD interrupt request */
struct irq *dal_adapter_service_obtain_hpd_irq(
	struct adapter_service *as,
	struct graphics_object_id id);

/* Release interrupt request */
void dal_adapter_service_release_irq(
	struct adapter_service *as,
	struct irq *irq);

/* Obtain GPIO */
struct gpio *dal_adapter_service_obtain_gpio(
	struct adapter_service *as,
	enum gpio_id id,
	uint32_t en);

/* Obtain GPIO for stereo3D*/
struct gpio *dal_adapter_service_obtain_stereo_gpio(struct adapter_service *as);

/* Release GPIO */
void dal_adapter_service_release_gpio(
		struct adapter_service *as,
		struct gpio *gpio);

/* Get SW I2C speed */
uint32_t dal_adapter_service_get_sw_i2c_speed(struct adapter_service *as);

/* Get HW I2C speed */
uint32_t dal_adapter_service_get_hw_i2c_speed(struct adapter_service *as);

/* Get line buffer size */
uint32_t dal_adapter_service_get_line_buffer_size(struct adapter_service *as);

/* Get information on audio support */
union audio_support dal_adapter_service_get_audio_support(
		struct adapter_service *as);

/* Get I2C information from BIOS */
bool dal_adapter_service_get_i2c_info(
	struct adapter_service *as,
	struct graphics_object_id id,
	struct graphics_object_i2c_info *i2c_info);

/* Get bios parser handler */
struct dc_bios *dal_adapter_service_get_bios_parser(
	struct adapter_service *as);

/* Get i2c aux handler */
struct i2caux *dal_adapter_service_get_i2caux(
	struct adapter_service *as);

struct dal_asic_runtime_flags dal_adapter_service_get_asic_runtime_flags(
	struct adapter_service *as);

bool dal_adapter_service_initialize_hw_data(
	struct adapter_service *as);

struct graphics_object_id dal_adapter_service_enum_fake_path_resource(
	struct adapter_service *as,
	uint32_t index);

struct graphics_object_id dal_adapter_service_enum_stereo_sync_object(
	struct adapter_service *as,
	uint32_t index);

struct graphics_object_id dal_adapter_service_enum_sync_output_object(
	struct adapter_service *as,
	uint32_t index);

struct graphics_object_id dal_adapter_service_enum_audio_object(
	struct adapter_service *as,
	uint32_t index);

void dal_adapter_service_update_audio_connectivity(
	struct adapter_service *as,
	uint32_t number_of_audio_capable_display_path);

bool dal_adapter_service_has_embedded_display_connector(
	struct adapter_service *as);

bool dal_adapter_service_get_embedded_panel_info(
	struct adapter_service *as,
	struct embedded_panel_info *info);

bool dal_adapter_service_enum_embedded_panel_patch_mode(
	struct adapter_service *as,
	uint32_t index,
	struct embedded_panel_patch_mode *mode);

bool dal_adapter_service_get_faked_edid_len(
	struct adapter_service *as,
	uint32_t *len);

bool dal_adapter_service_get_faked_edid_buf(
	struct adapter_service *as,
	uint8_t *buf,
	uint32_t len);

uint32_t dal_adapter_service_get_max_cofunc_non_dp_displays(void);

uint32_t dal_adapter_service_get_single_selected_timing_signals(void);

bool dal_adapter_service_get_device_tag(
	struct adapter_service *as,
	struct graphics_object_id connector_object_id,
	uint32_t device_tag_index,
	struct connector_device_tag_info *info);

bool dal_adapter_service_is_device_id_supported(
	struct adapter_service *as,
	struct device_id id);

bool dal_adapter_service_is_meet_underscan_req(struct adapter_service *as);

bool dal_adapter_service_underscan_for_hdmi_only(struct adapter_service *as);

uint32_t dal_adapter_service_get_src_num(
	struct adapter_service *as,
	struct graphics_object_id id);

struct graphics_object_id dal_adapter_service_get_src_obj(
	struct adapter_service *as,
	struct graphics_object_id id,
	uint32_t index);

/* Is this Fusion ASIC */
bool dal_adapter_service_is_fusion(struct adapter_service *as);

/* Is this ASIC support dynamic DFSbypass switch */
bool dal_adapter_service_is_dfsbyass_dynamic(struct adapter_service *as);

/* Reports whether driver settings allow requested optimization */
bool dal_adapter_service_should_optimize(
		struct adapter_service *as, enum optimization_feature feature);

/* Determine if driver is in accelerated mode */
bool dal_adapter_service_is_in_accelerated_mode(struct adapter_service *as);

struct ddc *dal_adapter_service_obtain_ddc_from_i2c_info(
	struct adapter_service *as,
	struct graphics_object_i2c_info *info);

/* Determine if this ASIC needs to wait on PLL lock bit */
bool dal_adapter_service_should_psr_skip_wait_for_pll_lock(
	struct adapter_service *as);

#define SIZEOF_BACKLIGHT_LUT 101
#define ABSOLUTE_BACKLIGHT_MAX 255
#define DEFAULT_MIN_BACKLIGHT 12
#define DEFAULT_MAX_BACKLIGHT 255
#define BACKLIGHT_CURVE_COEFFB 100
#define BACKLIGHT_CURVE_COEFFA_FACTOR 10000
#define BACKLIGHT_CURVE_COEFFB_FACTOR 100

struct panel_backlight_levels {
	uint32_t ac_level_percentage;
	uint32_t dc_level_percentage;
};

bool dal_adapter_service_is_lid_open(struct adapter_service *as);

bool dal_adapter_service_get_panel_backlight_default_levels(
	struct adapter_service *as,
	struct panel_backlight_levels *levels);

bool dal_adapter_service_get_panel_backlight_boundaries(
	struct adapter_service *as,
	struct panel_backlight_boundaries *boundaries);

uint32_t dal_adapter_service_get_view_port_pixel_granularity(
	struct adapter_service *as);

uint32_t dal_adapter_service_get_num_of_path_per_dp_mst_connector(
		struct adapter_service *as);

uint32_t dal_adapter_service_get_num_of_underlays(
		struct adapter_service *as);

bool dal_adapter_service_get_encoder_cap_info(
		struct adapter_service *as,
		struct graphics_object_id id,
		struct graphics_object_encoder_cap_info *info);

bool dal_adapter_service_is_mc_tuning_req(struct adapter_service *as);

#endif /* __DAL_ADAPTER_SERVICE_INTERFACE_H__ */
