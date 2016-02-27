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
struct dc_bios;

enum dce_version {
	DCE_VERSION_UNKNOWN = (-1),
#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
	DCE_VERSION_8_0,
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	DCE_VERSION_10_0,
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	DCE_VERSION_11_0,
#endif
	DCE_VERSION_MAX
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
