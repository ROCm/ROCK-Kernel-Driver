/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "gmc/gmc_8_2_sh_mask.h"
#include "gmc/gmc_8_2_d.h"

#include "include/logger_interface.h"
#include "include/adapter_service_interface.h"

#include "fbc_dce110.h"

static const uint32_t compressed_surface_address_high_reg[] = {
	mmDCP0_GRPH_COMPRESS_SURFACE_ADDRESS_HIGH,
	mmDCP1_GRPH_COMPRESS_SURFACE_ADDRESS_HIGH,
	mmDCP2_GRPH_COMPRESS_SURFACE_ADDRESS_HIGH,
	mmDCP3_GRPH_COMPRESS_SURFACE_ADDRESS_HIGH
};

static const uint32_t compressed_surface_address_reg[] = {
	mmDCP0_GRPH_COMPRESS_SURFACE_ADDRESS,
	mmDCP1_GRPH_COMPRESS_SURFACE_ADDRESS,
	mmDCP2_GRPH_COMPRESS_SURFACE_ADDRESS,
	mmDCP3_GRPH_COMPRESS_SURFACE_ADDRESS
};

static const uint32_t compressed_surface_pitch[] = {
	mmDCP0_GRPH_COMPRESS_PITCH,
	mmDCP1_GRPH_COMPRESS_PITCH,
	mmDCP2_GRPH_COMPRESS_PITCH,
	mmDCP3_GRPH_COMPRESS_PITCH
};

static const uint32_t stutter_control_non_lpt_ch_reg[] = {
	mmDMIF_PG0_DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
	mmDMIF_PG1_DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
	mmDMIF_PG2_DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
	mmDMIF_PG3_DPG_PIPE_STUTTER_CONTROL_NONLPTCH
};

static const uint32_t dce11_one_lpt_channel_max_resolution = 2560 * 1600;

/*
 * DCE 11 Frame Buffer Compression Implementation
 */

void dal_fbc_dce110_wait_for_fbc_state_changed(
	struct fbc *fbc,
	bool enabled)
{
	uint8_t counter = 0;
	uint32_t addr = mmFBC_STATUS;
	uint32_t value;

	while (counter < 10) {
		value = dal_read_reg(fbc->context, addr);
		if (get_reg_field_value(
			value,
			FBC_STATUS,
			FBC_ENABLE_STATUS) == enabled)
			break;
		dal_delay_in_microseconds(10);
		counter++;
	}

	if (counter == 10) {
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s: wait counter exceeded, changes to HW not applied",
			__func__);
	}
}

static uint32_t lpt_required_size(struct fbc *fbc, uint32_t fbc_size)
{
	uint32_t chan_divider = 1;
	/* chan_divider = 2 pow (LOW_POWER_TILING_MODE) */
	if (fbc->lpt_channels_num == 1)
		chan_divider = 1;
	else
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s: Unexpected DCE11 number of LPT DRAM channels",
			__func__);

	/* LPT_SURFACE_SIZE (in bytes) = FBC_COMPRESSED_SURFACE_SIZE (in bytes)
	 * * DRAM_CHANNELS / 2 pow (LOW_POWER_TILING_MODE).
	 * fbc->m_numOfLPTChannels = 1 == > chanDivider = 2 pow
	 * (LOW_POWER_TILING_MODE) = 2 pow 0 = 1
	 */
	return fbc_size * fbc->memory_bus_width / 64 / chan_divider;
}

static uint32_t lpt_size_alignment(struct fbc *fbc)
{
	/*LPT_ALIGNMENT (in bytes) = ROW_SIZE * #BANKS * # DRAM CHANNELS. */
	return fbc->raw_size * fbc->banks_num * fbc->dram_channels_num;
}

void dal_fbc_dce110_power_up_fbc(struct fbc *fbc)
{
	uint32_t value;
	uint32_t addr;

	addr = mmFBC_CNTL;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
	set_reg_field_value(value, 1, FBC_CNTL, FBC_EN);
	set_reg_field_value(value, 2, FBC_CNTL, FBC_COHERENCY_MODE);
	if (fbc->options.bits.CLK_GATING_DISABLED == 1) {
		/* HW needs to do power measurment comparision. */
		set_reg_field_value(
			value,
			0,
			FBC_CNTL,
			FBC_COMP_CLK_GATE_EN);
	}
	dal_write_reg(fbc->context, addr, value);

	addr = mmFBC_COMP_MODE;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_RLE_EN);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_DPCM4_RGB_EN);
	set_reg_field_value(value, 1, FBC_COMP_MODE, FBC_IND_EN);
	dal_write_reg(fbc->context, addr, value);

	addr = mmFBC_COMP_CNTL;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(value, 1, FBC_COMP_CNTL, FBC_DEPTH_RGB08_EN);
	dal_write_reg(fbc->context, addr, value);
	/*FBC_MIN_COMPRESSION 0 ==> 2:1 */
	/*                    1 ==> 4:1 */
	/*                    2 ==> 8:1 */
	/*                  0xF ==> 1:1 */
	set_reg_field_value(value, 0xF, FBC_COMP_CNTL, FBC_MIN_COMPRESSION);
	dal_write_reg(fbc->context, addr, value);
	fbc->min_compress_ratio = FBC_COMPRESS_RATIO_1TO1;

	value = 0;
	dal_write_reg(fbc->context, mmFBC_IND_LUT0, value);

	value = 0xFFFFFF;
	dal_write_reg(fbc->context, mmFBC_IND_LUT1, value);
}

static void destroy(struct fbc **fbc)
{
	dal_free(*fbc);
	*fbc = NULL;
}

bool dal_fbc_dce110_get_required_compressed_surface_size(
	struct fbc *fbc,
	struct fbc_input_info *input_info,
	struct fbc_requested_compressed_size *size)
{
	bool result = false;

	if (fbc->options.bits.LPT_MC_CONFIG == 0) {
		if (input_info->lpt_config.banks_num == 0 ||
			input_info->lpt_config.row_size == 0) {
			dal_logger_write(fbc->context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: incorrect input data\n",
				__func__);
		}

		fbc->banks_num = input_info->lpt_config.banks_num;
		fbc->raw_size = input_info->lpt_config.row_size;
		fbc->channel_interleave_size =
			input_info->lpt_config.chan_interleave_size;
		fbc->dram_channels_num =
			input_info->lpt_config.mem_channels_num;
		fbc->options.bits.LPT_MC_CONFIG = 1;
	}

	if (input_info->dynamic_fbc_buffer_alloc == 0) {
		/* This is default option at boot up time */
		if (fbc->embedded_panel_h_size != 0
			&& fbc->embedded_panel_v_size != 0) {
			/* Align horizontal to the required number of chunks
			 * (256 pixels (by 2 lines)) (min compression ratio =
			 * 1:1 for DCE 11) */
			size->prefered_size =
				dal_fbc_align_to_chunks_number_per_line(
					fbc,
					fbc->embedded_panel_h_size)
					/* For FBC when LPT not supported */
					* fbc->embedded_panel_v_size * 4;
			size->min_size = size->prefered_size;
			/* For FBC when LPT not supported */
			size->prefered_size_alignment = 0x100;
			size->min_size_alignment = 0x100;
			if (fbc->options.bits.LPT_SUPPORT == true) {
				size->prefered_size = lpt_required_size(
					fbc,
					size->min_size);
				size->prefered_size_alignment =
					lpt_size_alignment(fbc);
			}
			size->flags.PREFERED_MUST_BE_FRAME_BUFFER_POOL = 1;
			size->flags.MIN_MUST_BE_FRAME_BUFFER_POOL = 1;
			fbc->preferred_requested_size = size->prefered_size;
			result = true;
		} else {
			/* For DCE11 here use Max HW supported size */
			/* Use: 18000 Chunks * 256 * 2 pixels * 4 bytes.
			 * (For FBC when LPT not supported). */
			size->min_size = 18000 * 256 * 2 * 4;
			size->prefered_size = size->min_size;
			/* For FBC when LPT not supported */
			size->prefered_size_alignment = 0x100;
			size->min_size_alignment = 0x100;
			if (fbc->options.bits.LPT_SUPPORT == true) {
				size->prefered_size = lpt_required_size(
					fbc,
					size->min_size);
				size->prefered_size_alignment =
					lpt_size_alignment(fbc);
			}
			size->flags.PREFERED_MUST_BE_FRAME_BUFFER_POOL = 1;
			size->flags.MIN_MUST_BE_FRAME_BUFFER_POOL = 1;
			fbc->preferred_requested_size = size->prefered_size;
			result = true;
		}
	} else {
		/* Dynamic allocation at mode set time
		 * For DCE11 here use actual mode that is going to be set - LPT
		 * not supported in this case, FBC Compressed surface can be in
		 * System (Scaterred) memory without LPT support. If Embedded
		 * Panel present FBC is supported only up to Embedded Panel size
		 */
		if (dal_fbc_is_source_bigger_than_epanel_size(
			fbc,
			input_info->source_view_width,
			input_info->source_view_height)) {
			/* Align horizontal to the required number of chunks
			 * (256 pixels (by 2 lines)) (min compression ratio =
			 * 1:1 for DCE 11) */
			/*For FBC when LPT not supported */
			size->min_size =
				dal_fbc_align_to_chunks_number_per_line(
					fbc,
					fbc->embedded_panel_h_size)
					* fbc->embedded_panel_v_size * 4;
			size->prefered_size = size->min_size;
			/* For FBC when LPT not supported */
			size->prefered_size_alignment = 0x100;
			size->min_size_alignment = 0x100;
		} else {
			/* (For FBC when LPT not supported). */
			size->min_size =
				dal_fbc_align_to_chunks_number_per_line(
					fbc,
					input_info->source_view_width)
					* input_info->source_view_height * 4;
			size->prefered_size = size->min_size;

			if (input_info->source_view_width *
				input_info->source_view_height
					> FBC_MAX_X * FBC_MAX_Y) {
				/* can not exceed HW limit. */
				size->prefered_size =
				/* (For FBC when LPT not supported).*/
				dal_fbc_align_to_chunks_number_per_line(fbc,
						FBC_MAX_X) * FBC_MAX_Y * 4;
				/* Note:
				 * this is the same as 18000 * 256 * 2 * 4;
				 * Use: 18000 Chunks * 256 * 2 pixels * 4 bytes.
				 * (For FBC when LPT not supported).
				 * FBC mixed mode is disabled by default --
				 * which disable FBC for resolutions bigger
				 * than supported by FBC HW,
				 * partial compression not supported. */
			}

			/* For FBC when LPT not supported */
			size->prefered_size_alignment = 0x100;
			size->min_size_alignment = 0x100;
		}
		size->flags.PREFERED_MUST_BE_FRAME_BUFFER_POOL = 0;
		size->flags.MIN_MUST_BE_FRAME_BUFFER_POOL = 0;
		fbc->preferred_requested_size = size->prefered_size;
		result = true;

		/* In case more than 2 displays will be active on this mode set
		 * do not even allocate compressed surface as FBC will not be
		 * enabled. This code may be enabled after Initial bringup and
		 * testing on Carrizo */
		/*
		 if (pFBCInpuInfo->numOfActiveTargets > 2)
		 {
		 pSize->preferedSize = pSize->minSize = 0;
		 pSize->preferedSizeAlignment = pSize->minSizeAlignment = 0;
		 pSize->bits.preferedMustBeFrameBufferPool = 0;
		 pSize->bits.minMustBeFrameBufferPool = 0;
		 fbc->m_preferredRequestedSize = 0;
		 fbc->m_comprSurfaceAddress.QuadPart = 0;
		 bReturn = false;
		 } */
	}

	return result;
}

static void disable_fbc(struct fbc *fbc)
{
	if (fbc->options.bits.FBC_SUPPORT &&
		fbc->funcs->is_fbc_enabled_in_hw(fbc, NULL)) {
		uint32_t reg_data;
		/* Turn off compression */
		reg_data = dal_read_reg(fbc->context, mmFBC_CNTL);
		set_reg_field_value(reg_data, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
		dal_write_reg(fbc->context, mmFBC_CNTL, reg_data);

		/* Reset enum controller_id to undefined */
		fbc->attached_controller_id = CONTROLLER_ID_UNDEFINED;

		/* Whenever disabling FBC make sure LPT is disabled if LPT
		 * supported */
		if (fbc->options.bits.LPT_SUPPORT)
			fbc->funcs->disable_lpt(fbc);

		dal_fbc_dce110_wait_for_fbc_state_changed(fbc, false);
	}
}

void dal_fbc_dce110_program_compressed_surface_address_and_pitch(
	struct fbc *fbc,
	struct compr_addr_and_pitch_params *params)
{
	uint32_t value = 0;
	uint32_t fbc_pitch = 0;
	uint32_t inx = fbc->funcs->controller_idx(
		fbc,
		params->controller_id);
	uint32_t compressed_surf_address_low_part =
		fbc->compr_surface_address.addr.low_part;

	/* Clear content first. */
	dal_write_reg(
		fbc->context,
		compressed_surface_address_high_reg[inx],
		0);
	dal_write_reg(fbc->context, compressed_surface_address_reg[inx], 0);

	if (fbc->options.bits.LPT_SUPPORT) {
		uint32_t lpt_alignment = lpt_size_alignment(fbc);

		if (lpt_alignment != 0) {
			compressed_surf_address_low_part =
				((compressed_surf_address_low_part
					+ (lpt_alignment - 1)) / lpt_alignment)
					* lpt_alignment;
		}
	}

	/* Write address, HIGH has to be first. */
	dal_write_reg(fbc->context, compressed_surface_address_high_reg[inx],
		fbc->compr_surface_address.addr.high_part);
	dal_write_reg(fbc->context, compressed_surface_address_reg[inx],
		compressed_surf_address_low_part);

	fbc_pitch = dal_fbc_align_to_chunks_number_per_line(
		fbc,
		params->source_view_width);

	if (fbc->min_compress_ratio == FBC_COMPRESS_RATIO_1TO1)
		fbc_pitch = fbc_pitch / 8;
	else
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s: Unexpected DCE11 compression ratio",
			__func__);

	/* Clear content first. */
	dal_write_reg(fbc->context, compressed_surface_pitch[inx], 0);

	/* Write FBC Pitch. */
	set_reg_field_value(
		value,
		fbc_pitch,
		GRPH_COMPRESS_PITCH,
		GRPH_COMPRESS_PITCH);
	dal_write_reg(fbc->context, compressed_surface_pitch[inx], value);

}

/*
 FBCIdleForce_DisplayRegisterUpdate    = 0x00000001,  bit  0
 FBCIdleForce_MemoryWriteOtherThanMCIF = 0x10000000,  bit 28
 FBCIdleForce_CGStaticScreenIsInactive = 0x20000000,  bit 29
 */
static void set_fbc_invalidation_triggers(struct fbc *fbc, uint32_t fbc_trigger)
{
	uint32_t value;
	uint32_t addr;
	/* Disable region hit event, FBC_MEMORY_REGION_MASK = 0 (bits 16-19)
	 * for DCE 11 regions cannot be used - does not work with S/G */
	addr = mmFBC_CLIENT_REGION_MASK;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		0,
		FBC_CLIENT_REGION_MASK,
		FBC_MEMORY_REGION_MASK);
	dal_write_reg(fbc->context, addr, value);

	/* Setup events when to clear all CSM entries (effectively marking
	 * current compressed data invalid).
	 * For DCE 11 CSM metadata 11111 means - "Not Compressed"
	 * Used as the initial value of the metadata sent to the compressor
	 * after invalidation, to indicate that the compressor should attempt
	 * to compress all chunks on the current pass.  Also used when the chunk
	 * is not successfully written to memory.
	 * When this CSM value is detected, FBC reads from the uncompressed
	 * buffer. Set events according to passed in value, these events are
	 * valid for DCE11:
	 * - display register updated
	 * - memory write from any client except from MCIF
	 * - CG static screen signal is inactive */
	addr = mmFBC_IDLE_FORCE_CLEAR_MASK;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		fbc_trigger,
		FBC_IDLE_FORCE_CLEAR_MASK,
		FBC_IDLE_FORCE_CLEAR_MASK);
	dal_write_reg(fbc->context, addr, value);

	/* DAL2 CL#1051812: enable this cause display flashing on register
	 * read when SG enabled, according the HW, this FBC_IDLE_MASK is
	 * obsolete, SW could remove programming it */
#if 0
	addr = mmFBC_IDLE_MASK;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(value, fbc_trigger, FBC_IDLE_MASK, FBC_IDLE_MASK);
	dal_write_reg(fbc->context, addr, value);
#endif
}

void dal_fbc_dce110_enable_fbc(
	struct fbc *fbc,
	uint32_t paths_num,
	struct compr_addr_and_pitch_params *params)
{
	if (fbc->options.bits.FBC_SUPPORT &&
		(fbc->options.bits.DUMMY_BACKEND == 0) &&
		(!fbc->funcs->is_fbc_enabled_in_hw(fbc, NULL)) &&
		(!dal_fbc_is_source_bigger_than_epanel_size(
			fbc,
			params->source_view_width,
			params->source_view_height))) {
		uint32_t value;
		uint32_t addr;

		/* Before enabling FBC first need to enable LPT if applicable
		 * LPT state should always be changed (enable/disable) while FBC
		 * is disabled */
		if (fbc->options.bits.LPT_SUPPORT && (paths_num < 2) &&
			(params->source_view_width *
				params->source_view_height <=
				dce11_one_lpt_channel_max_resolution)) {
			fbc->funcs->enable_lpt(
				fbc, paths_num, params->controller_id);
		}

		addr = mmFBC_CNTL;
		value = dal_read_reg(fbc->context, addr);
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		set_reg_field_value(
			value,
			fbc->funcs->controller_idx(fbc, params->controller_id),
			FBC_CNTL, FBC_SRC_SEL);
		dal_write_reg(fbc->context, addr, value);

		/* Keep track of enum controller_id FBC is attached to */
		fbc->attached_controller_id = params->controller_id;

		/*Toggle it as there is bug in HW */
		set_reg_field_value(value, 0, FBC_CNTL, FBC_GRPH_COMP_EN);
		dal_write_reg(fbc->context, addr, value);
		set_reg_field_value(value, 1, FBC_CNTL, FBC_GRPH_COMP_EN);
		dal_write_reg(fbc->context, addr, value);

		dal_fbc_dce110_wait_for_fbc_state_changed(fbc, true);
	}
}

bool dal_fbc_dce110_is_fbc_enabled_in_hw(
	struct fbc *fbc,
	enum controller_id *fbc_mapped_crtc_id)
{
	/* Check the hardware register */
	uint32_t value;

	value = dal_read_reg(fbc->context, mmFBC_STATUS);
	if (get_reg_field_value(value, FBC_STATUS, FBC_ENABLE_STATUS)) {
		if (fbc_mapped_crtc_id != NULL)
			*fbc_mapped_crtc_id = fbc->attached_controller_id;
		return true;
	}

	value = dal_read_reg(fbc->context, mmFBC_MISC);
	if (get_reg_field_value(value, FBC_MISC, FBC_STOP_ON_HFLIP_EVENT)) {
		value = dal_read_reg(fbc->context, mmFBC_CNTL);

		if (get_reg_field_value(value, FBC_CNTL, FBC_GRPH_COMP_EN)) {
			if (fbc_mapped_crtc_id != NULL)
				*fbc_mapped_crtc_id =
					fbc->attached_controller_id;
			return true;
		}
	}
	if (fbc_mapped_crtc_id != NULL)
		*fbc_mapped_crtc_id = CONTROLLER_ID_UNDEFINED;
	return false;
}

bool dal_fbc_dce110_is_lpt_enabled_in_hw(struct fbc *fbc)
{
	/* Check the hardware register */
	uint32_t value = dal_read_reg(fbc->context, mmLOW_POWER_TILING_CONTROL);

	return get_reg_field_value(
		value,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
}

void dal_fbc_dce110_disable_lpt(struct fbc *fbc)
{
	uint32_t value;
	uint32_t addr;
	uint32_t inx;

	/* Disable all pipes LPT Stutter */
	for (inx = 0; inx < 3; inx++) {
		value =
			dal_read_reg(
				fbc->context,
				stutter_control_non_lpt_ch_reg[inx]);
		set_reg_field_value(
			value,
			0,
			DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
			STUTTER_ENABLE_NONLPTCH);
		dal_write_reg(
			fbc->context,
			stutter_control_non_lpt_ch_reg[inx],
			value);
	}
	/* Disable Underlay pipe LPT Stutter */
	addr = mmDPGV0_PIPE_STUTTER_CONTROL_NONLPTCH;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		0,
		DPGV0_PIPE_STUTTER_CONTROL_NONLPTCH,
		STUTTER_ENABLE_NONLPTCH);
	dal_write_reg(fbc->context, addr, value);

	/* Disable LPT */
	addr = mmLOW_POWER_TILING_CONTROL;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		0,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
	dal_write_reg(fbc->context, addr, value);

	/* Clear selection of Channel(s) containing Compressed Surface */
	addr = mmGMCON_LPT_TARGET;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		0xFFFFFFFF,
		GMCON_LPT_TARGET,
		STCTRL_LPT_TARGET);
	dal_write_reg(fbc->context, mmGMCON_LPT_TARGET, addr);
}

void dal_fbc_dce110_enable_lpt(
	struct fbc *fbc,
	uint32_t paths_num,
	enum controller_id cntl_id)
{
	uint32_t inx = fbc->funcs->controller_idx(fbc, cntl_id);
	uint32_t value;
	uint32_t addr;
	uint32_t value_control;
	uint32_t channels;

	/* Enable LPT Stutter from Display pipe */
	value = dal_read_reg(fbc->context, stutter_control_non_lpt_ch_reg[inx]);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_STUTTER_CONTROL_NONLPTCH,
		STUTTER_ENABLE_NONLPTCH);
	dal_write_reg(fbc->context, stutter_control_non_lpt_ch_reg[inx], value);

	/* Enable Underlay pipe LPT Stutter */
	addr = mmDPGV0_PIPE_STUTTER_CONTROL_NONLPTCH;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_STUTTER_CONTROL_NONLPTCH,
		STUTTER_ENABLE_NONLPTCH);
	dal_write_reg(fbc->context, addr, value);

	/* Selection of Channel(s) containing Compressed Surface: 0xfffffff
	 * will disable LPT.
	 * STCTRL_LPT_TARGETn corresponds to channel n. */
	addr = mmLOW_POWER_TILING_CONTROL;
	value_control = dal_read_reg(fbc->context, addr);
	channels = get_reg_field_value(value_control,
			LOW_POWER_TILING_CONTROL,
			LOW_POWER_TILING_MODE);

	addr = mmGMCON_LPT_TARGET;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		channels + 1, /* not mentioned in programming guide,
				but follow DCE8.1 */
		GMCON_LPT_TARGET,
		STCTRL_LPT_TARGET);
	dal_write_reg(fbc->context, addr, value);

	/* Enable LPT */
	addr = mmLOW_POWER_TILING_CONTROL;
	value = dal_read_reg(fbc->context, addr);
	set_reg_field_value(
		value,
		1,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ENABLE);
	dal_write_reg(fbc->context, addr, value);
}

static uint32_t lpt_memory_control_config(struct fbc *fbc, uint32_t lpt_control)
{
	/*LPT MC Config */
	if (fbc->options.bits.LPT_MC_CONFIG == 1) {
		/* POSSIBLE VALUES for LPT NUM_PIPES (DRAM CHANNELS):
		 * 00 - 1 CHANNEL
		 * 01 - 2 CHANNELS
		 * 02 - 4 OR 6 CHANNELS
		 * (Only for discrete GPU, N/A for CZ)
		 * 03 - 8 OR 12 CHANNELS
		 * (Only for discrete GPU, N/A for CZ) */
		switch (fbc->dram_channels_num) {
		case 2:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_PIPES);
			break;
		case 1:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_PIPES);
			break;
		default:
			dal_logger_write(
				fbc->context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: Invalid LPT NUM_PIPES!!!",
				__func__);
			break;
		}

		/* The mapping for LPT NUM_BANKS is in
		 * GRPH_CONTROL.GRPH_NUM_BANKS register field
		 * Specifies the number of memory banks for tiling
		 * purposes. Only applies to 2D and 3D tiling modes.
		 * POSSIBLE VALUES:
		 * 00 - DCP_GRPH_NUM_BANKS_2BANK: ADDR_SURF_2_BANK
		 * 01 - DCP_GRPH_NUM_BANKS_4BANK: ADDR_SURF_4_BANK
		 * 02 - DCP_GRPH_NUM_BANKS_8BANK: ADDR_SURF_8_BANK
		 * 03 - DCP_GRPH_NUM_BANKS_16BANK: ADDR_SURF_16_BANK */
		switch (fbc->banks_num) {
		case 16:
			set_reg_field_value(
				lpt_control,
				3,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 8:
			set_reg_field_value(
				lpt_control,
				2,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 4:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		case 2:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_NUM_BANKS);
			break;
		default:
			dal_logger_write(
				fbc->context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: Invalid LPT NUM_BANKS!!!",
				__func__);
			break;
		}

		/* The mapping is in DMIF_ADDR_CALC.
		 * ADDR_CONFIG_PIPE_INTERLEAVE_SIZE register field for
		 * Carrizo specifies the memory interleave per pipe.
		 * It effectively specifies the location of pipe bits in
		 * the memory address.
		 * POSSIBLE VALUES:
		 * 00 - ADDR_CONFIG_PIPE_INTERLEAVE_256B: 256 byte
		 * interleave
		 * 01 - ADDR_CONFIG_PIPE_INTERLEAVE_512B: 512 byte
		 * interleave
		 */
		switch (fbc->channel_interleave_size) {
		case 256: /*256B */
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_PIPE_INTERLEAVE_SIZE);
			break;
		case 512: /*512B */
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_PIPE_INTERLEAVE_SIZE);
			break;
		default:
			dal_logger_write(
				fbc->context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: Invalid LPT INTERLEAVE_SIZE!!!",
				__func__);
			break;
		}

		/* The mapping for LOW_POWER_TILING_ROW_SIZE is in
		 * DMIF_ADDR_CALC.ADDR_CONFIG_ROW_SIZE register field
		 * for Carrizo. Specifies the size of dram row in bytes.
		 * This should match up with NOOFCOLS field in
		 * MC_ARB_RAMCFG (ROW_SIZE = 4 * 2 ^^ columns).
		 * This register DMIF_ADDR_CALC is not used by the
		 * hardware as it is only used for addrlib assertions.
		 * POSSIBLE VALUES:
		 * 00 - ADDR_CONFIG_1KB_ROW: Treat 1KB as DRAM row
		 * boundary
		 * 01 - ADDR_CONFIG_2KB_ROW: Treat 2KB as DRAM row
		 * boundary
		 * 02 - ADDR_CONFIG_4KB_ROW: Treat 4KB as DRAM row
		 * boundary */
		switch (fbc->raw_size) {
		case 4096: /*4 KB */
			set_reg_field_value(
				lpt_control,
				2,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		case 2048:
			set_reg_field_value(
				lpt_control,
				1,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		case 1024:
			set_reg_field_value(
				lpt_control,
				0,
				LOW_POWER_TILING_CONTROL,
				LOW_POWER_TILING_ROW_SIZE);
			break;
		default:
			dal_logger_write(
				fbc->context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_CONTROLLER,
				"%s: Invalid LPT ROW_SIZE!!!",
				__func__);
			break;
		}
	} else {
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s: LPT MC Configuration is not provided",
			__func__);
	}

	return lpt_control;
}

void dal_fbc_dce110_program_lpt_control(
	struct fbc *fbc,
	struct compr_addr_and_pitch_params *params)
{
	uint32_t rows_per_channel;
	uint32_t lpt_alignment;
	uint32_t source_view_width;
	uint32_t source_view_height;
	uint32_t lpt_control = 0;

	if (!fbc->options.bits.LPT_SUPPORT)
		return;

	lpt_control = dal_read_reg(fbc->context, mmLOW_POWER_TILING_CONTROL);

	/* POSSIBLE VALUES for Low Power Tiling Mode:
	 * 00 - Use channel 0
	 * 01 - Use Channel 0 and 1
	 * 02 - Use Channel 0,1,2,3
	 * 03 - reserved */
	switch (fbc->lpt_channels_num) {
	/* case 2:
	 * Use Channel 0 & 1 / Not used for DCE 11 */
	case 1:
		/*Use Channel 0 for LPT for DCE 11 */
		set_reg_field_value(
			lpt_control,
			0,
			LOW_POWER_TILING_CONTROL,
			LOW_POWER_TILING_MODE);
		break;
	default:
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s: Invalid selected DRAM channels for LPT!!!",
			__func__);
		break;
	}

	lpt_control = lpt_memory_control_config(fbc, lpt_control);

	/* Program LOW_POWER_TILING_ROWS_PER_CHAN field which depends on
	 * FBC compressed surface pitch.
	 * LOW_POWER_TILING_ROWS_PER_CHAN = Roundup ((Surface Height *
	 * Surface Pitch) / (Row Size * Number of Channels *
	 * Number of Banks)). */
	rows_per_channel = 0;
	lpt_alignment = lpt_size_alignment(fbc);
	source_view_width =
		dal_fbc_align_to_chunks_number_per_line(
			fbc,
			params->source_view_width);
	source_view_height = (params->source_view_height + 1) & (~0x1);

	if (lpt_alignment != 0) {
		rows_per_channel = source_view_width * source_view_height * 4;
		rows_per_channel =
			(rows_per_channel % lpt_alignment) ?
				(rows_per_channel / lpt_alignment + 1) :
				rows_per_channel / lpt_alignment;
	}

	set_reg_field_value(
		lpt_control,
		rows_per_channel,
		LOW_POWER_TILING_CONTROL,
		LOW_POWER_TILING_ROWS_PER_CHAN);

	dal_write_reg(fbc->context, mmLOW_POWER_TILING_CONTROL, lpt_control);
}

static uint32_t controller_idx(struct fbc *fbc, enum controller_id id)
{
	switch (id) {
	case CONTROLLER_ID_D0:
		return 0;
	case CONTROLLER_ID_D1:
		return 1;
	case CONTROLLER_ID_D2:
		return 2;
	default:
		dal_logger_write(
			fbc->context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_CONTROLLER,
			"%s::%s: unexpected controller id: %d\n",
			__FILE__,
			__func__,
			id);
		return 0;
	}
}

static const struct fbc_funcs funcs = {
	.power_up_fbc = dal_fbc_dce110_power_up_fbc,
	.disable_fbc = disable_fbc,
	.enable_fbc = dal_fbc_dce110_enable_fbc,
	.is_fbc_enabled_in_hw = dal_fbc_dce110_is_fbc_enabled_in_hw,
	.is_lpt_enabled_in_hw = dal_fbc_dce110_is_lpt_enabled_in_hw,
	.set_fbc_invalidation_triggers = set_fbc_invalidation_triggers,
	.get_required_compressed_surface_size =
		dal_fbc_dce110_get_required_compressed_surface_size,
	.program_compressed_surface_address_and_pitch =
		dal_fbc_dce110_program_compressed_surface_address_and_pitch,
	.disable_lpt = dal_fbc_dce110_disable_lpt,
	.enable_lpt = dal_fbc_dce110_enable_lpt,
	.program_lpt_control = dal_fbc_dce110_program_lpt_control,
	.controller_idx = controller_idx,
	.destroy = destroy
};

bool dal_fbc_dce110_construct(struct fbc *fbc, struct fbc_init_data *data)
{
	if (!dal_fbc_construct(fbc, data))
		return false;

	fbc->funcs = &funcs;
	fbc->options.bits.FBC_SUPPORT = true;
	if (!(dal_adapter_service_is_feature_supported(
		FEATURE_DISABLE_LPT_SUPPORT)))
		fbc->options.bits.LPT_SUPPORT = true;
	 /* For DCE 11 always use one DRAM channel for LPT */
	fbc->lpt_channels_num = 1;

	if (dal_adapter_service_is_feature_supported(FEATURE_DUMMY_FBC_BACKEND))
		fbc->options.bits.DUMMY_BACKEND = true;

	/* Check if this system has more than 1 DRAM channel; if only 1 then LPT
	 * should not be supported */
	if (fbc->memory_bus_width == 64)
		fbc->options.bits.LPT_SUPPORT = false;

	if (dal_adapter_service_is_feature_supported(
		FEATURE_DISABLE_FBC_COMP_CLK_GATE))
		fbc->options.bits.CLK_GATING_DISABLED = true;

	return true;
}

struct fbc *dal_fbc_dce110_create(struct fbc_init_data *data)
{
	struct fbc *fbc = dal_alloc(sizeof(*fbc));

	if (!fbc)
		return NULL;

	if (dal_fbc_dce110_construct(fbc, data))
		return fbc;

	dal_free(fbc);
	return NULL;
}
