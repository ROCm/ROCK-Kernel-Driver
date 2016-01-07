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

#ifndef __DAL_TYPES_H__
#define __DAL_TYPES_H__

#include "signal_types.h"
#include "dc_types.h"

struct dal_logger;

enum dce_version {
	DCE_VERSION_UNKNOWN = (-1),
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	DCE_VERSION_11_0,
#endif
	DCE_VERSION_MAX
};

/*
 * ASIC Runtime Flags
 */
struct dal_asic_runtime_flags {
	union {
		uint32_t raw;
		struct {
			uint32_t EMULATE_REPLUG_ON_CAP_CHANGE:1;
			uint32_t SUPPORT_XRBIAS:1;
			uint32_t SKIP_POWER_DOWN_ON_RESUME:1;
			uint32_t FULL_DETECT_ON_RESUME:1;
			uint32_t GSL_FRAMELOCK:1;
			uint32_t NO_LOW_BPP_MODES:1;
			uint32_t BLOCK_ON_INITIAL_DETECTION:1;
			uint32_t OPTIMIZED_DISPLAY_PROGRAMMING_ON_BOOT:1;
			uint32_t DRIVER_CONTROLLED_BRIGHTNESS:1;
			uint32_t MODIFIABLE_FRAME_DURATION:1;
			uint32_t MIRACAST_SUPPORTED:1;
			uint32_t CONNECTED_STANDBY_SUPPORTED:1;
			uint32_t GNB_WAKEUP_SUPPORTED:1;
		} bits;
	} flags;
};

struct hw_asic_id {
	uint32_t chip_id;
	uint32_t chip_family;
	uint32_t pci_revision_id;
	uint32_t hw_internal_rev;
	uint32_t vram_type;
	uint32_t vram_width;
	uint32_t feature_flags;
	struct dal_asic_runtime_flags runtime_flags;
	uint32_t fake_paths_num;
	void *atombios_base_address;
};

/* this is pci information. BDF stands for BUS,DEVICE,FUNCTION*/

struct bdf_info {
	uint16_t BUS_NUMBER:8;
	uint16_t DEVICE_NUMBER:5;
	uint16_t FUNCTION_NUMBER:3;
};

#define DAL_PARAM_INVALID_INT 0x80000000

/* shift values for bool override parameter mask
 * bmask is for this struct,if we touch this feature
 * bval indicates every bit fields for this struct too,1 is enable this feature
 * amdgpu.disp_bval=1594, amdgpu.disp_bmask=1594 ,
 * finally will show log like this:
 * Overridden FEATURE_LIGHT_SLEEP is enabled now
 * Overridden FEATURE_USE_MAX_DISPLAY_CLK is enabled now
 * Overridden FEATURE_ENABLE_DFS_BYPASS is enabled now
 * Overridden FEATURE_POWER_GATING_PIPE_IN_TILE is enabled now
 * Overridden FEATURE_USE_PPLIB is enabled now
 * Overridden FEATURE_DISABLE_LPT_SUPPORT is enabled now
 * Overridden FEATURE_DUMMY_FBC_BACKEND is enabled now */
enum bool_param_shift {
	DAL_PARAM_MAXIMIZE_STUTTER_MARKS = 0,
	DAL_PARAM_LIGHT_SLEEP,
	DAL_PARAM_MAXIMIZE_URGENCY_WATERMARKS,
	DAL_PARAM_USE_MAX_DISPLAY_CLK,
	DAL_PARAM_ENABLE_DFS_BYPASS,
	DAL_PARAM_POWER_GATING_PIPE_IN_TILE,
	DAL_PARAM_POWER_GATING_LB_PORTION,
	DAL_PARAM_PSR_ENABLE,
	DAL_PARAM_VARI_BRIGHT_ENABLE,
	DAL_PARAM_USE_PPLIB,
	DAL_PARAM_DISABLE_LPT_SUPPORT,
	DAL_PARAM_DUMMY_FBC_BACKEND,
	DAL_PARAM_ENABLE_GPU_SCALING,
	DAL_BOOL_PARAM_MAX
};

/* array index for integer override parameters*/
enum int_param_array_index {
	DAL_PARAM_MAX_COFUNC_NON_DP_DISPLAYS = 0,
	DAL_PARAM_DRR_SUPPORT,
	DAL_INT_PARAM_MAX
};

struct dal_override_parameters {
	uint32_t bool_param_enable_mask;
	uint32_t bool_param_values;
	uint32_t int_param_values[DAL_INT_PARAM_MAX];
};


struct dal_init_data {
	struct hw_asic_id asic_id;
	struct view_port_alignment vp_alignment;
	struct bdf_info bdf_info;
	struct dal_override_parameters display_param;
	void *driver; /* ctx */
	void *cgs_device;
	uint8_t num_virtual_links;
};

struct dal_dc_init_data {
	struct dc_context *ctx; /* TODO: remove 'dal' when DC is complete. */
	struct adapter_service *adapter_srv;
};

struct dal_dev_c_lut {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t reserved;
};

struct dal_dev_gamma_lut {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
};

struct dc_context {
	struct dc *dc;

	void *driver_context; /* e.g. amdgpu_device */

	struct dal_logger *logger;
	void *cgs_device;
};

/* Wireless display structs */

union dal_remote_display_cea_mode_bitmap {
	struct {
		uint32_t CEA_640X480_60P:1;/*0*/
		uint32_t CEA_720X480_60P:1;/*1*/
		uint32_t CEA_720X480_60I:1;/*2*/
		uint32_t CEA_720X576_50P:1;/*3*/
		uint32_t CEA_720X576_50I:1;/*4*/
		uint32_t CEA_1280X720_30P:1;/*5*/
		uint32_t CEA_1280X720_60P:1;/*6*/
		uint32_t CEA_1920X1080_30P:1;/*7*/
		uint32_t CEA_1920X1080_60P:1;/*8*/
		uint32_t CEA_1920X1080_60I:1;/*9*/
		uint32_t CEA_1280X720_25P:1;/*10*/
		uint32_t CEA_1280X728_50P:1;/*11*/
		uint32_t CEA_1920X1080_25P:1;/*12*/
		uint32_t CEA_1920X1080_50P:1;/*13*/
		uint32_t CEA_1920X1080_50I:1;/*14*/
		uint32_t CEA_1280X1024_24P:1;/*15*/
		uint32_t CEA_1920X1080_24P:1;/*16*/
		uint32_t RESERVED:15;/*[17-31]*/
	} flags;
	uint32_t raw;
};

union dal_remote_display_vesa_mode_bitmap {
	struct {
		uint32_t VESA_800X600_30P:1;/*0*/
		uint32_t VESA_800X600_60P:1;/*1*/
		uint32_t VESA_1024X768_30P:1;/*2*/
		uint32_t VESA_1024X768_60P:1;/*3*/
		uint32_t VESA_1152X864_30P:1;/*4*/
		uint32_t VESA_1152X864_60P:1;/*5*/
		uint32_t VESA_1280X768_30P:1;/*6*/
		uint32_t VESA_1280X768_60P:1;/*7*/
		uint32_t VESA_1280X800_30P:1;/*8*/
		uint32_t VESA_1280X800_60P:1;/*9*/
		uint32_t VESA_1360X768_30P:1;/*10*/
		uint32_t VESA_1360X768_60P:1;/*11*/
		uint32_t VESA_1366X768_30P:1;/*12*/
		uint32_t VESA_1366X768_60P:1;/*13*/
		uint32_t VESA_1280X1024_30P:1;/*14*/
		uint32_t VESA_1280X1024_60P:1;/*15*/
		uint32_t VESA_1400X1050_30P:1;/*16*/
		uint32_t VESA_1400X1050_60P:1;/*17*/
		uint32_t VESA_1440X900_30P:1;/*18*/
		uint32_t VESA_1440X900_60P:1;/*19*/
		uint32_t VESA_1600X900_30P:1;/*20*/
		uint32_t VESA_1600X900_60P:1;/*21*/
		uint32_t VESA_1600X1200_30P:1;/*22*/
		uint32_t VESA_1600X1200_60P:1;/*23*/
		uint32_t VESA_1680X1024_30P:1;/*24*/
		uint32_t VESA_1680X1024_60P:1;/*25*/
		uint32_t VESA_1680X1050_30P:1;/*26*/
		uint32_t VESA_1680X1050_60P:1;/*27*/
		uint32_t VESA_1920X1200_30P:1;/*28*/
		uint32_t VESA_1920X1200_60P:1;/*29*/
		uint32_t RESERVED:2;/*[30-31]*/
	} flags;
	uint32_t raw;
};

union dal_remote_display_hh_mode_bitmap {
	struct {
		uint32_t HH_800X480_30P:1;/*0*/
		uint32_t HH_800X480_60P:1;/*1*/
		uint32_t HH_854X480_30P:1;/*2*/
		uint32_t HH_854X480_60P:1;/*3*/
		uint32_t HH_864X480_30P:1;/*4*/
		uint32_t HH_864X480_60P:1;/*5*/
		uint32_t HH_640X360_30P:1;/*6*/
		uint32_t HH_640X360_60P:1;/*7*/
		uint32_t HH_960X540_30P:1;/*8*/
		uint32_t HH_960X540_60P:1;/*9*/
		uint32_t HH_848X480_30P:1;/*10*/
		uint32_t HH_848X480_60P:1;/*11*/
		uint32_t RESERVED:20;/*[12-31]*/
	} flags;
	uint32_t raw;
};

union dal_remote_display_stereo_3d_mode_bitmap {
	struct {
		uint32_t STEREO_1920X1080_24P_TOP_AND_BOTTOM:1;/*0*/
		uint32_t STEREO_1280X720_60P_TOP_AND_BOTTOM:1;/*1*/
		uint32_t STEREO_1280X720_50P_TOP_AND_BOTTOM:1;/*2*/
		uint32_t STEREO_1920X1080_24X2P_FRAME_ALTERNATE:1;/*3*/
		uint32_t STEREO_1280X720_60X2P_FRAME_ALTERNATE:1;/*4*/
		uint32_t STEREO_1280X720_30X2P_FRAME_ALTERNATE:1;/*5*/
		uint32_t STEREO_1280X720_50X2P_FRAME_ALTERNATE:1;/*6*/
		uint32_t STEREO_1280X720_25X2P_FRAME_ALTERNATE:1;/*7*/
		uint32_t STEREO_1920X1080_24P_FRAME_PACKING:1;/* 8*/
		uint32_t STEREO_1280X720_60P_FRAME_PACKING:1;/* 9*/
		uint32_t STEREO_1280X720_30P_FRAME_PACKING:1;/*10*/
		uint32_t STEREO_1280X720_50P_FRAME_PACKING:1;/*11*/
		uint32_t STEREO_1280X720_25P_FRAME_PACKING:1;/*12*/
		uint32_t RESERVED:19; /*[13-31]*/
	} flags;
	uint32_t raw;
};

union dal_remote_display_audio_bitmap {
	struct {
		uint32_t LPCM_44100HZ_16BITS_2_CHANNELS:1;/*0*/
		uint32_t LPCM_48000HZ_16BITS_2_CHANNELS:1;/*1*/
		uint32_t AAC_48000HZ_16BITS_2_CHANNELS:1;/*2*/
		uint32_t AAC_48000HZ_16BITS_4_CHANNELS:1;/*3*/
		uint32_t AAC_48000HZ_16BITS_6_CHANNELS:1;/*4*/
		uint32_t AAC_48000HZ_16BITS_8_CHANNELS:1;/*5*/
		uint32_t AC3_48000HZ_16BITS_2_CHANNELS:1;/*6*/
		uint32_t AC3_48000HZ_16BITS_4_CHANNELS:1;/*7*/
		uint32_t AC3_48000HZ_16BITS_6_CHANNELS:1;/*8*/
		uint32_t RESERVED:23;/*[9-31]*/
	} flags;
	uint32_t raw;
};

struct dal_remote_display_receiver_capability {
	union dal_remote_display_cea_mode_bitmap cea_mode;
	union dal_remote_display_vesa_mode_bitmap vesa_mode;
	union dal_remote_display_hh_mode_bitmap hh_mode;
	union dal_remote_display_stereo_3d_mode_bitmap stereo_3d_mode;
	union dal_remote_display_audio_bitmap audio;
};

#endif /* __DAL_TYPES_H__ */
