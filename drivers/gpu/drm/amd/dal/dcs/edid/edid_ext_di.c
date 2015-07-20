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
#include "include/timing_service_interface.h"
#include "edid_base.h"
#include "edid_ext_di.h"

#define DI_EXT__NUM_OF_BYTES_IN_VERSION_REVISION_FIELDS 4
#define DI_EXT__NUM_OF_BYTES_IN_MIN_MAX_CROSSOVER_PIXELCLOCK_FIELDS 5
#define DI_EXT__NUM_OF_BYTES_IN_DISPLAYDEVICE_FIELDS 6
#define DI_EXT__NUM_OF_BYTES_IN_FRAMERATE_CONVERSION_FIELDS 5
#define DI_EXT__NUM_OF_BYTES_IN_COLOR_LUMMINANCE_DECODING_FIELDS 4
#define DI_EXT__NUM_OF_BYTES_IN_PACKETIZED_SUPPORT_FIELDS 16
#define DI_EXT__NUM_OF_BYTES_IN_UNUSED_BYTES_FIELDS 17
#define DI_EXT__NUM_OF_BYTES_IN_AUDIO_SUPPORT_FIELDS 9
#define DI_EXT__NUM_OF_BYTES_IN_GAMMA_FIELDS 46

enum di_ext_interface_type {
	DI_EXT_INTERFACE_TYPE_ANALOG = 0x00,
	DI_EXT_INTERFACE_TYPE_DIGITAL = 0x01,
	DI_EXT_INTERFACE_TYPE_DVI_SL = 0x02,
	DI_EXT_INTERFACE_TYPE_DVI_DL_HR = 0x03,
	DI_EXT_INTERFACE_TYPE_DVI_DL_HC = 0x04,
	DI_EXT_INTERFACE_TYPE_DVI_CE = 0x05,
	DI_EXT_INTERFACE_TYPE_PLUG_DISPLAY = 0x06,
	DI_EXT_INTERFACE_TYPE_DFP = 0x07,
	DI_EXT_INTERFACE_TYPE_LDI_SL = 0x08,
	DI_EXT_INTERFACE_TYPE_LDI_DL = 0x09,
	DI_EXT_INTERFACE_TYPE_LDI_CE = 0x0A
};

enum di_ext_data_format {
	DI_EXT_DATA_FORMAT_ANALOG = 0x00,
	DI_EXT_DATA_FORMAT_8BIT_RGB = 0x15,
	DI_EXT_DATA_FORMAT_12BIT_RGB = 0x19,
	DI_EXT_DATA_FORMAT_24BIT_RGB_SL = 0x24,
	DI_EXT_DATA_FORMAT_48BIT_RGB_DL_HR = 0x48,
	DI_EXT_DATA_FORMAT_48BIT_RGB_DL_HC = 0x49
};

struct bgr_color_depth {
	uint8_t blue;
	uint8_t green;
	uint8_t red;
};

struct ycbcr_color_depth {
	uint8_t cb;
	uint8_t y;
	uint8_t cr;
};

struct di_ext_monitor_color_depths {
	uint8_t dithering;
	struct bgr_color_depth bgr_color_depth;
	struct ycbcr_color_depth ycrcb_color_depth;
};

struct edid_data_di_ext {
	uint8_t extension_tag;
	uint8_t version;

	/* digital interface,12 bytes*/
	struct {
		uint8_t standard;

		uint8_t version_revision
		[DI_EXT__NUM_OF_BYTES_IN_VERSION_REVISION_FIELDS];

		struct {
			uint8_t description;
			uint8_t data;
		} format;

		uint8_t min_max_crossover_pix_clk
		[DI_EXT__NUM_OF_BYTES_IN_MIN_MAX_CROSSOVER_PIXELCLOCK_FIELDS];

	} digital_interface;

	uint8_t display_device[DI_EXT__NUM_OF_BYTES_IN_DISPLAYDEVICE_FIELDS];
	/*displayCapability, 35 bytes*/
	struct {
		uint8_t misc_caps;

		uint8_t frame_rate_conversion
		[DI_EXT__NUM_OF_BYTES_IN_FRAMERATE_CONVERSION_FIELDS];

		uint8_t display_scan_orientation;

		uint8_t color_lumminance_decoding
		[DI_EXT__NUM_OF_BYTES_IN_COLOR_LUMMINANCE_DECODING_FIELDS];

		struct di_ext_monitor_color_depths monitor_color_depth;

		uint8_t aspect_ratio_conversion;

		uint8_t packetized_support
		[DI_EXT__NUM_OF_BYTES_IN_PACKETIZED_SUPPORT_FIELDS];

	} display_caps;

	uint8_t used_bytes[DI_EXT__NUM_OF_BYTES_IN_UNUSED_BYTES_FIELDS];

	uint8_t audio_support[DI_EXT__NUM_OF_BYTES_IN_AUDIO_SUPPORT_FIELDS];

	uint8_t gamma[DI_EXT__NUM_OF_BYTES_IN_GAMMA_FIELDS];

	uint8_t checksum;
};

struct edid_ext_di {
	struct edid_base edid;
	struct edid_data_di_ext *data;
};

#define FROM_EDID(e) (container_of((e), struct edid_ext_di, edid))

static const uint8_t *get_raw_data(struct edid_base *edid)
{
	struct edid_ext_di *ext = FROM_EDID(edid);

	return (const uint8_t *)ext->data;
}

static const uint32_t get_raw_size(struct edid_base *edid)
{
	return sizeof(struct edid_data_di_ext);
}

bool dal_edid_ext_di_is_di_ext(uint32_t len, const uint8_t *buf)
{
	const struct edid_data_di_ext *data;

	if (len < sizeof(struct edid_data_di_ext))
		return false; /* di extension is 128 byte in length*/

	data = (const struct edid_data_di_ext *)buf;

	if (EDID_EXTENSION_TAG_DI_EXT != data->extension_tag)
		return false;/* Tag says it's not Di ext*/

	return true;
}

static bool get_display_color_depth(
	struct edid_base *edid,
	struct display_color_depth_support *color_depth)
{
	bool ret = false;
	const struct bgr_color_depth *depth;
	struct edid_ext_di *ext = FROM_EDID(edid);

	ASSERT(color_depth != NULL);

	if ((ext->data->digital_interface.standard !=
		DI_EXT_INTERFACE_TYPE_DVI_DL_HC) ||
		(ext->data->digital_interface.format.data !=
			DI_EXT_DATA_FORMAT_48BIT_RGB_DL_HC))
		return false;

	depth = &ext->data->display_caps.monitor_color_depth.bgr_color_depth;

	/* We support only identical depth for all colors*/
	if ((depth->blue != depth->green) || (depth->blue != depth->red))
		return false;

	switch (depth->blue) {
	case 10:
		color_depth->mask |= COLOR_DEPTH_INDEX_101010;
		ret = true;
		break;

	case 12:
		color_depth->mask |= COLOR_DEPTH_INDEX_121212;
		ret = true;
		break;

	case 14:
		color_depth->mask |= COLOR_DEPTH_INDEX_141414;
		ret = true;
		break;

	case 16:
		color_depth->mask |= COLOR_DEPTH_INDEX_161616;
		ret = true;
		break;

	default:
		break;
	}

	return ret;
}

static bool get_connector_type(
	struct edid_base *edid,
	enum dcs_edid_connector_type *type)
{
	struct edid_ext_di *ext = FROM_EDID(edid);

	switch (ext->data->digital_interface.standard) {
	case DI_EXT_INTERFACE_TYPE_DVI_SL:
	case DI_EXT_INTERFACE_TYPE_DVI_DL_HR:
	case DI_EXT_INTERFACE_TYPE_DVI_DL_HC:
	case DI_EXT_INTERFACE_TYPE_DVI_CE:
		*type = EDID_CONNECTOR_DVI;
		return true;

	default:
		break;
	}

	return false;
}

static void destruct(struct edid_ext_di *edid)
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
	.get_edid_extension_tag = dal_edid_base_get_edid_extension_tag,
	.get_cea_audio_modes = dal_edid_base_get_cea_audio_modes,
	.get_cea861_support = dal_edid_base_get_cea861_support,
	.get_display_pixel_encoding = dal_edid_base_get_display_pixel_encoding,
	.get_display_color_depth = get_display_color_depth,
	.get_connector_type = get_connector_type,
	.get_screen_info = dal_edid_base_get_screen_info,
	.get_display_characteristics =
		dal_edid_base_get_display_characteristics,
	.get_monitor_range_limits = dal_edid_base_get_monitor_range_limits,
	.get_display_name = dal_edid_base_get_display_name,
	.get_vendor_product_id_info = dal_edid_base_get_vendor_product_id_info,
	.get_supported_mode_timing = dal_edid_base_get_supported_mode_timing,
	.get_cea_video_capability_data_block =
		dal_edid_base_get_cea_video_capability_data_block,
	.get_cea_colorimetry_data_block =
		dal_edid_base_get_cea_colorimetry_data_block,
	.get_cea_speaker_allocation_data_block =
		dal_edid_base_get_cea_speaker_allocation_data_block,
	.get_cea_vendor_specific_data_block =
		dal_edid_base_get_cea_vendor_specific_data_block,
	.get_raw_size = get_raw_size,
	.get_raw_data = get_raw_data,
};

static bool construct(
	struct edid_ext_di *ext,
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	if (len == 0 || buf == NULL)
		return false;

	if (!dal_edid_ext_di_is_di_ext(len, buf))
		return false;

	if (!dal_edid_base_construct(&ext->edid, ts))
		return false;

	ext->data = (struct edid_data_di_ext *)buf;

	ext->edid.funcs = &funcs;

	return true;
}

struct edid_base *dal_edid_ext_di_create(
	struct timing_service *ts,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_ext_di *ext = NULL;

	ext = dal_alloc(sizeof(struct edid_ext_di));

	if (!ext)
		return NULL;

	if (construct(ext, ts, len, buf))
		return &ext->edid;

	dal_free(ext);
	BREAK_TO_DEBUGGER();
	return NULL;
}

