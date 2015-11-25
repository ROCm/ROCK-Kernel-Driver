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

#ifndef __DAL_GRPH_OBJECT_ID_H__
#define __DAL_GRPH_OBJECT_ID_H__

/* Types of graphics objects */
enum object_type {
	OBJECT_TYPE_UNKNOWN  = 0,

	/* Direct ATOM BIOS translation */
	OBJECT_TYPE_GPU,
	OBJECT_TYPE_ENCODER,
	OBJECT_TYPE_CONNECTOR,
	OBJECT_TYPE_ROUTER,
	OBJECT_TYPE_GENERIC,

	/* Driver specific */
	OBJECT_TYPE_AUDIO,
	OBJECT_TYPE_CONTROLLER,
	OBJECT_TYPE_CLOCK_SOURCE,
	OBJECT_TYPE_ENGINE,

	OBJECT_TYPE_COUNT
};

/* Enumeration inside one type of graphics objects */
enum object_enum_id {
	ENUM_ID_UNKNOWN = 0,
	ENUM_ID_1,
	ENUM_ID_2,
	ENUM_ID_3,
	ENUM_ID_4,
	ENUM_ID_5,
	ENUM_ID_6,
	ENUM_ID_7,

	ENUM_ID_COUNT
};

/* Generic object ids */
enum generic_id {
	GENERIC_ID_UNKNOWN = 0,
	GENERIC_ID_MXM_OPM,
	GENERIC_ID_GLSYNC,
	GENERIC_ID_STEREO,

	GENERIC_ID_COUNT
};

/* Controller object ids */
enum controller_id {
	CONTROLLER_ID_UNDEFINED = 0,
	CONTROLLER_ID_D0,
	CONTROLLER_ID_D1,
	CONTROLLER_ID_D2,
	CONTROLLER_ID_D3,
	CONTROLLER_ID_D4,
	CONTROLLER_ID_D5,
	CONTROLLER_ID_UNDERLAY0,
	CONTROLLER_ID_MAX = CONTROLLER_ID_UNDERLAY0
};

#define IS_UNDERLAY_CONTROLLER(ctrlr_id) (ctrlr_id >= CONTROLLER_ID_UNDERLAY0)

/*
 * ClockSource object ids.
 * We maintain the order matching (more or less) ATOM BIOS
 * to improve optimized acquire
 */
enum clock_source_id {
	CLOCK_SOURCE_ID_UNDEFINED = 0,
	CLOCK_SOURCE_ID_PLL0,
	CLOCK_SOURCE_ID_PLL1,
	CLOCK_SOURCE_ID_PLL2,
	CLOCK_SOURCE_ID_EXTERNAL, /* ID (Phy) ref. clk. for DP */
	CLOCK_SOURCE_ID_DCPLL,
	CLOCK_SOURCE_ID_DFS,	/* DENTIST */
	CLOCK_SOURCE_ID_VCE,	/* VCE does not need a real PLL */
	CLOCK_SOURCE_ID_DP_DTO,	/* Used to distinguish between */
	/* programming pixel clock */
	/* and ID (Phy) clock */
};


/* Encoder object ids */
enum encoder_id {
	ENCODER_ID_UNKNOWN = 0,

	/* Radeon Class Display Hardware */
	ENCODER_ID_INTERNAL_LVDS,
	ENCODER_ID_INTERNAL_TMDS1,
	ENCODER_ID_INTERNAL_TMDS2,
	ENCODER_ID_INTERNAL_DAC1,
	ENCODER_ID_INTERNAL_DAC2,	/* TV/CV DAC */
	ENCODER_ID_INTERNAL_SDVOA,
	ENCODER_ID_INTERNAL_SDVOB,

	/* External Third Party Encoders */
	ENCODER_ID_EXTERNAL_SI170B,
	ENCODER_ID_EXTERNAL_CH7303,
	ENCODER_ID_EXTERNAL_CH7301,	/* 10 in decimal */
	ENCODER_ID_INTERNAL_DVO1,	/* Belongs to Radeon Display Hardware */
	ENCODER_ID_EXTERNAL_SDVOA,
	ENCODER_ID_EXTERNAL_SDVOB,
	ENCODER_ID_EXTERNAL_TITFP513,
	ENCODER_ID_INTERNAL_LVTM1,	/* not used for Radeon */
	ENCODER_ID_EXTERNAL_VT1623,
	ENCODER_ID_EXTERNAL_SI1930,	/* HDMI */
	ENCODER_ID_INTERNAL_HDMI,

	/* Kaledisope (KLDSCP) Class Display Hardware */
	ENCODER_ID_INTERNAL_KLDSCP_TMDS1,
	ENCODER_ID_INTERNAL_KLDSCP_DVO1,
	ENCODER_ID_INTERNAL_KLDSCP_DAC1,
	ENCODER_ID_INTERNAL_KLDSCP_DAC2,	/* Shared with CV/TV and CRT */
	/* External TMDS (dual link) */
	ENCODER_ID_EXTERNAL_SI178,
	ENCODER_ID_EXTERNAL_MVPU_FPGA,	/* MVPU FPGA chip */
	ENCODER_ID_INTERNAL_DDI,
	ENCODER_ID_EXTERNAL_VT1625,
	ENCODER_ID_EXTERNAL_SI1932,
	ENCODER_ID_EXTERNAL_AN9801,	/* External Display Port */
	ENCODER_ID_EXTERNAL_DP501,	/* External Display Port */
	ENCODER_ID_INTERNAL_UNIPHY,
	ENCODER_ID_INTERNAL_KLDSCP_LVTMA,
	ENCODER_ID_INTERNAL_UNIPHY1,
	ENCODER_ID_INTERNAL_UNIPHY2,
	ENCODER_ID_EXTERNAL_NUTMEG,
	ENCODER_ID_EXTERNAL_TRAVIS,

	ENCODER_ID_INTERNAL_WIRELESS,	/* Internal wireless display encoder */
	ENCODER_ID_INTERNAL_UNIPHY3,

	ENCODER_ID_EXTERNAL_GENERIC_DVO = 0xFF
};


/* Connector object ids */
enum connector_id {
	CONNECTOR_ID_UNKNOWN = 0,
	CONNECTOR_ID_SINGLE_LINK_DVII,
	CONNECTOR_ID_DUAL_LINK_DVII,
	CONNECTOR_ID_SINGLE_LINK_DVID,
	CONNECTOR_ID_DUAL_LINK_DVID,
	CONNECTOR_ID_VGA,
	CONNECTOR_ID_HDMI_TYPE_A,
	CONNECTOR_ID_NOT_USED,
	CONNECTOR_ID_LVDS,
	CONNECTOR_ID_PCIE,
	CONNECTOR_ID_HARDCODE_DVI,
	CONNECTOR_ID_DISPLAY_PORT,
	CONNECTOR_ID_EDP,
	CONNECTOR_ID_MXM,
	CONNECTOR_ID_WIRELESS,		/* wireless display pseudo-connector */
	CONNECTOR_ID_MIRACAST,		/* used for VCE encode display path
					 * for Miracast */

	CONNECTOR_ID_COUNT
};


/* Audio object ids */
enum audio_id {
	AUDIO_ID_UNKNOWN = 0,
	AUDIO_ID_INTERNAL_AZALIA
};


/* Engine object ids */
enum engine_id {
	ENGINE_ID_DIGA,
	ENGINE_ID_DIGB,
	ENGINE_ID_DIGC,
	ENGINE_ID_DIGD,
	ENGINE_ID_DIGE,
	ENGINE_ID_DIGF,
	ENGINE_ID_DIGG,
	ENGINE_ID_DVO,
	ENGINE_ID_DACA,
	ENGINE_ID_DACB,
	ENGINE_ID_VCE,	/* wireless display pseudo-encoder */

	ENGINE_ID_COUNT,
	ENGINE_ID_UNKNOWN = (-1L)
};

union supported_stream_engines {
	struct {
		uint32_t ENGINE_ID_DIGA:1;
		uint32_t ENGINE_ID_DIGB:1;
		uint32_t ENGINE_ID_DIGC:1;
		uint32_t ENGINE_ID_DIGD:1;
		uint32_t ENGINE_ID_DIGE:1;
		uint32_t ENGINE_ID_DIGF:1;
		uint32_t ENGINE_ID_DIGG:1;
		uint32_t ENGINE_ID_DVO:1;
		uint32_t ENGINE_ID_DACA:1;
		uint32_t ENGINE_ID_DACB:1;
		uint32_t ENGINE_ID_VCE:1;
	} engine;
	uint32_t u_all;
};


/*
 *****************************************************************************
 * graphics_object_id struct
 *
 * graphics_object_id is a very simple struct wrapping 32bit Graphics
 * Object identication
 *
 * This struct should stay very simple
 *  No dependencies at all (no includes)
 *  No debug messages or asserts
 *  No #ifndef and preprocessor directives
 *  No grow in space (no more data member)
 *****************************************************************************
 */

struct graphics_object_id {
	uint32_t  id:8;
	uint32_t  enum_id:4;
	uint32_t  type:4;
	uint32_t  reserved:16; /* for padding. total size should be u32 */
};

/* some simple functions for convenient graphics_object_id handle */

static inline struct graphics_object_id dal_graphics_object_id_init(
	uint32_t id,
	enum object_enum_id enum_id,
	enum object_type type)
{
	struct graphics_object_id result = {
		id, enum_id, type, 0
	};

	return result;
}

bool dal_graphics_object_id_is_valid(
	struct graphics_object_id id);
bool dal_graphics_object_id_is_equal(
	struct graphics_object_id id1,
	struct graphics_object_id id2);
uint32_t dal_graphics_object_id_to_uint(
	struct graphics_object_id id);


enum controller_id dal_graphics_object_id_get_controller_id(
	struct graphics_object_id id);
enum clock_source_id dal_graphics_object_id_get_clock_source_id(
	struct graphics_object_id id);
enum encoder_id dal_graphics_object_id_get_encoder_id(
	struct graphics_object_id id);
enum connector_id dal_graphics_object_id_get_connector_id(
	struct graphics_object_id id);
enum audio_id dal_graphics_object_id_get_audio_id(
	struct graphics_object_id id);
enum engine_id dal_graphics_object_id_get_engine_id(
	struct graphics_object_id id);
#endif
