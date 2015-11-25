#ifndef __DAL_HW_ADJUSTMENT_TYPES_H__
#define __DAL_HW_ADJUSTMENT_TYPES_H__

#include "hw_sequencer_types.h"

enum hw_adjustment_id {
	HW_ADJUSTMENT_ID_COLOR_CONTROL,
	HW_ADJUSTMENT_ID_GAMMA_LUT,
	HW_ADJUSTMENT_ID_GAMMA_RAMP,
	HW_ADJUSTMENT_ID_DEFLICKER,
	HW_ADJUSTMENT_ID_SHARPNESS_CONTROL,
	HW_ADJUSTMENT_ID_TIMING,
	HW_ADJUSTMENT_ID_TIMING_AND_PIXEL_CLOCK,
	HW_ADJUSTMENT_ID_OVERSCAN,
	HW_ADJUSTMENT_ID_UNDERSCAN_TYPE,
	HW_ADJUSTMENT_ID_VERTICAL_SYNC,
	HW_ADJUSTMENT_ID_HORIZONTAL_SYNC,
	HW_ADJUSTMENT_ID_COMPOSITE_SYNC,
	HW_ADJUSTMENT_ID_VIDEO_STANDARD,
	HW_ADJUSTMENT_ID_BACKLIGHT,
	HW_ADJUSTMENT_ID_BIT_DEPTH_REDUCTION,
	HW_ADJUSTMENT_ID_REDUCED_BLANKING,
	HW_ADJUSTMENT_ID_COHERENT,
	/* OVERLAY ADJUSTMENTS*/
	HW_ADJUSTMENT_ID_OVERLAY,
	HW_ADJUSTMENT_ID_OVERLAY_ALPHA,
	HW_ADJUSTMENT_ID_OVERLAY_VARIABLE_GAMMA,
	HW_ADJUSTMENT_ID_COUNT,
	HW_ADJUSTMENT_ID_UNDEFINED,
};

struct hw_adjustment_deflicker {
	int32_t hp_factor;
	uint32_t hp_divider;
	int32_t lp_factor;
	uint32_t lp_divider;
	int32_t sharpness;
	bool enable_sharpening;
};

struct hw_adjustment_value {
	union {
		uint32_t ui_value;
		int32_t i_value;
	};
};

enum hw_color_adjust_option {
	HWS_COLOR_MATRIX_HW_DEFAULT = 1,
	HWS_COLOR_MATRIX_SW
};

enum {
	HW_TEMPERATURE_MATRIX_SIZE = 9,
	HW_TEMPERATURE_MATRIX_SIZE_WITH_OFFSET = 12
};

struct hw_adjustment_color_control {
	enum hw_color_space color_space;
	enum hw_color_adjust_option option;
	enum pixel_format surface_pixel_format;
	enum dc_color_depth color_depth;
	uint32_t lb_color_depth;
	int32_t contrast;
	int32_t saturation;
	int32_t brightness;
	int32_t hue;
	uint32_t adjust_divider;
	uint32_t temperature_divider;
	uint32_t temperature_matrix[HW_TEMPERATURE_MATRIX_SIZE];
};

struct hw_underscan_adjustment {
	struct hw_adjustment_deflicker deflicker;
	struct overscan_info hw_overscan;
};

struct hw_underscan_adjustment_data {
	enum hw_adjustment_id hw_adj_id;
	struct hw_underscan_adjustment hw_underscan_adj;
};

union hw_adjustment_bit_depth_reduction {
	uint32_t raw;
	struct {
		uint32_t TRUNCATE_ENABLED:1;
		uint32_t TRUNCATE_DEPTH:2;
		uint32_t TRUNCATE_MODE:1;
		uint32_t SPATIAL_DITHER_ENABLED:1;
		uint32_t SPATIAL_DITHER_DEPTH:2;
		uint32_t SPATIAL_DITHER_MODE:2;
		uint32_t RGB_RANDOM:1;
		uint32_t FRAME_RANDOM:1;
		uint32_t HIGHPASS_RANDOM:1;
		uint32_t FRAME_MODULATION_ENABLED:1;
		uint32_t FRAME_MODULATION_DEPTH:2;
		uint32_t TEMPORAL_LEVEL:1;
		uint32_t FRC_25:2;
		uint32_t FRC_50:2;
		uint32_t FRC_75:2;
	} bits;
};

struct hw_color_control_range {
	struct hw_adjustment_range contrast;
	struct hw_adjustment_range saturation;
	struct hw_adjustment_range brightness;
	struct hw_adjustment_range hue;
	struct hw_adjustment_range temperature;
};

enum hw_surface_type {
	HW_OVERLAY_SURFACE = 1,
	HW_GRAPHIC_SURFACE
};

/* LUT type for GammaCorrection */
struct hw_gamma_lut {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
};

struct hw_devc_lut {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t reserved;
};

struct hw_adjustment_gamma_lut {
	struct hw_gamma_lut *pGammaLut;
	uint32_t size_in_elements;
	enum pixel_format surface_pixel_format;
};


enum hw_gamma_ramp_type {
	HW_GAMMA_RAMP_UNITIALIZED = 0,
	HW_GAMMA_RAMP_DEFAULT,
	HW_GAMMA_RAMP_RBG_256x3x16,
	HW_GAMMA_RAMP_RBG_DXGI_1
};

#define HW_GAMMA_RAMP_RBG_256 256

struct hw_gamma_ramp_rgb256x3x16 {
	unsigned short red[HW_GAMMA_RAMP_RBG_256];
	unsigned short green[HW_GAMMA_RAMP_RBG_256];
	unsigned short blue[HW_GAMMA_RAMP_RBG_256];
};

union hw_gamma_flags {
	uint32_t raw;
	struct {
		uint32_t gamma_ramp_array :1;
		uint32_t graphics_degamma_srgb :1;
		uint32_t overlay_degamma_srgb :1;
		uint32_t apply_degamma :1;
		uint32_t reserved :28;
	} bits;
};

struct hw_regamma_coefficients {
	int32_t gamma[3];
	int32_t a0[3];
	int32_t a1[3];
	int32_t a2[3];
	int32_t a3[3];
};

struct hw_regamma_ramp {
	/* Gamma ramp packed as RGB */
	unsigned short gamma[256 * 3];
};

struct hw_regamma_lut {
	union hw_gamma_flags flags;
	union {
		struct hw_regamma_ramp gamma;
		struct hw_regamma_coefficients coeff;
	};
};

union hw_gamma_flag {
	uint32_t uint;
	struct {
		uint32_t config_is_changed :1;
		uint32_t regamma_update :1;
		uint32_t gamma_update :1;
		uint32_t reserved :29;
	} bits;
};

struct hw_adjustment_gamma_ramp {
	uint32_t size;
	enum hw_gamma_ramp_type type;
	enum pixel_format surface_pixel_format;
	enum hw_color_space color_space;
	struct hw_regamma_lut regamma;
	union hw_gamma_flag flag;
	struct hw_gamma_ramp_rgb256x3x16 gamma_ramp_rgb256x3x16;
};

#endif
