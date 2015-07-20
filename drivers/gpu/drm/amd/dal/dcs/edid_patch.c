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

#include "include/vector.h"
#include "include/dcs_types.h"
#include "include/ddc_service_types.h"
#include "include/adapter_service_interface.h"

#include "edid_patch.h"
#include "edid/edid_base.h"
#include "edid/edid.h"
#include "edid/edid1x_data.h"

struct monitor_patch_list {
	struct vector vector;
	union dcs_monitor_patch_flags flags;
};

static bool monitor_patch_list_construct(struct monitor_patch_list *mpl)
{
	return dal_vector_construct(&mpl->vector, 64,
		sizeof(struct monitor_patch_info));
}

static void monitor_patch_list_destruct(struct monitor_patch_list *mpl)
{
	dal_vector_destruct(&mpl->vector);
}

static void monitor_patch_list_insert(
	struct monitor_patch_list *mpl,
	const struct monitor_patch_info *info)
{
	switch (info->type) {
	case MONITOR_PATCH_TYPE_ERROR_CHECKSUM:
		mpl->flags.flags.ERROR_CHECKSUM = true;
		break;
	case MONITOR_PATCH_TYPE_HDTV_WITH_PURE_DFP_EDID:
		mpl->flags.flags.HDTV_WITH_PURE_DFP_EDID = true;
		break;
	case MONITOR_PATCH_TYPE_DO_NOT_USE_DETAILED_TIMING:
		mpl->flags.flags.DO_NOT_USE_DETAILED_TIMING = true;
		break;
	case MONITOR_PATCH_TYPE_DO_NOT_USE_RANGE_LIMITATION:
		mpl->flags.flags.DO_NOT_USE_RANGE_LIMITATION = true;
		break;
	case MONITOR_PATCH_TYPE_EDID_EXTENTION_ERROR_CHECKSUM:
		mpl->flags.flags.EDID_EXTENTION_ERROR_CHECKSUM = true;
		break;
	case MONITOR_PATCH_TYPE_TURN_OFF_DISPLAY_BEFORE_MODE_CHANGE:
		mpl->flags.flags.TURN_OFF_DISPLAY_BEFORE_MODE_CHANGE = true;
		break;
	case MONITOR_PATCH_TYPE_RESTRICT_VESA_MODE_TIMING:
		mpl->flags.flags.RESTRICT_VESA_MODE_TIMING = true;
		break;
	case MONITOR_PATCH_TYPE_DO_NOT_USE_EDID_MAX_PIX_CLK:
		mpl->flags.flags.DO_NOT_USE_EDID_MAX_PIX_CLK = true;
		break;
	case MONITOR_PATCH_TYPE_VENDOR_0:
		mpl->flags.flags.VENDOR_0 = true;
		break;
	case MONITOR_PATCH_TYPE_RANDOM_CRT:
		mpl->flags.flags.RANDOM_CRT = true;
		break;
	case MONITOR_PATCH_TYPE_VENDOR_1:
		mpl->flags.flags.VENDOR_1 = true;
		break;
	case MONITOR_PATCH_TYPE_LIMIT_PANEL_SUPPORT_RGB_ONLY:
		mpl->flags.flags.LIMIT_PANEL_SUPPORT_RGB_ONLY = true;
		break;
	case MONITOR_PATCH_TYPE_PACKED_PIXEL_FORMAT:
		mpl->flags.flags.PACKED_PIXEL_FORMAT = true;
		break;
	case MONITOR_PATCH_TYPE_LARGE_PANEL:
		mpl->flags.flags.LARGE_PANEL = true;
		break;
	case MONITOR_PATCH_TYPE_STEREO_SUPPORT:
		mpl->flags.flags.STEREO_SUPPORT = true;
		break;
	case MONITOR_PATCH_TYPE_DUAL_EDID_PANEL:
		mpl->flags.flags.DUAL_EDID_PANEL = true;
		break;
	case MONITOR_PATCH_TYPE_IGNORE_19X12_STD_TIMING:
		mpl->flags.flags.IGNORE_19X12_STD_TIMING = true;
		break;
	case MONITOR_PATCH_TYPE_MULTIPLE_PACKED_TYPE:
		mpl->flags.flags.MULTIPLE_PACKED_TYPE = true;
		break;
	case MONITOR_PATCH_TYPE_RESET_TX_ON_DISPLAY_POWER_ON:
		mpl->flags.flags.RESET_TX_ON_DISPLAY_POWER_ON = true;
		break;
	case MONITOR_PATCH_TYPE_ALLOW_ONLY_CE_MODE:
		mpl->flags.flags.ALLOW_ONLY_CE_MODE = true;
		break;
	case MONITOR_PATCH_TYPE_RESTRICT_PROT_DUAL_LINK_DVI:
		mpl->flags.flags.RESTRICT_PROT_DUAL_LINK_DVI = true;
		break;
	case MONITOR_PATCH_TYPE_FORCE_LINK_RATE:
		mpl->flags.flags.FORCE_LINK_RATE = true;
		break;
	case MONITOR_PATCH_TYPE_DELAY_AFTER_DP_RECEIVER_POWER_UP:
		mpl->flags.flags.DELAY_AFTER_DP_RECEIVER_POWER_UP = true;
		break;
	case MONITOR_PATCH_TYPE_KEEP_DP_RECEIVER_POWERED:
		mpl->flags.flags.KEEP_DP_RECEIVER_POWERED = true;
		break;
	case MONITOR_PATCH_TYPE_DELAY_BEFORE_READ_EDID:
		mpl->flags.flags.DELAY_BEFORE_READ_EDID = true;
		break;
	case MONITOR_PATCH_TYPE_DELAY_AFTER_PIXEL_FORMAT_CHANGE:
		mpl->flags.flags.DELAY_AFTER_PIXEL_FORMAT_CHANGE = true;
		break;
	case MONITOR_PATCH_TYPE_INCREASE_DEFER_WRITE_RETRY_I2C_OVER_AUX:
		mpl->flags.flags.INCREASE_DEFER_WRITE_RETRY_I2C_OVER_AUX = true;
		break;
	case MONITOR_PATCH_TYPE_NO_DEFAULT_TIMINGS:
		mpl->flags.flags.NO_DEFAULT_TIMINGS = true;
		break;
	case MONITOR_PATCH_TYPE_ADD_CEA861_DETAILED_TIMING_VIC16:
		mpl->flags.flags.ADD_CEA861_DETAILED_TIMING_VIC16 = true;
		break;
	case MONITOR_PATCH_TYPE_ADD_CEA861_DETAILED_TIMING_VIC31:
		mpl->flags.flags.ADD_CEA861_DETAILED_TIMING_VIC31 = true;
		break;
	case MONITOR_PATCH_TYPE_DELAY_BEFORE_UNMUTE:
		mpl->flags.flags.DELAY_BEFORE_UNMUTE = true;
		break;
	case MONITOR_PATCH_TYPE_RETRY_LINK_TRAINING_ON_FAILURE:
		mpl->flags.flags.RETRY_LINK_TRAINING_ON_FAILURE = true;
		break;
	case MONITOR_PATCH_TYPE_ALLOW_AUX_WHEN_HPD_LOW:
		mpl->flags.flags.ALLOW_AUX_WHEN_HPD_LOW = true;
		break;
	case MONITOR_PATCH_TYPE_TILED_DISPLAY:
		mpl->flags.flags.TILED_DISPLAY = true;
		break;
	case MONITOR_PATCH_TYPE_DISABLE_PSR_ENTRY_ABORT:
		mpl->flags.flags.DISABLE_PSR_ENTRY_ABORT = true;
		break;
	case MONITOR_PATCH_TYPE_VID_STREAM_DIFFER_TO_SYNC:
		mpl->flags.flags.VID_STREAM_DIFFER_TO_SYNC = true;
		break;
	case MONITOR_PATCH_TYPE_DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS:
		mpl->flags.flags.DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS =
			true;
		break;
	default:
		break;
	}
	dal_vector_append(&mpl->vector, info);
}

static inline struct monitor_patch_info *monitor_patch_list_get_patch_info(
	struct monitor_patch_list *mpl,
	enum monitor_patch_type type)
{
	uint32_t i;

	for (i = 0; i < dal_vector_get_count(&mpl->vector); ++i) {
		struct monitor_patch_info *info =
			dal_vector_at_index(&mpl->vector, i);
		if (info->type == type)
			return info;
	}

	return NULL;
}

static inline uint32_t monitor_patch_list_size(struct monitor_patch_list *mpl)
{
	return dal_vector_get_count(&mpl->vector);
}

static struct monitor_patch_info *monitor_patch_list_get_patch_at(
	struct monitor_patch_list *mpl,
	uint32_t i)
{
	return dal_vector_at_index(&mpl->vector, i);
}

struct edid_patch {
	struct monitor_patch_list mpl;
	enum monitor_manufacturer_id manufacturer_id;
	enum monitor_product_id product_id;
	uint8_t extensions_num;
	uint8_t edid_version_major;
	uint8_t edid_version_minor;
	union dcs_monitor_patch_flags flags;
	enum dcs_packed_pixel_format packed_pixel_format;
};

static bool construct(struct edid_patch *ep, struct adapter_service *as)
{
	if (!as)
		return false;

	ep->packed_pixel_format =
		dal_adapter_service_get_feature_flags(as).
		bits.PACKED_PIXEL_FORMAT;

	return monitor_patch_list_construct(&ep->mpl);
}

struct edid_patch *dal_edid_patch_create(struct adapter_service *as)
{
	struct edid_patch *ep = dal_alloc(sizeof(struct edid_patch));

	if (!ep)
		return NULL;

	if (construct(ep, as))
		return ep;

	dal_free(ep);
	return NULL;
}

static void destruct(struct edid_patch *ep)
{
	monitor_patch_list_destruct(&ep->mpl);
}

void dal_edid_patch_destroy(struct edid_patch **ep)
{
	if (!ep || !*ep)
		return;
	destruct(*ep);
	dal_free(*ep);
	*ep = NULL;
}

static enum edid_tiled_display_type translate_tiled_display(
	enum monitor_manufacturer_id manufacturer_id,
	enum monitor_product_id product_id)
{
	if (manufacturer_id == MONITOR_MANUFACTURER_ID_14 &&
		product_id == MONITOR_PRODUCT_ID_37)
		return EDID_TILED_DISPLAY_1;

	if (manufacturer_id == MONITOR_MANUFACTURER_ID_20 &&
		product_id == MONITOR_PRODUCT_ID_42)
		return EDID_TILED_DISPLAY_2;

	return EDID_TILED_DISPLAY_NONE;
}

/*
 * dal_edid_patch_initialize
 *
 * Parses EDID manufacture/product and based on this, initialize the instance.
 * Actually binds the instance to this EDID.
 * Should be called before any other API.
 *
 */
bool dal_edid_patch_initialize(
	struct edid_patch *ep,
	const uint8_t *edid_buf,
	uint32_t edid_len)
{
	uint32_t entries_num = dal_monitor_tables_get_count();
	uint32_t i;

	if (edid_buf == NULL)
		return false;

	if (!dal_edid_get_version_raw(
		edid_buf,
		edid_len,
		&ep->edid_version_major,
		&ep->edid_version_minor))
		return false;

	if (ep->edid_version_major == 1) {
		const struct edid_data_v1x *edid_data =
			(const struct edid_data_v1x *) edid_buf;

		ep->manufacturer_id = (edid_data->vendor_id[1] << 8) +
			edid_data->vendor_id[0];
		ep->product_id = (edid_data->vendor_id[3] << 8) +
			edid_data->vendor_id[2];
		ep->extensions_num = edid_data->ext_blk_cnt;
	} else {
		return false;
	}

	/* Build patch list. Packed pixel format property will be cached,
	 * besides list entry */
	for (i = 0; i < entries_num; ++i) {
		const struct monitor_patch_info *entry =
			dal_monitor_tables_get_entry_at(i);

		if (entry == NULL)
			continue;

		if ((entry->manufacturer_id == ep->manufacturer_id &&
			(entry->product_id == ep->product_id ||
				entry->product_id ==
					MONITOR_PRODUCT_ID_0)) ||
			(entry->manufacturer_id ==
					MONITOR_MANUFACTURER_ID_0 &&
					entry->product_id ==
						MONITOR_PRODUCT_ID_0)) {

			struct monitor_patch_info info = *entry;

			if (info.type == MONITOR_PATCH_TYPE_TILED_DISPLAY)
				info.param =
					translate_tiled_display(
						entry->manufacturer_id,
						entry->product_id);

			/* Insert will never add patch with same type */
			monitor_patch_list_insert(&ep->mpl, &info);
		}
	}

	return true;
}

static void patch_header_error(uint8_t *buff)
{
	buff[0] = 0x00;
	buff[1] = 0xff;
	buff[2] = 0xff;
	buff[3] = 0xff;
	buff[4] = 0xff;
	buff[5] = 0xff;
	buff[6] = 0xff;
	buff[7] = 0x00;
}

static void patch_vendor1_workaround(
	struct edid_patch *ep,
	uint8_t *buff)
{
	/* Edid wrong with zero VBlank, regulate it according to
	 * CEA-861rCV2 specification.This monitor DO support below established
	 * timing with HDMI interface. But it doesn't report.
	 */
	buff[0x23] = 0xad;
	buff[0x24] = 0xcf;

	/* 1st detailed timing entry at extended block: Mode 1920 x 540 x 30,
	 * the ucVBlankingL8 should be 0x16 rather than 0x00
	 * according to specification. After correcting ucVBlankingL8, the
	 * checksum is right also!
	 */
	if (ep->extensions_num > 0 && buff[0xa0] == 0)
		buff[0xa0] = 0x16;
}

/*
 * patch_checksum_error
 *
 * Recalculates and writes back checksum of EDID block. It can be base block or
 * valid extension
 */
static void patch_checksum_error(
	struct edid_patch *ep,
	uint8_t *buff,
	uint32_t block_number)
{
	uint32_t length = EDID_VER_1_STDLEN;
	uint8_t checksum = 0;
	uint32_t i;

	/* blockNumber = extension index + 1 */
	if (block_number > ep->extensions_num)
		return;

	buff += block_number * length;

	for (i = 0; i < length - 1; ++i)
		checksum += buff[i];

	buff[length-1] = (uint8_t)(0x100 - checksum);
}

static void get_edid1xx_std_mode(
	struct edid_patch *ep,
	const struct standard_timing *std_timing,
	struct mode_info *mode_info)
{
	uint32_t v_active = 0;
	uint32_t h_active;
	uint32_t frequency;
	/* Unused Standard Timing data fields shall be set to 01h, 01h, as per
	 * specification */
	if (std_timing->h_addressable == 0x00 ||
		(std_timing->h_addressable == 0x01 &&
			std_timing->u.s_uchar == 0x01))
		return;

	h_active = (std_timing->h_addressable + 31) * 8;
	frequency = std_timing->u.ratio_and_refresh_rate.REFRESH_RATE + 60;

	switch (std_timing->u.ratio_and_refresh_rate.RATIO) {
	case RATIO_16_BY_10:
		if (ep->edid_version_major == 1 &&
			ep->edid_version_minor < 3) {
			/* as per spec EDID structures prior to version 1,
			 * revision 3 defined the bit (bits 7 & 6 at address
			 * 27h) combination of 0 0 to indicate a 1 : 1 aspect
			 * ratio.
			 */
			v_active = h_active;
		} else
			v_active = (h_active * 10) / 16;
		break;
	case RATIO_4_BY_3:
		v_active = (h_active * 3) / 4;
		break;

	case RATIO_5_BY_4:
		v_active = (h_active * 4) / 5;
		break;

	case RATIO_16_BY_9:
		v_active = (h_active * 9) / 16;
		break;

	default:
		break;
	}

	mode_info->pixel_width = h_active;
	mode_info->pixel_height = v_active;
	mode_info->field_rate = frequency;
	mode_info->timing_standard = TIMING_STANDARD_DMT;
	mode_info->timing_source = TIMING_SOURCE_EDID_STANDARD;
}

/*
 * patch_19x12_std_timing_error
 *
 * Removes 1900x1200@60 mode from standard modes section (does not check
 * detailed timings section for std modes).
 */
static void patch_19x12_std_timing_error(
	struct edid_patch *ep,
	uint8_t *buff)
{
	bool checksum_changed = false;
	uint32_t i;

	/* Parse standard timings in standard timing section */
	for (i = 0; i < NUM_OF_EDID1X_STANDARD_TIMING; ++i) {
		struct mode_info info = { 0 };
		struct standard_timing *timing =
			(struct standard_timing *)&buff[0x26+(i*2)];

		get_edid1xx_std_mode(ep, timing, &info);

		if (info.pixel_width == 1920 && info.pixel_height == 1200 &&
			info.field_rate == 60) {
			timing->h_addressable = 0x01;
			timing->u.s_uchar = 0x01;
			checksum_changed = true;
			break;
		}
	}

	if (checksum_changed)
		patch_checksum_error(ep, buff, 0);
}

/*
 * patch_dual_edid_panel_error
 *
 * Patches the edid by forcing the digital edid to an analog edid. It does so by
 * zeroing the digital byte.
 */
static void patch_dual_edid_panel_error(
	struct edid_patch *ep,
	uint8_t *buff)
{
	if (ep->edid_version_major == 1) {
		struct edid_data_v1x *edid_data =
			(struct edid_data_v1x *)buff;

		/* bit 7 of byte 0 of basicDisplayParameters determines digital
		 * (bit 7 = 1) or analog (bit 7 = 0)
		 */
		if (edid_data->basic_display_params[0] & 0x80) {
			/* clear out the entire byte, because the rest of bits
			 * 0 - 6 have different meanings depending on bit 7,
			 * so we shouldn't keep the existing bits.
			 */
			edid_data->basic_display_params[0] = 0;

			/* Analog Edid cannot have extensions */
			edid_data->ext_blk_cnt = 0;

			/* we also want to recalculate the checksum */
			patch_checksum_error(ep, buff, 0);
		}
	}
}

/*
 * patch_multipacked_type_panel_edid
 *
 * Patch the Edid detailed timing blocks based on reserved manufacture timing
 * byte, and .inf select pack type.
 */
static void patch_multipacked_type_panel_edid(
	struct edid_patch *ep,
	uint8_t *buff)
{
	uint32_t i;
	bool checksum_changed = false;
	struct edid_data_v1x *edid_data = (struct edid_data_v1x *)&buff[0];
	const struct monitor_patch_info *info =
		monitor_patch_list_get_patch_info(
			&ep->mpl,
			MONITOR_PATCH_TYPE_MULTIPLE_PACKED_TYPE);
	/* check whether the packed type. */
	union edid13_multipacked_panel_manufacture_reserved_timing_info timing_info;

	timing_info.all = edid_data->established_timings[2];

	if (timing_info.all == 0)
		return; /*this panel is not packed pixel panel. do nothing. */

	if (info->param == DCS_PACKED_PIXEL_FORMAT_B70_G70_R70 &&
		!timing_info.bits.G8)
		return;

	if (info->param == DCS_PACKED_PIXEL_FORMAT_B70_R30_G70_R74 &&
		!timing_info.bits.G10 &&
		!timing_info.bits.G12)
		return;

	/* Patch horizontal sizes of detailed timings for packed pixel format */
	for (i = 0; i < NUM_OF_EDID1X_DETAILED_TIMING; i++) {
		struct edid_detailed *edid_detailed = (struct edid_detailed *)
			&edid_data->edid_detailed_timings[i];
		uint32_t h_addressable;
		uint32_t h_blank;
		uint32_t h_total;
		uint32_t new_h_total;
		uint32_t pix_clk;

		if (edid_detailed->pix_clk == 0)
			continue;

		h_addressable = edid_detailed->pix_width_8_low +
			(edid_detailed->byte4.PIX_WIDTH_4_HIGH << 8);
		h_blank = edid_detailed->h_blank_8_low +
			(edid_detailed->byte4.H_BLANK_4_HIGH << 8);
		h_total = h_addressable + (2 * edid_detailed->h_border) +
			h_blank;
		new_h_total = h_total;
		pix_clk = edid_detailed->pix_clk;

		if (info->param == DCS_PACKED_PIXEL_FORMAT_B70_G70_R70) {
			/* G8: NewHaddressable = ((Edid'sHaddressable +23)/24)*8
			 * must be % 24; % 24 not matching its own table,
			 * conflict specification
			 */
			h_addressable = (h_addressable + 23) / 3 -
				((h_addressable + 23) / 3) % 8;
		} else if (info->param ==
			DCS_PACKED_PIXEL_FORMAT_B70_R30_G70_R74) {
			/* G12: NewHaddressable = ((Edid'sHaddressable + 7) / 8)
			 * * 4, must be 8;
			 */
			h_addressable = (h_addressable + 7) / 2 -
				((h_addressable + 7) / 2) % 8;
		} else
			continue;

		/* NewHtotal =  NewHaddressable + original Blank. */
		new_h_total = h_addressable + (2 * edid_detailed->h_border)
			+ h_blank;

		/* New DVI pixel clock = Edid Pixelclock * (NewHtotal /
		 * EdidHtotal). */
		pix_clk = (edid_detailed->pix_clk * new_h_total) / h_total;

		/* if HRx flag set, NewDviPixelclock *= 2; */
		if ((timing_info.bits.HR0 && i == 0)
			|| (timing_info.bits.HR1 && i == 1))
			pix_clk = pix_clk * 2;

		/* Now, let's overwrite the original. */
		edid_detailed->pix_width_8_low = h_addressable & 0xFF;
		edid_detailed->byte4.PIX_WIDTH_4_HIGH = h_addressable >> 8;
		edid_detailed->pix_clk = (uint16_t) pix_clk;
		checksum_changed = true;
	}

	if (checksum_changed)
		patch_checksum_error(ep, buff, 0);
}

/*
 * dal_edid_patch_apply
 *
 * Apply all relevant patches to the EDID buffer. EDID buffer should match one
 * given at "Initialize"
 *
 */
void dal_edid_patch_apply(struct edid_patch *ep, uint8_t *buff)
{
	uint32_t i;
	uint32_t patch_list_size = monitor_patch_list_size(&ep->mpl);
	struct monitor_patch_info *info;

	if (!buff)
		return;

	for (i = 0; i < patch_list_size; i++) {
		info = monitor_patch_list_get_patch_at(&ep->mpl, i);
		switch (info->type) {
		case MONITOR_PATCH_TYPE_ERROR_CHECKSUM:
			patch_header_error(buff);
			patch_checksum_error(ep, buff, 0);
			break;

		case MONITOR_PATCH_TYPE_VENDOR_1:
			patch_vendor1_workaround(ep, buff);
			patch_checksum_error(ep, buff, 0);
			patch_checksum_error(ep, buff, 1);
			break;

		case MONITOR_PATCH_TYPE_EDID_EXTENTION_ERROR_CHECKSUM:
			patch_checksum_error(ep, buff, 1);
			break;

		case MONITOR_PATCH_TYPE_IGNORE_19X12_STD_TIMING:
			patch_19x12_std_timing_error(ep, buff);
			break;

		case MONITOR_PATCH_TYPE_DUAL_EDID_PANEL:
			if (info->param != 0)
				patch_dual_edid_panel_error(ep, buff);
			break;
		case MONITOR_PATCH_TYPE_MULTIPLE_PACKED_TYPE: {
			bool apply = info->param ==
				DCS_PACKED_PIXEL_FORMAT_B70_R30_G70_R74 ||
				info->param ==
					DCS_PACKED_PIXEL_FORMAT_B70_G70_R70;
			if (ep->packed_pixel_format != 0 && apply)
				patch_multipacked_type_panel_edid(ep, buff);
			break;
		}
		case MONITOR_PATCH_TYPE_PACKED_PIXEL_FORMAT:
			break;

		default:
			break;
		}
	}
}

union dcs_monitor_patch_flags dal_edid_patch_get_monitor_patch_flags(
	struct edid_patch *ep)
{
	return ep->flags;
}

/*
 * dal_edid_patch_get_monitor_patch_info
 *
 * Get patch info for specific patch type. Info includes patch type and patch
 * parameter
 * Returns NULL if display does not require such patch
 *
 */
const struct monitor_patch_info *dal_edid_patch_get_monitor_patch_info(
	struct edid_patch *ep,
	enum monitor_patch_type type)
{
	return monitor_patch_list_get_patch_info(&ep->mpl, type);
}

bool dal_edid_patch_set_monitor_patch_info(
	struct edid_patch *ep,
	struct monitor_patch_info *info)
{
	struct monitor_patch_info *found_info;

	if (!info)
		return false;

	found_info = monitor_patch_list_get_patch_info(&ep->mpl, info->type);

	if (!found_info)
		return false;

	found_info->param = info->param;
	return true;
}

uint32_t dal_edid_patch_get_patches_number(struct edid_patch *ep)
{
	return monitor_patch_list_size(&ep->mpl);
}

/*
 * dal_edid_patch_update_dp_receiver_id_based_monitor_patches
 *
 * Updates patches which are based on DPReceiver information.  This should only
 * be called after edid mfr/prod id based patches are already applied (for now).
 *
 */
void dal_edid_patch_update_dp_receiver_id_based_monitor_patches(
	struct edid_patch *ep,
	struct dp_receiver_id_info *info)
{
	uint32_t delay_after_power_up = 0;
	bool keep_receiver_powered = false;
	bool disable_psr_entry_abort = false;
	unsigned int delay_after_disable_backlight_dfs_bypass = 0;

	if (!info)
		return;

	switch (info->sink_id) {
	case DP_SINK_DEVICE_ID_2:
/*
 * First batch of PSR panels with TCON from ParadeTech shows an intermittent
 * black flash when PSR Abort sequence executed.
 * From debug comments from Parade:
 * The bug is the corner case of handling PSR abort. It happens when following
 * happens:
 * 1. TCON receives PSR in-active command back in the n-k frame
 * 2. TCON starting to exit PSR state. Because of synchronization, it may take k
 * frames to finish the transition (from PSR to live mode)
 * 3. TCON receives PSR active command in the n frame
 * 4. TCON receives PSR abort command in the n+1 frame
 Under this condition, our current PSR TCON will miss the PSR abort command.
 This causes the black screen flash.
*/
		if (!dal_strncmp(info->sink_id_str,
			DP_SINK_DEV_STRING_ID2_REV0,
			sizeof(info->sink_id_str)))
			disable_psr_entry_abort = true;
		else if (info->sink_id_str[1] ==
			DP_SINK_DEV_STRING_ID2_REV1_HW_ID_HIGH_BYTE) {
			/* Second generation PSR TCON from parade also show this
			 * issue. Keep abort disabled for now. The device that
			 * we need this work-around has following ID strings:
			 * DPCD 00400: 0x00 (Parade OUI byte 0)
			 * DPCD 00401: 0x1C (Parade OUI byte 1)
			 * DPCD 00402: 0xF8 (Parade OUI byte 2)
			 * DPCD 00403: 0x61, or 0x62, or 0x63, or 0x72, or 0x73
			 * (HW ID low byte, the same silicon has several
			 * package/feature flavors)
			 * DPCD 00404: 0x06 (HW ID high byte)
			 */
			if ((info->sink_id_str[0] == DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE1) ||
				(info->sink_id_str[0] == DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE2) ||
				(info->sink_id_str[0] == DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE3) ||
				(info->sink_id_str[0] == DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE4) ||
				(info->sink_id_str[0] == DP_SINK_DEV_STRING_ID2_REV1_HW_ID_LOW_BYTE5))
				disable_psr_entry_abort = true;

			/* Parade TCON on PSR panels have a backlight issue. If
			 * backlight is toggled from high -> low for ~20ms ->
			 * high, backlight stops working properly and becomes
			 * very dim.
			 * To resolve this issue, let us detect this TCON and
			 * apply a patch to add delay to prevent this sequence.
			 */
			if (info->sink_hw_revision < 0x2)
				delay_after_disable_backlight_dfs_bypass = 100;
		}
		break;
	default:
		break;
	}

	switch (info->branch_id) {
	case DP_BRANCH_DEVICE_ID_1:
/* Some active dongles (DP-VGA, DP-DLDVI converters) power down all internal
 * circuits including AUX communication preventing reading DPCD table and EDID
 * (spec violation). Encoder will skip DP RX power down on disable_output to
 * keep receiver powered all the time.*/
		if (!dal_strncmp(info->branch_name, DP_VGA_CONVERTER_ID_1,
			sizeof(info->branch_name)) ||
			!dal_strncmp(info->branch_name,
				DP_DVI_CONVERTER_ID_1,
				sizeof(info->branch_name)))
			keep_receiver_powered = true;
		break;

	case DP_BRANCH_DEVICE_ID_4:
/* Workaround for some DP-VGA dongle
 * We will add default 350 ms, after power up to let receiver "get used" to the
 * state
 */
		if (!dal_strncmp(info->branch_name, DP_VGA_CONVERTER_ID_4,
			sizeof(info->branch_name)) ||
			(!dal_strncmp(info->branch_name,
				DP_VGA_CONVERTER_ID_4,
				sizeof(info->branch_name))))
			delay_after_power_up = 350;
		break;

	default:
		break;
	}

	/* now we update the patches based on the values we found above. */

	/* handle MONITOR_PATCH_TYPE_DELAY_AFTER_DP_RECEIVER_POWER_UP */
	if (delay_after_power_up > 0) {
		struct monitor_patch_info info;

		info.type =
			MONITOR_PATCH_TYPE_DELAY_AFTER_DP_RECEIVER_POWER_UP;
		info.param = delay_after_power_up;
		info.manufacturer_id =
			MONITOR_MANUFACTURER_ID_0;
		info.product_id = MONITOR_PRODUCT_ID_0;

		if (ep->mpl.flags.flags.DELAY_AFTER_DP_RECEIVER_POWER_UP) {
			/* if patch is already applied, we only update the patch
			 * param if the delay is larger than the currently set
			 * one. This assumes that DP receiver id based patches
			 * are done after edid mfr/prod id patches are done. */
			if (delay_after_power_up >
				monitor_patch_list_get_patch_info(
					&ep->mpl,
					info.type)->param)
				dal_edid_patch_set_monitor_patch_info(
					ep, &info);
		} else {
			/* otherwise, we don't have the delay patch currently
			 * applied, so insert it to the list */

			/* Insert will never add patch with same type */
			monitor_patch_list_insert(&ep->mpl, &info);
		}
	}

	/* handle MONITOR_PATCH_TYPE_KEEP_DP_RECEIVER_POWERED */
	if (keep_receiver_powered) {
		/* MONITOR_PATCH_TYPE_KEEP_DP_RECEIVER_POWERED is a boolean
		 * patch (patch param is zero, so it will either be applied or
		 * not. If it isn't applied yet, we insert it to the list. */
		if (!ep->mpl.flags.flags.KEEP_DP_RECEIVER_POWERED) {
			struct monitor_patch_info info;

			info.type =
				MONITOR_PATCH_TYPE_KEEP_DP_RECEIVER_POWERED;
			info.param = 0;
			info.manufacturer_id =
				MONITOR_MANUFACTURER_ID_0;
			info.product_id = MONITOR_PRODUCT_ID_0;

			/* Insert will never add patch with same type */
			monitor_patch_list_insert(&ep->mpl, &info);
		}
	}

	/* handle MONITOR_PATCH_TYPE_DisablePsrEntryAbort */
	if (disable_psr_entry_abort) {
		/* MONITOR_PATCH_TYPE_DisablePsrEntryAbort is a boolean patch
		 * (patch param is zero, so it will either be applied or not.
		 * If it isn't applied yet, we insert it to the list. */
		if (!ep->mpl.flags.flags.DISABLE_PSR_ENTRY_ABORT) {
			struct monitor_patch_info info;

			info.type =
				MONITOR_PATCH_TYPE_DISABLE_PSR_ENTRY_ABORT;
			info.param = 0;
			info.manufacturer_id =
				MONITOR_MANUFACTURER_ID_0;
			info.product_id = MONITOR_PRODUCT_ID_0;

			/* Insert will never add patch with same type */
			monitor_patch_list_insert(&ep->mpl, &info);
		}
	}

	/* handle MONITOR_PATCH_TYPE_DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS*/
	if (delay_after_disable_backlight_dfs_bypass) {
		if (!ep->mpl.flags.flags.
				DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS) {
			struct monitor_patch_info info;

			info.type =
				MONITOR_PATCH_TYPE_DELAY_AFTER_DISABLE_BACKLIGHT_DFS_BYPASS;
			info.param = delay_after_disable_backlight_dfs_bypass;
			info.manufacturer_id =
				MONITOR_MANUFACTURER_ID_0;
			info.product_id = MONITOR_PRODUCT_ID_0;

			/* Insert will never add patch with same type */
			monitor_patch_list_insert(&ep->mpl, &info);
		}
	}
}
