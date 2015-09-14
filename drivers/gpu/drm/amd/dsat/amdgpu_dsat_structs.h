/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright
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
 * Authors:
 *
 * Description:
 *  Public header file for AMDGPU DSAT library, struct defns
 *
 */

#ifndef __AMDGPU_DSAT_STRUCTS__H
#define __AMDGPU_DSAT_STRUCTS__H
#if !defined(BUILD_DC_CORE)
#define DSAT_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED		0x00000001
#define DSAT_DISPLAY_DISPLAYINFO_DISPLAYMAPPED			0x00000002
#define DSAT_DISPLAY_DISPLAYINFO_NONLOCAL			0x00000004
#define DSAT_DISPLAY_DISPLAYINFO_FORCIBLESUPPORTED		0x00000008
#define DSAT_DISPLAY_DISPLAYINFO_GENLOCKSUPPORTED		0x00000010
#define DSAT_DISPLAY_DISPLAYINFO_MULTIVPU_SUPPORTED		0x00000020
#define DSAT_DISPLAY_DISPLAYINFO_LDA_DISPLAY			0x00000040
#define DSAT_DISPLAY_DISPLAYINFO_MODETIMING_OVERRIDESSUPPORTED	0x00000080
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_SINGLE	0x00000100
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_CLONE		0x00000200
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_2VSTRETCH	0x00000400
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_2HSTRETCH	0x00000800
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_EXTENDED	0x00001000
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_NSTRETCH1GPU	0x00010000
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_NSTRETCHNGPU	0x00020000
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_RESERVED2	0x00040000
#define DSAT_DISPLAY_DISPLAYINFO_MANNER_SUPPORTED_RESERVED3	0x00080000
#define DSAT_DISPLAY_DISPLAYINFO_SHOWTYPE_PROJECTOR		0x00100000

#define DSAT_MAX_REGISTRY_PATH 256
enum dsat_cmd_errors {
	DSAT_CMD_OK,
	DSAT_CMD_NOT_IMPLEMENTED,
	DSAT_CMD_ERROR,
	DSAT_CMD_DATA_ERROR,
	DSAT_CMD_WRONG_BUFFER_SIZE,
	DSAT_CMD_DISABLED
};

enum dsat_ioctl_commands {
	/* @brief: Used to test DSAT bridge functionality
	 * @param[in]:dsat_test_in
	 * @param[out]:dsat_test_out
	 */
	DSAT_CMD_TEST,
	/* @brief: Get logger buffer size
	 * @param[in]:
	 * @param[out]:uint32_t buffer_size
	 */
	DSAT_CMD_LOGGER_GET_BUFFER_SIZE,
	/* @brief: Set logger buffer size
	 * @param[in]:uint32_t buffer_size
	 * @param[out]:uint32_t new_buffer_size
	 */
	DSAT_CMD_LOGGER_SET_BUFFER_SIZE,
	/* @brief: Get logger flags
	 * @param[in]:
	 * @param[out]:uint32_t flags
	 */
	DSAT_CMD_LOGGER_GET_FLAGS,
	/* @brief: Set logger flags
	 * @param[in]:uint32_t flags
	 * @param[out]:
	 */
	DSAT_CMD_LOGGER_SET_FLAGS,
	/* @brief: Get logger mask
	 * @param[in]:uint32_t mask_index
	 * @param[out]:uint32_t mask
	 */
	DSAT_CMD_LOGGER_GET_MASK,
	/* @brief: Set logger mask
	 * @param[in]:struct dsat_logger_request
	 * @param[out]:uint32_t mask
	 */
	DSAT_CMD_LOGGER_SET_MASK,
	/* @brief: Unset logger mask
	 * @param[in]:struct dsat_logger_request
	 * @param[out]:
	 */
	DSAT_CMD_LOGGER_UNSET_MASK,
	/* @brief: get logger masks
	 * @param[in]:struct dsat_logger_request
	 * @param[out]:uint32_t mask
	 */
	DSAT_CMD_LOGGER_GET_MASKS,
	/* @brief: Set logger masks
	 * @param[in]:struct dsat_logger_request
	 * @param[out]:
	 */
	DSAT_CMD_LOGGER_SET_MASKS,
	/* @brief: Get logger majors info
	 * @param[in]:uint32_t major_index
	 * @param[out]:void *
	 */
	DSAT_CMD_LOGGER_ENUM_MAJOR_INFO,
	/* @brief: Get logger minors info
	 * @param[in]:struct dsat_logger_request
	 * @param[out]:void *
	 */
	DSAT_CMD_LOGGER_ENUM_MINOR_INFO,
	/* @brief: Read driver log
	 * @param[in]:
	 * @param[out]:char *
	 */
	DSAT_CMD_LOGGER_READ,
	/* @brief: Read HW Reg
	 * @comment: set DDSAT_ALLOW_REG_ACCESS in makefile
	 * to enable this functionality in developers build
	 * @param[in]:dsat_hw_rw_request
	 * @param[out]:uint32_t reg_value
	 */
	DSAT_CMD_READ_HW_REG,
	/* @brief: Write HW Reg
	 * @comment: set DDSAT_ALLOW_REG_ACCESS in makefile
	 * to enable this functionality in developers build
	 * @param[in]:dsat_hw_rw_request
	 * @param[out]:
	 */
	DSAT_CMD_WRITE_HW_REG,
	/* @brief: Get adapters count
	 * @param[in]:
	 * @param[out]:uint32_t count
	 */
	DSAT_CMD_ADAPTERS_GET_COUNT,
	/* @brief: Get adapters info
	 * @param[in]:
	 * @param[out]:struct dsat_adapter_info[adapter_count]
	 */
	DSAT_CMD_ADAPTERS_GET_INFO,
	/* @brief: get adapter caps
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_ADAPTER_GET_CAPS,
	/* @brief: Get display EDID
	 * @param[in]:
	 * @param[out]:uint8_t *edid_data
	 */
	DSAT_CMD_GET_EDID,
	/* @brief: Override display EDID
	 * @param[in]:struct dsat_display_edid_data
	 * @param[out]:
	 */
	DSAT_CMD_OVERRIDE_EDID,
	/* @brief: Get display mode
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_GET_DISPLAY_MODE,
	/* @brief: Set display mode
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_SET_DISPLAY_MODE,
	/* @brief: Get display adjustment
	 * @comment: not implemented
	 * @param[in]:struct dsat_adjustment_data
	 * @param[out]:
	 */
	DSAT_CMD_DAL_ADJUSTMENT,
	/* @brief: Get display adjustment info
	 * @param[in]:struct dsat_adjustment_data
	 * @param[out]:struct adjustment_info adjust_info
	 */
	DSAT_CMD_GET_ADJUSTMENT_INFO,
	/* @brief: Get displays count
	 * @param[in]:
	 * @param[out]:uint32_t count
	 */
	DSAT_CMD_DISPLAYS_GET_COUNT,
	/* @brief: get displays info
	 * @param[in]:
	 * @param[out]:struct dsat_display_info[display_count]
	 */
	DSAT_CMD_DISPLAYS_GET_INFO,
	/* @brief: get display caps
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_DISPLAY_GET_CAPS,
	/* @brief: Get display config
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_DISPLAY_GET_DEVICE_CONFIG,
	/* @brief: get display DDC info
	 * @comment: not implemented
	 * @param[in]:
	 * @param[out]:
	 */
	DSAT_CMD_DISPLAY_GET_DDC_INFO,
	/* @brief: get display monitor mode timing count
	 * @param[in]:
	 * @param[out]:uint32_t count
	 */
	DSAT_CMD_DISPLAY_MODE_TIMING_GET_COUNT,
	/* @brief: get display monitor mode timing list
	 * @param[in]:
	 * @param[out]:mode timing list
	 */
	DSAT_CMD_DISPLAY_MODE_TIMINF_GET_LIST,
};

/* Ensure total struct is 8-byte aligned. */
#pragma pack(8)

struct amdgpu_dsat_input_context {
	enum dsat_ioctl_commands cmd;
	uint32_t adapter_index;
	uint32_t display_index;
	uint32_t in_data_size;
	uint8_t data[1];
	/*
	 * ------------------------
	 * | Additional data here |
	 * ------------------------
	 */
};

struct amdgpu_dsat_output_context {
	uint32_t data_size;
	uint32_t cmd_error;
	uint8_t data[1];
	/*
	 * ------------------------
	 * | Additional data here |
	 * ------------------------
	 */
};

/* structs for DSAT_CMD_TEST */
struct dsat_test_in {
	uint32_t value1;
	uint32_t value2;
	uint32_t value3;
};

struct dsat_test_out {
	uint64_t value4;
	uint32_t value1;
	uint32_t value2;
	uint32_t value3;
};

struct dsat_hw_rw_request {
	uint32_t address;
	uint32_t value;
};

/* structs for DSAT_CMD_GET_EDID_DATA and DSAT_CMD_OVERRIDE_EDID */
struct dsat_display_edid_data {
	uint32_t data_size;
	uint8_t data[DSAT_MAX_REGISTRY_PATH];
};

enum dsat_dal_adjustment_id {
	DSAT_DAL_ADJ_ID_SATURATION,
	DSAT_DAL_ADJ_ID_BACKLIGHT,
	DSAT_DAL_ADJ_ID_BIT_DEPTH_REDUCTION,
	DSAT_DAL_ADJ_ID_UNDERSCAN
};

enum dsat_dal_adjustment_cmd {
	DSAT_DAL_ADJ_SET, DSAT_DAL_ADJ_GET
};

struct dsat_adjustment_data {
	enum dsat_dal_adjustment_cmd cmd;
	enum dsat_dal_adjustment_id id;
	int32_t value;
};

/* structs for DSAT general use */
struct dsat_logger_request {
	uint32_t major_index;
	uint32_t minor_index;
	uint32_t mask;
};

union dsat_logger_flags {
	struct {
		uint32_t ENABLE_CONSOLE:1; /* Print to console */
		uint32_t ENABLE_BUFFER:1; /* Print to buffer */
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};
#define DSAT_MAX_MAJOR_NAME_LEN 32
#define DSAT_MAX_MINOR_NAME_LEN 32

struct dsat_logger_major_info {
	uint32_t major;
	char major_name[DSAT_MAX_MAJOR_NAME_LEN];
};

struct dsat_logger_minor_info {
	uint32_t minor;
	char minor_name[DSAT_MAX_MINOR_NAME_LEN];
};

struct dsat_adapter_info {
	uint32_t bus_number;
	uint32_t device_number;
	uint32_t function_number;
	uint32_t chip_family;
	uint32_t chip_id;
	uint32_t pci_revision_id;
	uint32_t hw_internal_rev;
	uint32_t vram_type;
	uint32_t vram_width;
	uint32_t vendor_id;
	uint32_t adapter_index;
	uint32_t present;
	uint32_t num_of_funct_controllers;
	uint32_t num_of_controllers;
	uint32_t num_of_connectors;
	uint32_t num_of_underlays;
};

struct dsat_display_info {
	uint32_t display_adapterIndex;
	uint32_t display_index;
	uint8_t display_name[DSAT_MAX_REGISTRY_PATH];
	uint8_t display_manufacturer_name[DSAT_MAX_REGISTRY_PATH];
	uint32_t display_active_signal;
	uint32_t display_type;
	uint32_t display_output_type;
	uint32_t display_connector;
	uint32_t display_infoMask;
	uint32_t display_info_value;
};


/**
 * DSAT Timing standard used to calculate a timing for a mode
 */
enum dsat_timing_standard {
	DSAT_TIMING_STANDARD_UNDEFINED,
	DSAT_TIMING_STANDARD_DMT,
	DSAT_TIMING_STANDARD_GTF,
	DSAT_TIMING_STANDARD_CVT,
	DSAT_TIMING_STANDARD_CVT_RB,
	DSAT_TIMING_STANDARD_CEA770,
	DSAT_TIMING_STANDARD_CEA861,
	DSAT_TIMING_STANDARD_HDMI,
	DSAT_TIMING_STANDARD_TV_NTSC,
	DSAT_TIMING_STANDARD_TV_NTSC_J,
	DSAT_TIMING_STANDARD_TV_PAL,
	DSAT_TIMING_STANDARD_TV_PAL_M,
	DSAT_TIMING_STANDARD_TV_PAL_CN,
	DSAT_TIMING_STANDARD_TV_SECAM,
	DSAT_TIMING_STANDARD_EXPLICIT,
	/*!< For explicit timings from EDID, VBIOS, etc.*/
	DSAT_TIMING_STANDARD_USER_OVERRIDE,
	/*!< For mode timing override by user*/
	DSAT_TIMING_STANDARD_MAX
};

enum dsat_aspect_ratio {
	DSAT_ASPECT_RATIO_NO_DATA,
	DSAT_ASPECT_RATIO_4_3,
	DSAT_ASPECT_RATIO_16_9,
	DSAT_ASPECT_RATIO_FUTURE
};

enum dsat_display_color_depth {
	DSAT_DISPLAY_COLOR_DEPTH_UNDEFINED,
	DSAT_DISPLAY_COLOR_DEPTH_666,
	DSAT_DISPLAY_COLOR_DEPTH_888,
	DSAT_DISPLAY_COLOR_DEPTH_101010,
	DSAT_DISPLAY_COLOR_DEPTH_121212,
	DSAT_DISPLAY_COLOR_DEPTH_141414,
	DSAT_DISPLAY_COLOR_DEPTH_161616
};

enum dsat_pixel_encoding {
	DSAT_PIXEL_ENCODING_UNDEFINED,
	DSAT_PIXEL_ENCODING_RGB,
	DSAT_PIXEL_ENCODING_YCBCR422,
	DSAT_PIXEL_ENCODING_YCBCR444
};

enum dsat_timing_3d_format {
	DSAT_TIMING_3D_FORMAT_NONE,
	DSAT_TIMING_3D_FORMAT_FRAME_ALTERNATE, /* No stereosync at all*/
	DSAT_, /* Inband Frame Alternate (DVI/DP)*/
	DSAT_TIMING_3D_FORMAT_DP_HDMI_INBAND_FA, /* Inband FA to HDMI Frame Pack*/
	/* for active DP-HDMI dongle*/
	DSAT_TIMING_3D_FORMAT_SIDEBAND_FA, /* Sideband Frame Alternate (eDP)*/
	DSAT_TIMING_3D_FORMAT_HW_FRAME_PACKING,
	DSAT_TIMING_3D_FORMAT_SW_FRAME_PACKING,
	DSAT_TIMING_3D_FORMAT_ROW_INTERLEAVE,
	DSAT_TIMING_3D_FORMAT_COLUMN_INTERLEAVE,
	DSAT_TIMING_3D_FORMAT_PIXEL_INTERLEAVE,
	DSAT_TIMING_3D_FORMAT_SIDE_BY_SIDE,
	DSAT_TIMING_3D_FORMAT_TOP_AND_BOTTOM,
	DSAT_TIMING_3D_FORMAT_SBS_SW_PACKED,
	/* Side-by-side, packed by application/driver into 2D frame*/
	DSAT_TIMING_3D_FORMAT_TB_SW_PACKED,
	/* Top-and-bottom, packed by application/driver into 2D frame*/

	DSAT_TIMING_3D_FORMAT_MAX,
};

enum dsat_timing_source {
	DSAT_TIMING_SOURCE_UNDEFINED,

/* explicitly specifed by user, most important*/
	DSAT_TIMING_SOURCE_USER_FORCED,
	DSAT_TIMING_SOURCE_USER_OVERRIDE,
	DSAT_TIMING_SOURCE_CUSTOM,
	DSAT_TIMING_SOURCE_DALINTERFACE_EXPLICIT,

/* explicitly specified by the display device, more important*/
	DSAT_TIMING_SOURCE_EDID_CEA_SVD_3D,
	DSAT_TIMING_SOURCE_EDID_DETAILED,
	DSAT_TIMING_SOURCE_EDID_ESTABLISHED,
	DSAT_TIMING_SOURCE_EDID_STANDARD,
	DSAT_TIMING_SOURCE_EDID_CEA_SVD,
	DSAT_TIMING_SOURCE_EDID_CVT_3BYTE,
	DSAT_TIMING_SOURCE_EDID_4BYTE,
	DSAT_TIMING_SOURCE_VBIOS,
	DSAT_TIMING_SOURCE_CV,
	DSAT_TIMING_SOURCE_TV,
	DSAT_TIMING_SOURCE_HDMI_VIC,

/* implicitly specified by display device, still safe but less important*/
	DSAT_TIMING_SOURCE_DEFAULT,

/* only used for custom base modes */
	DSAT_TIMING_SOURCE_CUSTOM_BASE,

/* these timing might not work, least important*/
	DSAT_TIMING_SOURCE_RANGELIMIT,
	DSAT_TIMING_SOURCE_OS_FORCED,
	DSAT_TIMING_SOURCE_DALINTERFACE_IMPLICIT,

/* only used by default mode list*/
	DSAT_TIMING_SOURCE_BASICMODE,
};

enum dsat_timing_support_method {
	DSAT_TIMING_SUPPORT_METHOD_UNDEFINED,
	DSAT_TIMING_SUPPORT_METHOD_EXPLICIT,
	DSAT_TIMING_SUPPORT_METHOD_IMPLICIT,
	DSAT_TIMING_SUPPORT_METHOD_NATIVE
};

struct dsat_mode_flags {
	/* note: part of refresh rate flag*/
	uint32_t INTERLACE:1;
	/* native display timing*/
	uint32_t NATIVE:1;
	/* preferred is the recommended mode, one per display */
	uint32_t PREFERRED:1;
	/* true if this mode should use reduced blanking timings
	 *_not_ related to the Reduced Blanking adjustment*/
	uint32_t REDUCED_BLANKING:1;
	/* note: part of refreshrate flag*/
	uint32_t VIDEO_OPTIMIZED_RATE:1;
	/* should be reported to upper layers as mode_flags*/
	uint32_t PACKED_PIXEL_FORMAT:1;
	/*< preferred view*/
	uint32_t PREFERRED_VIEW:1;
	/* this timing should be used only in tiled mode*/
	uint32_t TILED_MODE:1;
};

struct dsat_mode_info {
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t field_rate;
	/* Vertical refresh rate for progressive modes.
	 * Field rate for interlaced modes.*/

	enum dsat_timing_standard timing_standard;
	enum dsat_timing_source timing_source;
	struct dsat_mode_flags flags;
};

struct dsat_ts_timing_flags {
	uint32_t INTERLACE:1;
	uint32_t DOUBLESCAN:1;
	uint32_t PIXEL_REPETITION:4; /* values 1 to 10 supported*/
	uint32_t HSYNC_POSITIVE_POLARITY:1; /* when set to 1,
	it is positive polarity --reversed with dal1 or video bios define*/
	uint32_t VSYNC_POSITIVE_POLARITY:1; /* when set to 1,
	it is positive polarity --reversed with dal1 or video bios define*/
	uint32_t EXCLUSIVE_3D:1; /* if this bit set,
	timing can be driven in 3D format only
	and there is no corresponding 2D timing*/
	uint32_t RIGHT_EYE_3D_POLARITY:1; /* 1 - means right eye polarity
					(right eye = '1', left eye = '0') */
	uint32_t SUB_SAMPLE_3D:1; /* 1 - means left/right  images subsampled
	when mixed into 3D image. 0 - means summation (3D timing is doubled)*/
	uint32_t USE_IN_3D_VIEW_ONLY:1; /* Do not use this timing in 2D View,
	because corresponding 2D timing also present in the list*/
	uint32_t STEREO_3D_PREFERENCE:1; /* Means this is 2D timing
	and we want to match priority of corresponding 3D timing*/
	uint32_t YONLY:1;

};

/* TODO to be reworked: similar structures in timing generator
 *	and hw sequence service*/
struct dsat_crtc_timing {
	uint32_t h_total;
	uint32_t h_border_left;
	uint32_t h_addressable;
	uint32_t h_border_right;
	uint32_t h_front_porch;
	uint32_t h_sync_width;

	uint32_t v_total;
	uint32_t v_border_top;
	uint32_t v_addressable;
	uint32_t v_border_bottom;
	uint32_t v_front_porch;
	uint32_t v_sync_width;

	uint32_t pix_clk_khz;

	uint32_t vic;
	uint32_t hdmi_vic;
	enum dsat_timing_standard timing_standard;
	enum dsat_timing_3d_format timing_3d_format;
	enum dsat_display_color_depth display_color_depth;
	enum dsat_pixel_encoding pixel_encoding;

	struct dsat_ts_timing_flags flags;
};

/**
 * Combination structure to link a mode with a timing
 */

struct dsat_mode_timing {
	struct dsat_mode_info mode_info;
	struct dsat_crtc_timing crtc_timing;
};

#define DSAT_PIXEL_CLOCK_MULTIPLIER 1000


/* restore default packing  */
#pragma pack()
#endif
#endif /* __AMDGPU_DSAT_STRUCTS__H */
