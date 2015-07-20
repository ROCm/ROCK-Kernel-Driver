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
#include "include/dcs_interface.h"
#include "include/timing_service_interface.h"
#include "edid_base.h"
#include "edid_ext_cea.h"
#include "../edid_patch.h"

#define MAX_NUM_OF_DETAILED_TIMING_DESC_IN_CEA_861 6
#define CEA861_MAX_DATA_BLOKS_SIZE 123

#define CEA861_VERSION_1 0x01
#define CEA861_VERSION_A 0x02
#define CEA861_VERSION_B 0x03

/**
 * cea861 feature (CEA861B or later)
 */
#define CEA_861B_EXT_BYTE3_YCRCB422_SUPPORTED 0x10
#define CEA_861B_EXT_BYTE3_YCRCB444_SUPPORTED 0x20
#define CEA_861B_EXT_BYTE3_BASICAUDIO_SUPPORTED 0x40
#define CEA_861B_EXT_BYTE3_UNDERSCAN_SUPPORTED 0x80
#define CEA_861B_EXT_BYTE3_NATIVE_COUNT_MASK 0x0F

/**
 * CEA 861B Data block collection General Tag Format
 */
#define CEA_861B_GENERAL_TAG_FORMAT_TAG_CODE_MASK 0xE0
#define CEA_861B_GENERAL_TAG_FORMAT_TAG_CODE_SHIFT 0x5
#define CEA_861B_GENERAL_TAG_FORMAT_DATA_BLOCK_COUNT_MASK 0x1F
/**
 * CEA 861B Data Block Tag Codes
 */
#define CEA_861B_DATA_BLOCK_TAGCODE_SHORT_AUDIO_DESCRIPTOR 0x1
#define CEA_861B_DATA_BLOCK_TAGCODE_SHORT_VIDEO_DESCRIPTOR 0x2
#define CEA_861B_DATA_BLOCK_TAGCODE_VENDOR_SPECIFIC_DATA_BLOCK 0x3
#define CEA_861B_DATA_BLOCK_TAGCODE_SPKR_ALLOCATION_DATA_BLOCK 0x4
#define CEA_861B_DATA_BLOCK_TAGCODE_VESA_DTC_DATA_BLOCK 0x5
#define CEA_861B_DATA_BLOCK_TAGCODE_RESERVED 0x6
/*redirect to extended tag block*/
#define CEA_861B_DATA_BLOCK_TAGCODE_USE_EXTENDED_TAG 0x7

/**
 * CEA 861B short video descriptor related
 */
#define CEA_SHORT_VIDEO_DESCRIPTION_NATIVE_MASK 0x80
#define CEA_SHORT_VIDEO_DESCRIPTION_VIDEO_ID_CODE_MASK 0x7F

/**
 * CEA 861B Extended Data Block Tag Codes
 */
/*when CEA_861B_DATA_BLOCK_TAGCODE_USE_EXTEDNED_TAG not set.*/
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE 0x0
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VIDEO_CAPABILITY_DATA_BLOCK 0x0
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VENDOR_SPECIFIC_VIDEO_DATA_BLOCK 0x1
/*reserved for now*/
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VESA_VIDEO_DISPLAY_DEVICE_INFO_BLOCK 0x2
/* reserved for now */
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VESA_VIDEO_DATA_BLOCK 0x3
/* reserved for now */
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VESA_VIDEO_DATA_BLOCK1 0x4
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_COLORIMETRY_DATA_BLOCK 0x5

#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_CEA_MISC_AUDIO_FIELDS 0x16
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_VENDOR_SPECIFIC_AUDIO_DATA_BLOCK 0x17
/* reserved for now */
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_HDMI_AUDIO_DATA_BLOCK 0x18
#define CEA_861B_DATA_BLOCK_EXT_TAGCODE_MASK 0xFF

/**
 * cea861 extension structure
 */
struct edid_data_cea861_ext {
	uint8_t extension_tag;
	uint8_t revision;
	uint8_t timing_data_start;
	uint8_t cea861b_byte3;
	uint8_t data_block[CEA861_MAX_DATA_BLOKS_SIZE];
	uint8_t checksum;
};

#define DAL_PROGRESSIVE 0x0
#define DAL_INTERLACED 0x1

#define ASPECTRATIO_16_9 0x1
#define ASPECTRATIO_4_3 0x2
/* nunmber of entries in CEA-861-E spec */
#define MAX_NUM_SHORT_VIDEO_DESCRIPTOR_FORMAT 63

/**
 * CEA_AUDIO_MODE ucFormatCode defines
 */
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_RESERVED0 0x0
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_LINEAR_PCM 0x1
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_AC_3 0x2
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MPEG1 0x3
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MP3 0x4
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MPEG2 0x5
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_AAC 0x6
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_DTS 0x7
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_ATRAC 0x8
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_ONE_BIT_AUDIO 0x9
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_DOLBY_DIGITAL_PLUS 0xA
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_DTS_HD 0xB
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MAT_MLP 0xC
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_DST 0xD
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_WMA_PRO 0xE
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_RESERVEDF 0xF

#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MASK 0x78
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_SHIFT 0x3

#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MAX_CHANNEL_MINUS_ONE_MASK 0x7
#define CEA_861B_SHORT_AUDIO_FORMAT_CODE_MAX_CHANNEL_MINUS_ONE_SHIFT 0x0
#define CEA_861B_SHORT_AUDIO_SAMPLE_RATE_MASK 0x7F

/**
 * CEA_AUDIO_MODE bv8SampleRate defines
 */
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_32 0x01
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_44_1 0x02
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_48 0x04
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_88_2 0x08
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_96 0x10
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_176_4 0x20
#define CEA_861B_SHORT_AUDIO_SUPPORTED_FREQUENCY_192 0x40

/**
 * CEA_SPEAKER_ALLOCATION ucData[0] defines
 */
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_FL_FR 0x01
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_LFE 0x02
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_FC 0x04
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_RL_RR 0x08
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_RC 0x10
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_FLC_FRC 0x20
#define CEA_SPEAKER_ALLOCATION_SPEAKER_PRESENT_RLC_RRC 0x40

#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_NONE 0x00
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_XVYCC_601 0x01
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_XVYCC_709 0x02
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_XVYCC_MASK 0x03
#define CEA_861E_COLORIMETRY_SUPPORT_FLAG_SYCC_601 0x04
#define CEA_861E_COLORIMETRY_SUPPORT_FLAG_ADOBE_YCC601 0x08
#define CEA_861E_COLORIMETRY_SUPPORT_FLAG_ADOBE_RGB 0x10

#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_NONE 0x0
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_M0 0x1
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_M1 0x2
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_M2 0x4
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_M3 0x8
#define CEA_861C_COLORIMETRY_SUPPORT_FLAG_GAMUT_MASK 0xf

/**
 * CEA VIDEO CAPABILITY DATA BLCOK (VCDB)
 */
/* CE VIDEO format */
#define CEA_861C_DATA_BLOCK__VCDB__S_CE0 0x01
#define CEA_861C_DATA_BLOCK__VCDB__S_CE1 0x02
#define CEA_861C_DATA_BLOCK__VCDB__S_CE_MASK 0x03

/* IT video format */
#define CEA_861C_DATA_BLOCK__VCDB__S_IT0 0x04
#define CEA_861C_DATA_BLOCK__VCDB__S_IT1 0x08
#define CEA_861C_DATA_BLOCK__VCDB__S_IT_MASK 0x0C
#define CEA_861C_DATA_BLOCK__VCDB__S_IT_SHIFT 0x02

/* Prefered video formam */
#define CEA_861C_DATA_BLOCK__VCDB__S_PT0 0x10
#define CEA_861C_DATA_BLOCK__VCDB__S_PT1 0x20
#define CEA_861C_DATA_BLOCK__VCDB__S_PT_MASK 0x30
#define CEA_861C_DATA_BLOCK__VCDB__S_PT_SHIFT 0x04

/* Quantization */
#define CEA_861C_DATA_BLOCK__VCDB__QS 0x40
#define CEA_861C_DATA_BLOCK__EXTENDED_TAGCODE_MASK 0xFF

/**
 * HDMI Vendor specific data block
 */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_IEEE_REGISTRATION_ID 0x000C03

/* Byte 6 */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_AI 0x80
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_DC_48BIT 0x40
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_DC_36BIT 0x20
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_DC_30BIT 0x10
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_DC_Y444 0x08
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_SUPPORTED_DUALDVI 0x01

/* Byte 8 */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_LATENCY_PRESENT 0x80
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_I_LATENCY_PRESENT 0x40
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_HDMI_VIDEO_PRESENT 0x20
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_CNC3_GAME 0x08
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_CNC2_CINEMA 0x04
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_CNC1_PHOTO 0x02
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_CNC0_GRAPHICS 0x01

/* Byte 9~13 */
/* exact location of this byte depends in preceeding optional fields
like latency */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_PRESENT 0x80
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_PRESENT_SHIFT 0x7
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_MULTI_PRESENT_MASK 0x60 /*2 bits*/
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_MULTI_PRESENT_SHIFT 0x5
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_IMAGE_SIZE_MASK 0x18 /* 2 bits*/
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_IMAGE_SIZE_SHIFT 0x3

/* Byte that in the VSDB follows "flags byte" which defined right above
in this source file*/
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_VIC_LEN_MASK 0xE0 /* 3 bits */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_VIC_LEN_SHIFT 0x5
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_LEN_MASK 0x1F /* 5 bits */

/* Byte 4 and 5 */
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_COMPONENT_PHY_ADDR_MASK_HIGH 0xF0
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_COMPONENT_PHY_ADDR_MASK_LOW 0x0F
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_COMPONENT_PHY_ADDR_SHIFT 0x4

#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_STRUCTURE_ALL_SHIFT 0x8
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_MASK_SHIFT 0x8

#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_2D_VIC_ORDER_SHIFT 0x4
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_DETAIL_SHIFT 0x4
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_2D_VIC_ORDER_MASK 0xF0
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_STRUCTURE_MASK 0xF
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_3D_DETAIL_MASK 0xF0
#define HDMI_VENDOR_SPECIFIC_DATA_BLOCK_RESERVED_MASK 0xF

struct speaker_allocation_data_block {
	uint8_t spkr_data[3];
};

struct colorimetry_data_block {
	uint8_t byte3; /* bit 0~1 contains the setting for xvYCCC*/
	uint8_t byte4; /* bit ~4 contains the setting for MD0~MD3*/
};

struct video_capability_data_block {
	uint8_t byte3;
};

struct short_descr_info {
	uint32_t offset;
	uint32_t len;
};

struct latency_field {
	bool valid;
	uint8_t video_latency;
	uint8_t audio_latency;
};

struct latency_fields {
	struct latency_field latency;
	struct latency_field i_latency;
};

enum stereo_3d_all_support {
	STEREO_3D_ALL_FRAME_PACKING = 0,
	STEREO_3D_ALL_TOP_AND_BOTTOM = 6,
	STEREO_3D_ALL_SIDE_BY_SIDE_HALF = 8
};


struct cea_3d_format_support {
	bool FRAME_PACKING:1;
	bool SIDE_BY_SIDE_HALF:1;
	bool TOP_AND_BOTTOM:1;
};

static const struct cea_3d_format_support
		cea_mandatory_3d_format_supported[] = {

	{ false, false, false },/* VIC 1*/
	{ false, false, false },/* VIC 2*/
	{ false, false, false },/* VIC 3*/
	/*VIC 4 (Frame Packing, Top-and-Bottom) 1280x720@60 (59.94)*/
	{ true, false, true },/*VIC 4*/
	/* VIC 5 (Side-by-Side)   1920x1080i@60 (59.94) */
	{ false, true, false },/* VIC 5*/
	{ false, false, false },/* VIC 6*/
	{ false, false, false },/* VIC 7*/
	{ false, false, false },/* VIC 8*/
	{ false, false, false },/* VIC 9*/

	{ false, false, false },/* VIC 10*/
	{ false, false, false },/* VIC 11*/
	{ false, false, false },/* VIC 12*/
	{ false, false, false },/* VIC 13*/
	{ false, false, false },/* VIC 14*/
	{ false, false, false },/* VIC 15*/
	{ false, false, false },/* VIC 16*/
	{ false, false, false },/* VIC 17*/
	{ false, false, false },/* VIC 18*/
	/* VIC 19 (Frame Packing, Top-and-Bottom) 1280x720p@50*/
	{ true, false, true},/* VIC 19*/
	/* VIC 20 (Side-by-Side) 1920x1080i@50 */
	{ false, true, false },/* VIC 20*/
	{ false, false, false },/* VIC 21*/
	{ false, false, false },/* VIC 22*/
	{ false, false, false },/* VIC 23*/
	{ false, false, false },/* VIC 24*/
	{ false, false, false },/* VIC 25*/
	{ false, false, false },/* VIC 26*/
	{ false, false, false },/* VIC 27*/
	{ false, false, false },/* VIC 28*/
	{ false, false, false },/* VIC 29*/

	{ false, false, false },/* VIC 30*/
	{ false, false, false },/* VIC 31*/
	/*VIC 32 (Frame Packing, Top-and-Bottom) 1920x1080@24 (23.98)*/
	{ true, false, true },/*VIC 32*/
	{ false, false, false },/* VIC 33*/
	{ false, false, false },/* VIC 34*/
	{ false, false, false },/* VIC 35*/
	{ false, false, false },/* VIC 36*/
	{ false, false, false },/* VIC 37*/
	{ false, false, false },/* VIC 38*/
	{ false, false, false },/* VIC 39*/

	{ false, false, false },/* VIC 40*/
	{ false, false, false },/* VIC 41*/
	{ false, false, false },/* VIC 42*/
	{ false, false, false },/* VIC 43*/
	{ false, false, false },/* VIC 44*/
	{ false, false, false },/* VIC 45*/
	{ false, false, false },/* VIC 46*/
	{ false, false, false },/* VIC 47*/
	{ false, false, false },/* VIC 48*/
	{ false, false, false },/* VIC 49*/

	{ false, false, false },/* VIC 50*/
	{ false, false, false },/* VIC 51*/
	{ false, false, false },/* VIC 52*/
	{ false, false, false },/* VIC 53*/
	{ false, false, false },/* VIC 54*/
	{ false, false, false },/* VIC 55*/
	{ false, false, false },/* VIC 56*/
	{ false, false, false },/* VIC 57*/
	{ false, false, false },/* VIC 58*/
	{ false, false, false },/* VIC 59*/

	{ false, false, false },/* VIC 60*/
	{ false, false, false },/* VIC 61*/
	{ false, false, false }, /* VIC 62*/

	/*63~127 Reserved for future.*/
};

static const struct cea_3d_format_support cea_all_3d_format_supported[] = {

	{ false, false, false },/* VIC 1 */
	{ false, false, false },/* VIC 2 */
	{ false, false, false },/* VIC 3 */
	/* VIC 4 (Frame Packing(M), Side-by-Side(P),
	Top-and-Bottom(M)) 1280x720@60 (59.94)*/
	{ true, true, true },/* VIC 4*/
	/* VIC 5 (Frame Packing(P), Side-by-Side(M)) 1920x1080i@60 (59.94)*/
	{ true, true, false },/* VIC 5*/
	{ false, false, false },/* VIC 6*/
	{ false, false, false },/* VIC 7*/
	{ false, false, false },/* VIC 8*/
	{ false, false, false },/* VIC 9*/

	{ false, false, false },/* VIC 10*/
	{ false, false, false },/* VIC 11*/
	{ false, false, false },/* VIC 12*/
	{ false, false, false },/* VIC 13*/
	{ false, false, false },/* VIC 14*/
	{ false, false, false },/* VIC 15*/
	/* VIC 16 (Frame Packing, Side-by-Side,
	Top-and-Bottom(P)) 1920x1080p@60 (59.94)*/
	{ true, true, true },/* VIC 16*/
	{ false, false, false },/* VIC 17*/
	{ false, false, false },/* VIC 18*/
	/* VIC 19 (Frame Packing(M), Side-by-Side(P),
	Top-and-Bottom(M)) 1280x720p@50*/
	{ true, true, true },/* VIC 19*/

	/* VIC 20 (Frame Packing(P), Side-by-Side(M)) 1920x1080i@50*/
	{ true, true, false },/* VIC 20*/
	{ false, false, false },/* VIC 21*/
	{ false, false, false },/* VIC 22*/
	{ false, false, false },/* VIC 23*/
	{ false, false, false },/* VIC 24*/
	{ false, false, false },/* VIC 25*/
	{ false, false, false },/* VIC 26*/
	{ false, false, false },/* VIC 27*/
	{ false, false, false },/* VIC 28*/
	{ false, false, false },/* VIC 29*/

	{ false, false, false },/* VIC 30*/
	/* VIC 31 (Frame Packing, Top-and-Bottom(P)) 1920x1080p@50*/
	{ true, false, true },/* VIC 31*/
	/* VIC 32 (Frame Packing(M), Side-by-Side(P),
	Top-and-Bottom(M)) 1920x1080@24 (23.98)*/
	{ true, true, true },/* VIC 32*/
	{ false, false, false },/* VIC 33*/
	/* VIC 34 (Frame Packing(P), Top-and-Bottom(P)) 1920x1080p@30 (29.97)*/
	{ true, false, true },/* VIC 34 */
	{ false, false, false },/* VIC 35*/
	{ false, false, false },/* VIC 36*/
	{ false, false, false },/* VIC 37*/
	{ false, false, false },/* VIC 38*/
	{ false, false, false },/* VIC 39*/

	{ false, false, false },/* VIC 40*/
	{ false, false, false },/* VIC 41*/
	{ false, false, false },/* VIC 42*/
	{ false, false, false },/* VIC 43*/
	{ false, false, false },/* VIC 44*/
	{ false, false, false },/* VIC 45*/
	{ false, false, false },/* VIC 46*/
	/* VIC 47 (Frame Packing) 1280x720p@120 (119.88)*/
	{ true, false, false },/* VIC 47*/
	{ false, false, false },/* VIC 48*/
	{ false, false, false },/* VIC 49*/

	{ false, false, false },/* VIC 50*/
	{ false, false, false },/* VIC 51*/
	{ false, false, false },/* VIC 52*/
	{ false, false, false },/* VIC 53*/
	{ false, false, false },/* VIC 54*/
	{ false, false, false },/* VIC 55*/
	{ false, false, false },/* VIC 56*/
	{ false, false, false },/* VIC 57*/
	{ false, false, false },/* VIC 58*/
	{ false, false, false },/* VIC 59*/

	/* VIC 60 (Frame Packing(P)) 1280x720p@24 (23.98)*/
	{ true, false, false },/* VIC 60*/
	{ false, false, false },/* VIC 61 */
	/* VIC 62 (Frame Packing(P)) 1280x720p@30 (29.97)*/
	{ true, false, false },/* VIC 62*/
	/*63~127 Reserved for future.*/
};

struct cea_extended_3d_support {
	uint8_t CODE:4;
	uint8_t VIC_INDEX:4;
	uint8_t RESERVED:4;
	uint8_t DETAIL:4;
};

struct additional_video_fields {
	bool valid;
	struct cea_hdmi_vsdb_extended_caps cea_hdmi_vsdb_ext_cap;
	struct cea_3d_format_support stereo_3D_all_support;
	uint16_t stereo_3d_mask;
	uint32_t hdmi_3d_offset;
	uint32_t hdmi_3d_ext_len;
	struct stereo_3d_extended_support
		stereo_3d_ext_support[MAX_NUM_OF_HDMI_VSDB_3D_EXTENDED_SUPPORT];
	enum cea_hdmi_vic hdmi_vic[MAX_NUM_OF_HDMI_VSDB_VICS];
};

struct vendor_specific_data_block {
	uint8_t ieee_id[3];
	/*4*4 bits physical address.*/
	uint8_t commonent_phy_addr[2];

	uint8_t byte6;
/*bit 7 contains the SUPPORTS_AI, 1 - the sink accepts ACP,ISRC1, ISRC2*/
/*bits 3-6 declare supports of different pixel format*/
/*bit 0 declare support of DVI dual-link*/
	uint8_t max_tmds_clk;
	uint8_t byte8;
/*if bit 7 set - Video/Audio latency fields present*/
/* if bit 6 set - two pairs of Video/Audio latency fields present -
progressive & interlaced*/
/*if bit 5 set - additional video format capabilities are present,
which are described by the fields following the latency ones*/
/*bits 0-3 indicate which content type is supported*/
	uint32_t video_latency;
	uint32_t audio_latency;
	uint32_t i_video_latency;
	uint32_t i_audio_latency;

	struct additional_video_fields video_fields;
};

struct edid_ext_cea {
	struct edid_base edid;
	struct edid_data_cea861_ext *data;
	bool mandatory_3d_support;
	enum stereo_3d_multi_presence multi_3d_support;
	struct cea_3d_format_support
		cached_multi_3d_support[MAX_NUM_OF_HDMI_VSDB_3D_MULTI_SUPPORT];
	struct stereo_3d_extended_support
		cached_ext_3d_support[MAX_NUM_OF_HDMI_VSDB_3D_EXTENDED_SUPPORT];
	struct edid_patch *edid_patch;
};

#define FROM_EDID(e) container_of((e), struct edid_ext_cea, edid)

#define CEA_861B_DATA_BLOCK_START 0X04
#define CEA_861B_GET_DATA_BLOCK_TAG(data) (data >> 5)
#define CEA_861B_GET_DATA_BLOCK_LEN(data) (data & 0x1F)

bool dal_edid_ext_cea_is_cea_ext(uint32_t len, const uint8_t *buf)
{
	const struct edid_data_cea861_ext *ext;

	if (len < sizeof(struct edid_data_cea861_ext))
		return false; /* CEA extension is 128 byte in length*/

	ext = (const struct edid_data_cea861_ext *)buf;

	if (!(EDID_EXTENSION_TAG_CEA861_EXT == ext->extension_tag))
		return false; /* Tag says it's not CEA ext*/

	return true;
}

static bool find_short_descr(
	const struct edid_data_cea861_ext *data,
	uint8_t start_offs,
	uint8_t tag,
	uint8_t ext_tag,
	struct short_descr_info *descr)
{
/* initial setup end of descriptor data at end of the extension block*/
	uint8_t descr_end = CEA861_MAX_DATA_BLOKS_SIZE - 1;
	uint8_t i = start_offs;

	ASSERT(data != NULL);
	ASSERT(descr != NULL);
	ASSERT(
		data->timing_data_start >= CEA_861B_DATA_BLOCK_START ||
		data->timing_data_start == 0);

	if (data->timing_data_start >= CEA_861B_DATA_BLOCK_START)
		descr_end = data->timing_data_start - CEA_861B_DATA_BLOCK_START;

	while (i < descr_end) {
		uint8_t data_block_tag = CEA_861B_GET_DATA_BLOCK_TAG(
			data->data_block[i]);
		uint8_t data_block_len = CEA_861B_GET_DATA_BLOCK_LEN(
			data->data_block[i]);

		if (tag == CEA_861B_DATA_BLOCK_TAGCODE_USE_EXTENDED_TAG) {

			uint8_t data_block_ext_tag = data->data_block[i+1];

			if (data_block_ext_tag == ext_tag) {
				/* block length includes extended tag byte*/
				descr->len = data_block_len - 1;
				/* i point to the block tag,
				i+1 is extended tag byte,
				 so i+2 is extended data block*/
				descr->offset = i + 2;
				return true;
			}
		} else {
			if (data_block_tag == tag) {
				descr->len = data_block_len;
				/*i point to the block tag so i+1 is the 1st
				short descriptor*/
				descr->offset = i + 1;
				return true;
			}
		}

		/*next descriptor block starts at block length + 1
		to account for first tag byte*/
		i += data_block_len + 1;
	}
	return false;
}

static bool add_detailed_timings(
	struct edid_ext_cea *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i;
	struct mode_timing mode_timing;
	const struct edid_detailed *detailed;
	uint32_t num_of_detailed;

	ASSERT(list != NULL);
	ASSERT(preferred_mode_found != NULL);

	/* Get detailed timing from CEA861 extension*/
	if (edid->data->timing_data_start < 4)
		return false;

	/* Some HDMI displays report the wrong total number of DTDs.
	To avoid missing some included DTDs we are using check that we are not
	 accessing the EDID block out of range.The last byte of the i-th
	 detailed timing should be within the CEA block*/

	num_of_detailed = (sizeof(struct edid_data_cea861_ext) -
		edid->data->timing_data_start) / sizeof(struct edid_detailed);

	detailed = (const struct edid_detailed *)
			((uint8_t *)edid->data + edid->data->timing_data_start);

	for (i = 0; i < num_of_detailed; ++i) {

		dal_memset(&mode_timing, 0, sizeof(struct mode_timing));

		if (!dal_edid_detailed_to_timing(
			&edid->edid,
			&detailed[i],
			true,
			&mode_timing.crtc_timing))
			continue;

		dal_edid_timing_to_mode_info(&mode_timing.crtc_timing,
			&mode_timing.mode_info);

		if (mode_timing.mode_info.flags.INTERLACE
			&& mode_timing.mode_info.pixel_width == 1440
			&& mode_timing.mode_info.pixel_height == 480) {
			/*
			 * Detailed Timing has no way of specifying
			 * pixelRepetition here we check if the Timing just
			 * parsed is 480i pixelRepetion. if so we adjust the
			 *  ModeTiming accordingly*/
			mode_timing.mode_info.pixel_width /= 2;
			mode_timing.crtc_timing.flags.PIXEL_REPETITION = 2;
		}

		/* default to RGB 8bit */
		mode_timing.crtc_timing.display_color_depth =
			DISPLAY_COLOR_DEPTH_888;
		mode_timing.crtc_timing.pixel_encoding = PIXEL_ENCODING_RGB;

		/*If preferred mode yet not found -
		 * select first detailed mode/timing as preferred*/
		if (!(*preferred_mode_found)) {
			mode_timing.mode_info.flags.PREFERRED = 1;
			*preferred_mode_found = true;
		}

		dal_dcs_mode_timing_list_append(list, &mode_timing);
		ret = true;
	}
	return ret;
}

static uint32_t get_supported_3d_formats(
	struct edid_ext_cea *ext,
	uint32_t vic,
	uint32_t vic_idx)
{
	uint32_t formats = 0;
	const struct cea_3d_format_support *support;

	if (vic == 0 || vic > MAX_NUM_SHORT_VIDEO_DESCRIPTOR_FORMAT)
		return formats;

	/*Get 3D support from driver table (these are 2D timings which has
	3D mandatory support when sink reports stereo flag)*/
	if (ext->mandatory_3d_support) {
		const struct cea_3d_format_support *support =
			&cea_mandatory_3d_format_supported[vic-1];

		if (support->FRAME_PACKING)
			formats |= (1 << TIMING_3D_FORMAT_HW_FRAME_PACKING);

		if (support->SIDE_BY_SIDE_HALF)
			formats |= (1 << TIMING_3D_FORMAT_SIDE_BY_SIDE);

		if (support->TOP_AND_BOTTOM)
			formats |= (1 << TIMING_3D_FORMAT_TOP_AND_BOTTOM);
	}

	/*for multi and extended 3D support, we only care about the first
	16 entries in the EDID*/
	if (vic_idx >= 16)
		return formats;

	support = &cea_all_3d_format_supported[vic - 1];

	if (ext->multi_3d_support == STEREO_3D_MULTI_ALL_FORMATS ||
		ext->multi_3d_support == STEREO_3D_MULTI_MASKED_FORMATS) {

		if (support->FRAME_PACKING &&
			ext->cached_multi_3d_support[vic_idx].FRAME_PACKING)
			formats |= (1 << TIMING_3D_FORMAT_HW_FRAME_PACKING);

		if (support->SIDE_BY_SIDE_HALF &&
			ext->cached_multi_3d_support[vic_idx].SIDE_BY_SIDE_HALF)
			formats |= (1 << TIMING_3D_FORMAT_SIDE_BY_SIDE);

		if (support->TOP_AND_BOTTOM &&
			ext->cached_multi_3d_support[vic_idx].TOP_AND_BOTTOM)
			formats |= (1 << TIMING_3D_FORMAT_TOP_AND_BOTTOM);
	}

	/* check for extended 3D support */
	if (support->FRAME_PACKING &&
		ext->cached_ext_3d_support[vic_idx].format.FRAME_PACKING)
		formats |= (1 << TIMING_3D_FORMAT_HW_FRAME_PACKING);

	if (support->SIDE_BY_SIDE_HALF &&
		ext->cached_ext_3d_support[vic_idx].format.SIDE_BY_SIDE_HALF)
		formats |= (1 << TIMING_3D_FORMAT_SIDE_BY_SIDE);

	if (support->TOP_AND_BOTTOM &&
		ext->cached_ext_3d_support[vic_idx].format.TOP_AND_BOTTOM)
		formats |= (1 << TIMING_3D_FORMAT_TOP_AND_BOTTOM);

	return formats;
}

static bool retrieve_cea861b_svd_mode_timing(
	struct edid_ext_cea *ext,
	uint8_t svd_code,
	bool video_optimized_rate,
	enum timing_3d_format timing_3D_format,
	bool multi_stereo_mode,
	struct mode_timing *mode_timing)
{
	if (!dal_timing_service_get_mode_timing_for_video_code(
		ext->edid.ts,
		svd_code & CEA_SHORT_VIDEO_DESCRIPTION_VIDEO_ID_CODE_MASK,
		video_optimized_rate,
		mode_timing))
		return false;

	if (svd_code & CEA_SHORT_VIDEO_DESCRIPTION_NATIVE_MASK)
		mode_timing->mode_info.flags.NATIVE = 1;

	/* default to RGB 8bit */
	mode_timing->crtc_timing.display_color_depth = DISPLAY_COLOR_DEPTH_888;
	mode_timing->crtc_timing.pixel_encoding = PIXEL_ENCODING_RGB;

	/* Setup timing source - if this mode/timing is 3D or belongs to
	3D group - make it CEA_SVD_3D*/
	if (multi_stereo_mode || timing_3D_format != TIMING_3D_FORMAT_NONE)
		mode_timing->mode_info.timing_source =
			TIMING_SOURCE_EDID_CEA_SVD_3D;
	else
		mode_timing->mode_info.timing_source =
			TIMING_SOURCE_EDID_CEA_SVD;

	/* If this mode/timing belongs to 3D group - set these two flags*/
	mode_timing->crtc_timing.flags.USE_IN_3D_VIEW_ONLY =
		(multi_stereo_mode &&
			timing_3D_format != TIMING_3D_FORMAT_NONE);

	mode_timing->crtc_timing.flags.STEREO_3D_PREFERENCE =
		(multi_stereo_mode &&
			timing_3D_format == TIMING_3D_FORMAT_NONE);

	mode_timing->crtc_timing.timing_3d_format = timing_3D_format;

	return true;
}

static bool add_svd_mode_timing_from_svd_code(
	struct edid_ext_cea *ext,
	struct dcs_mode_timing_list *list,
	uint8_t svd_code,
	uint8_t vic_idx)
{
	bool ret = false;
	struct mode_timing mode_timing;
	uint32_t format;
	uint32_t stereo_3d_formats = get_supported_3d_formats(
			ext,
			svd_code &
			CEA_SHORT_VIDEO_DESCRIPTION_VIDEO_ID_CODE_MASK,
			vic_idx);

	/* We add separate 2D timing if this is multi-stereo mode
	(support 2 or more 3D formats) or we do not have 3D formats
	at all - need to add 2D timign only*/
	bool multi_stereo_mode =
		((stereo_3d_formats & (stereo_3d_formats - 1)) != 0);

	if (stereo_3d_formats == 0 || multi_stereo_mode)
		stereo_3d_formats =
			stereo_3d_formats | (1 << TIMING_3D_FORMAT_NONE);

	for (format = 0; format < TIMING_3D_FORMAT_MAX; ++format) {

		if (!(stereo_3d_formats & (1 << format)))
			continue;

		/* get non video optimized version*/
		if (!retrieve_cea861b_svd_mode_timing(
			ext,
			svd_code,
			false,
			format,
			multi_stereo_mode,
			&mode_timing))
			continue;

		dal_dcs_mode_timing_list_append(list, &mode_timing);
		ret = true;

		/* try video optimized version*/
		if (!retrieve_cea861b_svd_mode_timing(
			ext,
			svd_code,
			true,
			format,
			multi_stereo_mode,
			&mode_timing))
			continue;

		dal_dcs_mode_timing_list_append(list, &mode_timing);
		ret = true;
	}
	return ret;
}

static bool add_svd_mode_timings(
	struct edid_ext_cea *ext,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint8_t offset = 0;
	struct short_descr_info descr = { 0 };
	uint32_t vic_idx = 0;

	ASSERT(list != NULL);

	/* loop through all short video descriptors*/
	while (find_short_descr(
		ext->data,
		offset,
		CEA_861B_DATA_BLOCK_TAGCODE_SHORT_VIDEO_DESCRIPTOR,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
		&descr)) {
		/*get the SVDs*/
		const uint8_t *svd = &ext->data->data_block[descr.offset];
		uint32_t i;

		for (i = 0; i < descr.len; ++i) {

			if (add_svd_mode_timing_from_svd_code(
				ext, list, svd[i], vic_idx))
				ret = true;

			vic_idx++;
		}

		/* start next search at the end of this descriptor block*/
		offset = (uint8_t)(descr.offset + descr.len);

	}

	return ret;
}

#define GET_BIT(byte, bit) ((byte >> bit) & 0x01)
#define GET_BITS(byte, bit, count) ((byte >> bit) & ((1<<count)-1))

static void get_additional_video_fields(
	struct edid_ext_cea *ext,
	const struct short_descr_info *descr,
	struct additional_video_fields *video_fields)
{
	uint8_t byte8;
	uint32_t offset;
	uint32_t bytes_to_parse;
	uint32_t i;
	uint32_t ext_3d_to_parse = 0;
	uint32_t ext_3d_parsed = 0;

	ASSERT(descr != NULL);
	ASSERT(video_fields != NULL);

	dal_memset(video_fields, 0, sizeof(struct additional_video_fields));

	/*Check if additional video parameters present*/
	if (descr->len < 8)
		return;

	/*In byte 8 VSDB defines which of the optional video fields present
	(latency, interlaced latency and HDMI Video)*/
	byte8 = ext->data->data_block[descr->offset + 7];
	/* We start at byte 9.*/
	offset = descr->offset + 8;

	if (!(byte8 & HDMI_VENDOR_SPECIFIC_DATA_BLOCK_HDMI_VIDEO_PRESENT))
		return;

	video_fields->valid = false;

	/* Increase offset if latency field present*/
	if (byte8 & HDMI_VENDOR_SPECIFIC_DATA_BLOCK_LATENCY_PRESENT)
		offset += 2;

	/* Increase offset if interlaced latency field present*/
	if (byte8 & HDMI_VENDOR_SPECIFIC_DATA_BLOCK_I_LATENCY_PRESENT)
		offset += 2;

	bytes_to_parse = descr->len + descr->offset - offset;

	/* Finally get additional video parameters*/
	if (bytes_to_parse >= 2) {
		video_fields->valid = true;
		video_fields->cea_hdmi_vsdb_ext_cap.stereo_3d_present =
			GET_BIT(ext->data->data_block[offset], 7);
		video_fields->cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present =
			GET_BITS(ext->data->data_block[offset], 5, 2);
		video_fields->cea_hdmi_vsdb_ext_cap.image_size =
			GET_BITS(ext->data->data_block[offset], 3, 2);
		offset += 1;

		video_fields->cea_hdmi_vsdb_ext_cap.hdmi_vic_len =
			GET_BITS(ext->data->data_block[offset], 5, 3);
		video_fields->cea_hdmi_vsdb_ext_cap.hdmi_3d_len =
			GET_BITS(ext->data->data_block[offset], 0, 5);

		offset += 1;
		bytes_to_parse -= 2;
	}

	for (i = 0; i < video_fields->cea_hdmi_vsdb_ext_cap.hdmi_vic_len; ++i)
		video_fields->hdmi_vic[i] = ext->data->data_block[offset+i];

	offset += video_fields->cea_hdmi_vsdb_ext_cap.hdmi_vic_len;
	bytes_to_parse -= video_fields->cea_hdmi_vsdb_ext_cap.hdmi_vic_len;
	video_fields->hdmi_3d_offset = 0;

	if (bytes_to_parse >= 2 &&
		(video_fields->cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
		STEREO_3D_MULTI_ALL_FORMATS ||
		video_fields->cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
			STEREO_3D_MULTI_MASKED_FORMATS)) {

		uint16_t s3d_support = (ext->data->data_block[offset] << 8) +
			ext->data->data_block[offset+1];
		/* From Table 8-19 - 3D_Structure_ALL of HDMI 1.4a spec:
		 Bit 0 specifies whether frame packing is supported
		 Bit 6 specifies whether top-and-bottom is supported
		 Bit 8 specifies whether side-by-side (half) is supported*/

		video_fields->stereo_3D_all_support.FRAME_PACKING =
			GET_BIT(s3d_support, STEREO_3D_ALL_FRAME_PACKING);

		video_fields->stereo_3D_all_support.TOP_AND_BOTTOM =
			GET_BIT(s3d_support, STEREO_3D_ALL_TOP_AND_BOTTOM);

		video_fields->stereo_3D_all_support.SIDE_BY_SIDE_HALF =
			GET_BIT(s3d_support, STEREO_3D_ALL_SIDE_BY_SIDE_HALF);

		video_fields->hdmi_3d_offset += 2;
		offset += 2;
		bytes_to_parse -= 2;
	}

	if (bytes_to_parse >= 2 &&
		video_fields->cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
			STEREO_3D_MULTI_MASKED_FORMATS) {

		video_fields->stereo_3d_mask =
			(ext->data->data_block[offset] << 8) +
			ext->data->data_block[offset+1];

		video_fields->hdmi_3d_offset += 2;
		offset += 2;
		bytes_to_parse -= 2;
	}

	ASSERT(video_fields->cea_hdmi_vsdb_ext_cap.hdmi_3d_len >=
		video_fields->hdmi_3d_offset);

	/*HDMI_3D_LEN indicates the total length of following 3D video format
	capabilities including 3D_STRUCTURE_ALL_15...0,
	3D_MASK_15...0, 2D_VIC_order_X, 3D_STRUCTURE_X, and 3D_Detail_X fields
	3D_STRUCTURE_ALL_15...0 and 3D_MASK_15...0 take 2 bytes each, so
	if numBytesToParse > 0 and pVideoFields->hdmi3DLength > 2 or 4
	(depending on the presence of 3D_MASK_15...0), then there are fields
	for 2D_VIC_order_X, 3D_STRUCTURE_X, and 3D_Detail_X*/
	if (video_fields->cea_hdmi_vsdb_ext_cap.hdmi_3d_len >
		video_fields->hdmi_3d_offset && bytes_to_parse > 0)
		ext_3d_to_parse =
			video_fields->cea_hdmi_vsdb_ext_cap.hdmi_3d_len -
			video_fields->hdmi_3d_offset;

	while (ext_3d_to_parse > 0) {
		struct cea_extended_3d_support ext_3d_support = { 0 };
		struct stereo_3d_extended_support *video_ext_3d;
		uint32_t parsed = 0;

		ext_3d_support.VIC_INDEX =
			GET_BITS(ext->data->data_block[offset], 4, 4);

		ext_3d_support.CODE =
			GET_BITS(ext->data->data_block[offset], 0, 4);

		offset += 1;

		/* if 3D_Structure_X is in the range of 0000 to 0111,
		 there are no 3D detail and reserved fields*/
		if (ext_3d_support.CODE >= STEREO_3D_ALL_SIDE_BY_SIDE_HALF) {
			ext_3d_support.DETAIL =
				GET_BITS(ext->data->data_block[offset], 4, 4);
			ext_3d_support.RESERVED =
				GET_BITS(ext->data->data_block[offset], 0, 4);
			offset += 1;
		}

		video_ext_3d =
			&video_fields->stereo_3d_ext_support[ext_3d_parsed];
		switch (ext_3d_support.CODE) {
		case STEREO_3D_ALL_FRAME_PACKING:
			video_ext_3d->format.FRAME_PACKING = true;
			video_ext_3d->vic_index = ext_3d_support.VIC_INDEX;
			video_ext_3d->size = 1;
			ext_3d_parsed++;
			parsed = 1;
			break;
		case STEREO_3D_ALL_TOP_AND_BOTTOM:
			video_ext_3d->format.TOP_AND_BOTTOM = true;
			video_ext_3d->vic_index = ext_3d_support.VIC_INDEX;
			video_ext_3d->size = 1;
			ext_3d_parsed++;
			parsed = 1;
			break;
		case STEREO_3D_ALL_SIDE_BY_SIDE_HALF:
			video_ext_3d->format.SIDE_BY_SIDE_HALF = true;
			video_ext_3d->vic_index = ext_3d_support.VIC_INDEX;
			video_ext_3d->value = ext_3d_support.DETAIL;
			video_ext_3d->size = 2;
			ext_3d_parsed++;
			parsed = 2;
			break;
		default:
			parsed = (ext_3d_support.CODE >=
				STEREO_3D_ALL_SIDE_BY_SIDE_HALF ? 2 : 1);
			break;
		}

		ASSERT(ext_3d_to_parse >= parsed);

		if (ext_3d_to_parse >= parsed)
			ext_3d_to_parse -= parsed;
		else
			ext_3d_to_parse = 0;
	}

	video_fields->hdmi_3d_ext_len = ext_3d_parsed;
}

static bool get_timing_for_hdmi_vic(
	struct edid_ext_cea *ext,
	enum cea_hdmi_vic vic,
	bool video_optimized_rate,
	struct mode_timing *mode_timing)
{
	if (vic == CEA_HDMI_VIC_RESERVED ||
		(vic == CEA_HDMI_VIC_4KX2K_25 && video_optimized_rate) ||
		(vic == CEA_HDMI_VIC_4KX2K_24_SMPTE && video_optimized_rate))
		return false;

	if (dal_timing_service_get_mode_timing_for_hdmi_video_code(
		ext->edid.ts, vic, video_optimized_rate, mode_timing)) {
		/* default to RGB - 8bit*/
		mode_timing->crtc_timing.display_color_depth =
			DISPLAY_COLOR_DEPTH_888;
		mode_timing->crtc_timing.pixel_encoding =
			PIXEL_ENCODING_RGB;

		return true;
	}

	return false;
}

static bool add_hdmi_vic_timings(
	struct edid_ext_cea *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	bool ret = false;
	uint32_t i;
	struct short_descr_info descr = { 0 };
	struct additional_video_fields video_fields;

	ASSERT(list != NULL);

	if (!find_short_descr(
		edid->data,
		0,
		CEA_861B_DATA_BLOCK_TAGCODE_VENDOR_SPECIFIC_DATA_BLOCK,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
		&descr))
		return false;

		get_additional_video_fields(edid, &descr, &video_fields);

		for (i = 0;
			i < video_fields.cea_hdmi_vsdb_ext_cap.hdmi_vic_len;
			++i) {
			/* get non video optimized version */
			struct mode_timing mode_timing;

			if (get_timing_for_hdmi_vic(
				edid,
				video_fields.hdmi_vic[i],
				false,
				&mode_timing)) {

				dal_dcs_mode_timing_list_append(
					list, &mode_timing);
				ret = true;
			}

			/* try video optimized version*/
			if (get_timing_for_hdmi_vic(
				edid,
				video_fields.hdmi_vic[i],
				true,
				&mode_timing)) {
				dal_dcs_mode_timing_list_append(
					list, &mode_timing);
				ret = true;
			}
		}

	return ret;
}

static void get_latency_fields(
	struct edid_ext_cea *ext,
	const struct short_descr_info *descr,
	struct latency_fields *latency_fields)
{
	uint8_t byte8;
	uint32_t offset;

	ASSERT(descr != NULL);
	ASSERT(latency_fields != NULL);

	dal_memset(latency_fields, 0, sizeof(struct latency_fields));

	if (descr->len < 8)
		return;

	byte8 = ext->data->data_block[descr->offset + 7];
	offset = descr->offset + 8;

	if (byte8 & HDMI_VENDOR_SPECIFIC_DATA_BLOCK_LATENCY_PRESENT) {

		ASSERT(offset + 1 < descr->offset + descr->len);

		latency_fields->latency.valid = true;

		latency_fields->latency.video_latency =
			ext->data->data_block[offset];

		latency_fields->latency.audio_latency =
			ext->data->data_block[offset + 1];

		offset += 2;
	}

	if (byte8 & HDMI_VENDOR_SPECIFIC_DATA_BLOCK_I_LATENCY_PRESENT) {

		ASSERT(offset + 1 < descr->offset + descr->len);

		latency_fields->i_latency.valid = true;

		latency_fields->i_latency.video_latency =
			ext->data->data_block[offset];

		latency_fields->i_latency.audio_latency =
			ext->data->data_block[offset + 1];

		offset += 2;
	}
}

static bool get_cea_vendor_specific_data_block(
	struct edid_base *edid,
	struct cea_vendor_specific_data_block *vendor_block)
{
	/* here we assume it is only one block and just take the first one*/
	struct short_descr_info descr = { 0 };
	const struct vendor_specific_data_block *vsdb;
	struct latency_fields latency_fields = { { 0 } };
	struct additional_video_fields video_fields = { 0 };
	struct edid_ext_cea *ext = FROM_EDID(edid);
	uint32_t i;

	if (vendor_block == NULL)
		return false;

	dal_memset(
		vendor_block, 0, sizeof(struct cea_vendor_specific_data_block));

	if (!find_short_descr(
		ext->data,
		0,
		CEA_861B_DATA_BLOCK_TAGCODE_VENDOR_SPECIFIC_DATA_BLOCK,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
		&descr))
		return false;

	vsdb = (const struct vendor_specific_data_block *)
		(&ext->data->data_block[descr.offset]);

	/* Translate */
	vendor_block->ieee_id = (vsdb->ieee_id[2] << 16) +
		(vsdb->ieee_id[1] << 8) + (vsdb->ieee_id[0]);
	vendor_block->commonent_phy_addr.PHY_ADDR_A =
		GET_BITS(vsdb->commonent_phy_addr[0], 4, 4);
	vendor_block->commonent_phy_addr.PHY_ADDR_B =
		GET_BITS(vsdb->commonent_phy_addr[0], 0, 4);
	vendor_block->commonent_phy_addr.PHY_ADDR_C =
		GET_BITS(vsdb->commonent_phy_addr[1], 4, 4);
	vendor_block->commonent_phy_addr.PHY_ADDR_D =
		GET_BITS(vsdb->commonent_phy_addr[0], 0, 4);

	/* optional 6th byte */
	if (descr.len >= 6) {
		vendor_block->byte6.SUPPORTS_AI = GET_BIT(vsdb->byte6, 7);
		vendor_block->byte6.DC_48BIT = GET_BIT(vsdb->byte6, 6);
		vendor_block->byte6.DC_36BIT = GET_BIT(vsdb->byte6, 5);
		vendor_block->byte6.DC_30BIT = GET_BIT(vsdb->byte6, 4);
		vendor_block->byte6.DC_Y444 = GET_BIT(vsdb->byte6, 3);
		vendor_block->byte6.DVI_DUAL = GET_BIT(vsdb->byte6, 0);
		vendor_block->byte6_valid = true;
	}

	/* optional 7th byte */
	if (descr.len >= 7) {
		const struct monitor_patch_info *patch_info;

		vendor_block->max_tmds_clk_mhz = vsdb->max_tmds_clk * 5;

		patch_info =
			dal_edid_patch_get_monitor_patch_info(
				ext->edid_patch,
				MONITOR_PATCH_TYPE_DO_NOT_USE_EDID_MAX_PIX_CLK);

		if (patch_info)
			vendor_block->max_tmds_clk_mhz = patch_info->param;
	}

	/* optional 8th byte*/
	if (descr.len >= 8) {
		/* bits 0 - 3 indicate which content type is supported*/
		vendor_block->byte8.CNC0_GRAPHICS = GET_BIT(vsdb->byte8, 0);
		vendor_block->byte8.CNC1_PHOTO = GET_BIT(vsdb->byte8, 1);
		vendor_block->byte8.CNC2_CINEMA = GET_BIT(vsdb->byte8, 2);
		vendor_block->byte8.CNC3_GAME = GET_BIT(vsdb->byte8, 3);
		vendor_block->byte8.HDMI_VIDEO_PRESENT =
			GET_BIT(vsdb->byte8, 5);
	}

	/* optional latency fields (byte 9-12)*/
	get_latency_fields(ext, &descr, &latency_fields);

	if (latency_fields.latency.valid) {

		vendor_block->byte8.LATENCY_FIELDS_PRESENT = 1;
		vendor_block->video_latency =
			latency_fields.latency.video_latency;
		vendor_block->audio_latency =
			latency_fields.latency.audio_latency;
	}

	if (latency_fields.i_latency.valid) {
		vendor_block->byte8.ILATENCY_FIELDS_PRESENT = 1;
		vendor_block->i_video_latency =
			latency_fields.i_latency.video_latency;
		vendor_block->i_audio_latency =
			latency_fields.i_latency.audio_latency;

	}

	/*Here could come additional optional fields in VSDB.
	Not needed for now*/

	get_additional_video_fields(ext, &descr, &video_fields);

	if (video_fields.valid)
		vendor_block->hdmi_vsdb_extended_caps =
			video_fields.cea_hdmi_vsdb_ext_cap;

	for (i = 0; i < video_fields.cea_hdmi_vsdb_ext_cap.hdmi_vic_len; ++i)
		vendor_block->hdmi_vic[i] = video_fields.hdmi_vic[i];

	if (video_fields.cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
		STEREO_3D_MULTI_ALL_FORMATS ||
		video_fields.cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
			STEREO_3D_MULTI_MASKED_FORMATS) {

		vendor_block->stereo_3d_all_support.FRAME_PACKING =
			video_fields.stereo_3D_all_support.FRAME_PACKING;

		vendor_block->stereo_3d_all_support.SIDE_BY_SIDE_HALF =
			video_fields.stereo_3D_all_support.SIDE_BY_SIDE_HALF;

		vendor_block->stereo_3d_all_support.TOP_AND_BOTTOM =
			video_fields.stereo_3D_all_support.TOP_AND_BOTTOM;
	}

	if (video_fields.cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present ==
		STEREO_3D_MULTI_MASKED_FORMATS)
		vendor_block->stereo_3d_mask = video_fields.stereo_3d_mask;

	for (i = 0; i < video_fields.hdmi_3d_ext_len; ++i)
		vendor_block->stereo_3d_extended_support[i] =
			video_fields.stereo_3d_ext_support[i];

	return true;
}

static bool get_supported_mode_timing(
	struct edid_base *edid,
	struct dcs_mode_timing_list *list,
	bool *preferred_mode_found)
{
	struct edid_ext_cea *ext = FROM_EDID(edid);

	/* Calling sequence/order is important for preferred mode lookup*/
	bool det = add_detailed_timings(
		ext, list, preferred_mode_found);
	bool svd = add_svd_mode_timings(
		ext, list, preferred_mode_found);
	bool hdmi_vic = add_hdmi_vic_timings(
		ext, list, preferred_mode_found);

	return det || svd || hdmi_vic;

}

static bool get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	struct cea_vendor_specific_data_block vendor_block;

	if (!get_cea_vendor_specific_data_block(edid, &vendor_block))
		return false;

	if (vendor_block.ieee_id !=
		HDMI_VENDOR_SPECIFIC_DATA_BLOCK_IEEE_REGISTRATION_ID)
		return false;

	*type = EDID_CONNECTOR_HDMIA;
	return true;
}

static bool get_display_color_depth(
	struct edid_base *edid,
	struct display_color_depth_support *color_depth)
{
	struct cea_vendor_specific_data_block vendor_block;

	if (!get_cea_vendor_specific_data_block(edid, &vendor_block))
		return false;

	if (vendor_block.ieee_id !=
		HDMI_VENDOR_SPECIFIC_DATA_BLOCK_IEEE_REGISTRATION_ID)
		return false;

	if (!vendor_block.byte6_valid)
		return false;

	if (vendor_block.byte6.DC_48BIT)
		color_depth->mask |= COLOR_DEPTH_INDEX_161616;

	if (vendor_block.byte6.DC_36BIT)
		color_depth->mask |= COLOR_DEPTH_INDEX_121212;

	if (vendor_block.byte6.DC_30BIT)
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;

	return true;
}

static bool get_cea861_support(
	struct edid_base *edid,
	struct cea861_support *cea861_support)
{
	bool ret = false;
	struct edid_ext_cea *ext = FROM_EDID(edid);

	ASSERT(cea861_support != NULL);

	dal_memset(cea861_support, 0, sizeof(struct cea861_support));

	cea861_support->revision = ext->data->revision;

	if (cea861_support->revision > CEA861_VERSION_1) {

		cea861_support->raw_features = ext->data->cea861b_byte3;
		ret = true;
	}

	return ret;
}

static bool get_display_pixel_encoding(
	struct edid_base *edid,
	struct display_pixel_encoding_support *pixel_encoding)
{
	struct cea861_support cea861_support;

	if (!get_cea861_support(edid, &cea861_support))
		return false;

	/**TODO: add edid patch support*/
	if (cea861_support.features.YCRCB422)
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_YCBCR422;

	if (cea861_support.features.YCRCB444)
		pixel_encoding->mask |= PIXEL_ENCODING_MASK_YCBCR444;

	return true;
}

static bool get_cea_speaker_allocation_data_block(
	struct edid_base *edid,
	union cea_speaker_allocation_data_block *spkr_data)
{
	struct edid_ext_cea *ext = FROM_EDID(edid);
	struct short_descr_info descr;

	if (!find_short_descr(
		ext->data,
		0,
		CEA_861B_DATA_BLOCK_TAGCODE_SPKR_ALLOCATION_DATA_BLOCK,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
		&descr))
		return false;

	if (descr.len == sizeof(struct speaker_allocation_data_block))
		/* the first byte has the speaker description*/
		spkr_data->raw = ext->data->data_block[descr.offset];

	return true;
}

static bool get_cea_colorimetry_data_block(
		struct edid_base *edid,
		struct cea_colorimetry_data_block *colorimetry_data_block)
{
	struct short_descr_info descr = { 0 };
	struct colorimetry_data_block cmdb = { 0 };
	uint32_t block_len = sizeof(struct colorimetry_data_block);
	struct edid_ext_cea *ext = FROM_EDID(edid);

	ASSERT(colorimetry_data_block != NULL);

	/* here we assume it is only one block and just take the first one*/
	if (!find_short_descr(
		ext->data,
		0,
		CEA_861B_DATA_BLOCK_TAGCODE_USE_EXTENDED_TAG,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_COLORIMETRY_DATA_BLOCK,
		&descr))
		return false;

	if (descr.len < block_len)
		block_len = descr.len;

	dal_memmove(&cmdb, &ext->data->data_block[descr.offset], block_len);

	/*Translate*/
	colorimetry_data_block->flag.XV_YCC601 = GET_BIT(cmdb.byte3, 0);
	colorimetry_data_block->flag.XV_YCC709 = GET_BIT(cmdb.byte3, 1);
	colorimetry_data_block->flag.S_YCC601 = GET_BIT(cmdb.byte3, 2);
	colorimetry_data_block->flag.ADOBE_YCC601 = GET_BIT(cmdb.byte3, 3);
	colorimetry_data_block->flag.ADOBE_RGB = GET_BIT(cmdb.byte3, 4);

	colorimetry_data_block->metadata_flag.MD0 = GET_BIT(cmdb.byte4, 0);
	colorimetry_data_block->metadata_flag.MD1 = GET_BIT(cmdb.byte4, 1);
	colorimetry_data_block->metadata_flag.MD2 = GET_BIT(cmdb.byte4, 2);
	colorimetry_data_block->metadata_flag.MD3 = GET_BIT(cmdb.byte4, 3);

	return true;
}

static bool get_cea_video_capability_data_block(
		struct edid_base *edid,
		union cea_video_capability_data_block *caps)
{
	struct short_descr_info descr = { 0 };
	struct video_capability_data_block vcdb = { 0 };
	uint32_t block_len = sizeof(struct video_capability_data_block);
	struct edid_ext_cea *ext = FROM_EDID(edid);

	ASSERT(caps != NULL);

	/* here we assume it is only one block and just take the first one*/
	if (!find_short_descr(
		ext->data,
		0,
		CEA_861B_DATA_BLOCK_TAGCODE_USE_EXTENDED_TAG,
		CEA_861B_DATA_BLOCK_EXT_TAGCODE_VIDEO_CAPABILITY_DATA_BLOCK,
		&descr))
		return false;

	if (descr.len < block_len)
		block_len = descr.len;

	dal_memmove(&vcdb, &ext->data->data_block[descr.offset], block_len);

	caps->raw = vcdb.byte3;
	return true;
}

static void cache_stereo_3d_support_info(struct edid_ext_cea *ext)
{
	struct additional_video_fields video_fields;
	struct short_descr_info descr = { 0 };
	uint32_t i = 0;

	video_fields.valid = false;
	/* Get additional (optional) video capabilities from VSDB.
	Here we assume there is only one VSDB in CEA extension*/
	if (find_short_descr(
			ext->data,
			0,
			CEA_861B_DATA_BLOCK_TAGCODE_VENDOR_SPECIFIC_DATA_BLOCK,
			CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
			&descr))
		get_additional_video_fields(ext, &descr, &video_fields);


	if (!video_fields.valid)
		return;

	for (i = 0; i < video_fields.hdmi_3d_ext_len; ++i) {

		uint32_t vic_idx =
			video_fields.stereo_3d_ext_support[i].vic_index;
		ext->cached_ext_3d_support[vic_idx] =
			video_fields.stereo_3d_ext_support[i];
	}

	ext->mandatory_3d_support =
		video_fields.cea_hdmi_vsdb_ext_cap.stereo_3d_present;

	ext->multi_3d_support =
		video_fields.cea_hdmi_vsdb_ext_cap.stereo_3d_multi_present;

	if (ext->multi_3d_support != STEREO_3D_MULTI_ALL_FORMATS &&
		(ext->multi_3d_support != STEREO_3D_MULTI_MASKED_FORMATS))
		return;

	/* For the case of Stereo3DMulti_AllFormats:
	3D formats are assigned to all of the VICs listed in the first
	16 entries in the EDID.
	For the case of Stereo3DMulti_MaskedFormats:
	3D formats are assigned to some of the VICs listed in the first
	16 entries in the EDID.*/
	for (i = 0; i < MAX_NUM_OF_HDMI_VSDB_3D_MULTI_SUPPORT; ++i) {

		if ((ext->multi_3d_support == STEREO_3D_MULTI_ALL_FORMATS) ||
			(video_fields.stereo_3d_mask & (1 << i))) {

			ext->cached_multi_3d_support[i] =
				video_fields.stereo_3D_all_support;

		} else {
			ext->cached_multi_3d_support[i].FRAME_PACKING =
				false;

			ext->cached_multi_3d_support[i].SIDE_BY_SIDE_HALF =
				false;

			ext->cached_multi_3d_support[i].TOP_AND_BOTTOM =
				false;
		}
	}
}

static bool add_cea861b_audio_modes(
	struct edid_ext_cea *ext,
	struct dcs_cea_audio_mode_list *audio_list)
{
	bool ret = false;
	uint8_t offset = 0;
	struct short_descr_info descr = { 0 };

	/* loop through all short audio descriptors */
	while (find_short_descr(ext->data,
			offset,
			CEA_861B_DATA_BLOCK_TAGCODE_SHORT_AUDIO_DESCRIPTOR,
			CEA_861B_DATA_BLOCK_EXT_TAGCODE_NONE,
			&descr)) {
		uint8_t index;
		const uint8_t *sad = &ext->data->data_block[descr.offset];

		for (index = 0; index < descr.len/3; ++index) {
			struct cea_audio_mode audio_mode = { 0 };

			audio_mode.format_code = sad[0]>>3;
			audio_mode.channel_count = (sad[0] & 0x7) + 1;
			audio_mode.sample_rate = sad[1] & 0x7F;
			audio_mode.sample_size = sad[2];
			sad += 3;
			ret = true;
			if (audio_list)
				dal_dcs_cea_audio_mode_list_append(
					audio_list, &audio_mode);
		}

		/* start next search at the end of this descriptor block */
		offset = descr.offset + descr.len;
	}
	return ret;
}

static bool get_cea_audio_modes(
	struct edid_base *edid,
	struct dcs_cea_audio_mode_list *audio_list)
{
	struct edid_ext_cea *ext = FROM_EDID(edid);

	return add_cea861b_audio_modes(ext, audio_list);
}

static uint8_t get_edid_extension_tag(struct edid_base *edid)
{
	struct edid_ext_cea *ext = FROM_EDID(edid);

	return ext->data->extension_tag;
}

static const uint8_t *get_raw_data(struct edid_base *edid)
{
	struct edid_ext_cea *ext = FROM_EDID(edid);

	return (const uint8_t *)ext->data;
}

static const uint32_t get_raw_size(struct edid_base *edid)
{
	return sizeof(struct edid_data_cea861_ext);
}

static void destruct(struct edid_ext_cea *edid)
{

}

static void destroy(struct edid_base **edid)
{
	destruct(FROM_EDID(*edid));
	dal_free(FROM_EDID(*edid));
	*edid = NULL;
}

static const struct edid_funcs funcs = {
	.destroy = destroy,
	.get_display_tile_info = dal_edid_base_get_display_tile_info,
	.get_min_drr_fps = dal_edid_base_get_min_drr_fps,
	.get_drr_pixel_clk_khz = dal_edid_base_get_drr_pixel_clk_khz,
	.is_non_continuous_frequency =
		dal_edid_base_is_non_continuous_frequency,
	.get_stereo_3d_support = dal_edid_base_get_stereo_3d_support,
	.validate = dal_edid_base_validate,
	.get_version = dal_edid_base_get_version,
	.num_of_extension = dal_edid_base_num_of_extension,
	.get_edid_extension_tag = get_edid_extension_tag,
	.get_cea_audio_modes = get_cea_audio_modes,
	.get_cea861_support = get_cea861_support,
	.get_display_pixel_encoding = get_display_pixel_encoding,
	.get_display_color_depth = get_display_color_depth,
	.get_connector_type = get_connector_type,
	.get_screen_info = dal_edid_base_get_screen_info,
	.get_display_characteristics =
		dal_edid_base_get_display_characteristics,
	.get_monitor_range_limits = dal_edid_base_get_monitor_range_limits,
	.get_display_name = dal_edid_base_get_display_name,
	.get_vendor_product_id_info = dal_edid_base_get_vendor_product_id_info,
	.get_supported_mode_timing = get_supported_mode_timing,
	.get_cea_video_capability_data_block =
			get_cea_video_capability_data_block,
	.get_cea_colorimetry_data_block =
		get_cea_colorimetry_data_block,
	.get_cea_speaker_allocation_data_block =
		get_cea_speaker_allocation_data_block,
	.get_cea_vendor_specific_data_block =
		get_cea_vendor_specific_data_block,
	.get_raw_size = get_raw_size,
	.get_raw_data = get_raw_data,
};

static bool construct(
	struct edid_ext_cea *ext,
	struct edid_ext_cea_init_data *init_data)
{
	if (!init_data)
		return false;

	if (init_data->len == 0 ||
		init_data->buf == NULL ||
		init_data->edid_patch == NULL)
		return false;

	if (!dal_edid_ext_cea_is_cea_ext(init_data->len, init_data->buf))
		return false;

	if (!dal_edid_base_construct(&ext->edid, init_data->ts))
		return false;

	ext->data = (struct edid_data_cea861_ext *)init_data->buf;
	ext->edid_patch = init_data->edid_patch;

	ext->edid.funcs = &funcs;

	cache_stereo_3d_support_info(ext);

	return true;
}

struct edid_base *dal_edid_ext_cea_create(
	struct edid_ext_cea_init_data *init_data)
{
	struct edid_ext_cea *ext = NULL;

	ext = dal_alloc(sizeof(struct edid_ext_cea));

	if (!ext)
		return NULL;

	if (construct(ext, init_data))
		return &ext->edid;

	dal_free(ext);
	BREAK_TO_DEBUGGER();
	return NULL;
}
