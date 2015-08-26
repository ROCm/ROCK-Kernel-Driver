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
#include "dpsst_link_service.h"
#include "include/link_service_interface.h"
#include "include/display_path_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/i2caux_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/dcs_interface.h"
#include "include/logger_interface.h"
#include "include/ddc_service_types.h"

struct display_path;
struct ddc;
struct aux_payload;
struct aux_command;
struct adapter_service;
struct i2caux;

static const struct link_settings bandwidth_priority_table[] = {
/*162 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 270 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 2160 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED } };

/*bandwidth table for DP 3.24GHz support*/
static const struct link_settings bandwidth_priority_table_rbr2[] = {
/* 162 Mbytes/seci*/
{ LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 270 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_RBR2, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_RBR2, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 1296 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_RBR2, LINK_SPREAD_DISABLED } };

static const struct link_settings link_training_fallback_table[] = {
/* 2160 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 270 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 162 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED } };

/*bandwidth table for DP 3.24GHz support*/
static const struct link_settings link_training_fallback_table_rbr2[] = {
/* 1296 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_RBR2, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_RBR2, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_RBR2, LINK_SPREAD_DISABLED },
/* 270 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 162 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED } };

/* maximum pre emphasis level allowed for each voltage swing level*/
const enum pre_emphasis voltage_swing_to_pre_emphasis[] = {
		PRE_EMPHASIS_LEVEL3,
		PRE_EMPHASIS_LEVEL2,
		PRE_EMPHASIS_LEVEL1,
		PRE_EMPHASIS_DISABLED };

static bool tmds_converter_capability_translate(
		struct dpsst_link_service *dpsst,
		union downstream_port *downstream_port,
		union dwnstream_portx_caps *downstream_caps)
{
	struct link_service *ls = &dpsst->link_service;
	struct tmds_converter_capability *converter_caps =
				&dpsst->tmds_conv_capability;

	converter_caps->use_default_caps = false;
	converter_caps->port_present = downstream_port->bits.PRESENT;
	converter_caps->port_type = (enum dpcd_downstream_port_type)
			downstream_port->bits.TYPE;

	if (converter_caps->port_present == 0 ||
			converter_caps->port_type != DOWNSTREAM_DVI_HDMI)
		return false;

	converter_caps->port_detailed_type =
			(enum dpcd_downstream_port_detailed_type)
			downstream_caps->bytes.byte0.bits.DWN_STRM_PORTX_TYPE;

	if (downstream_port->bits.DETAILED_CAPS) {
		converter_caps->max_tmds_clock =
				downstream_caps->bytes.max_tmds_clk * 25/10;
		if (0 == converter_caps->max_tmds_clock ||
				converter_caps->max_tmds_clock < 25) {

			converter_caps->use_default_caps = true;
			dal_logger_write(ls->dal_context->logger,
					LOG_MAJOR_WARNING,
					LOG_MINOR_COMPONENT_LINK_SERVICE,
					"Dongle Dpcd data is incomplete or invalid");
		}

		switch (downstream_caps->bytes.byte2.bits.MAX_BITS_PER_COLOR_COMPONENT)	{
		case 0:
			converter_caps->max_color_depth = COLOR_DEPTH_INDEX_888;
			break;
		case 1:
			converter_caps->max_color_depth =
					COLOR_DEPTH_INDEX_101010;
			break;
		case 2:
			converter_caps->max_color_depth =
					COLOR_DEPTH_INDEX_121212;
			break;
		case 3:
			converter_caps->max_color_depth =
					COLOR_DEPTH_INDEX_161616;
			break;
		}
	} else {
		/* 5.3.3.1 of DP1.2a specifies that Source should *not* impose
		 * any limits on modes when the converter has Format Conversion
		 * block. In effect, we should ignore the fact that converter
		 * exists and treat as a "no converter" case. */
		if (downstream_port->bits.FORMAT_CONV)
			return false;

		converter_caps->use_default_caps = true;
	}

	if (converter_caps->use_default_caps) {
		switch (converter_caps->port_detailed_type) {
		case DOWN_STREAM_DETAILED_DVI:
			converter_caps->max_tmds_clock = 330;
			break;
		case DOWN_STREAM_DETAILED_HDMI:
		default:
			converter_caps->max_tmds_clock = 300;
			break;
		}

		converter_caps->max_color_depth = COLOR_DEPTH_INDEX_121212;
	}

	return true;
}

static bool tmds_converter_capability_validate_mode_timing(
		struct dpsst_link_service *dpsst,
		const struct hw_crtc_timing *in_timing)
{
	uint32_t max_converter_pixel_clock_in_khz;
	uint32_t timing_bits_per_pixel;
	struct tmds_converter_capability *converter_caps =
			&dpsst->tmds_conv_capability;

	/* If there is no converter, or converter of an unsupported type,
	 * the timing passed validation
	 */
	if (!converter_caps->port_present ||
			converter_caps->port_type != DOWNSTREAM_DVI_HDMI)
		return true;

	ASSERT_CRITICAL(converter_caps->max_color_depth >=
			in_timing->flags.COLOR_DEPTH);

	/* Requested color depth higher than the converter can support */
	if (in_timing->flags.COLOR_DEPTH > converter_caps->max_color_depth)
		return true;

	switch (in_timing->flags.COLOR_DEPTH) {
	case CRTC_COLOR_DEPTH_666:
		timing_bits_per_pixel = 18;
		break;
	case CRTC_COLOR_DEPTH_888:
		timing_bits_per_pixel = 24;
		break;
	case CRTC_COLOR_DEPTH_101010:
		timing_bits_per_pixel = 30;
		break;
	case CRTC_COLOR_DEPTH_121212:
		timing_bits_per_pixel = 36;
		break;
	case CRTC_COLOR_DEPTH_141414:
		timing_bits_per_pixel = 42;
		break;
	case CRTC_COLOR_DEPTH_161616:
		timing_bits_per_pixel = 48;
		break;
	default:
		timing_bits_per_pixel = 24;
		break;
	}

	/* The calculation will be based on the following known data:
	 * 1. pInTiming->flags.colorDepth –
	 *	the color depth of the validated timing,
	 *	gives us Bits Per Pixel.
	 * 2. m_DviHdmiConverterCaps.im_maxTmdsClock –
	 *	the maximum TMDS clock of the converter
	 *	(was read from converter's DPCD). */

	max_converter_pixel_clock_in_khz = converter_caps->max_tmds_clock *
			24 / timing_bits_per_pixel;
	max_converter_pixel_clock_in_khz *= 1000; /* MHz-->KHz */

	if (max_converter_pixel_clock_in_khz > in_timing->pixel_clock)
		return true; /* Timing supported */
	else
		return false; /* Timing not supported */
}

static bool validate_mode_timing(struct link_service *ls,
	uint32_t display_index,
	const struct hw_crtc_timing *timing,
	struct link_validation_flags flags)
{
	uint32_t req_bw;
	uint32_t max_bw;

	struct dpsst_link_service *dpsst;
	const struct link_settings *link_setting;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/*always DP fail safe mode*/
	if (timing->pixel_clock == (uint32_t)25175 &&
		timing->h_addressable == (uint32_t)640 &&
		timing->v_addressable == (uint32_t)480)
		return true;

	/* For static validation we always use reported
	 * link settings for other cases, when no modelist
	 * changed we can use verified link setting*/
	link_setting = &dpsst->reported_link_cap;

	if (flags.DYNAMIC_VALIDATION == 1 &&
		dpsst->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN)
		link_setting = &dpsst->verified_link_cap;

	if (false == tmds_converter_capability_validate_mode_timing(
			dpsst, timing))
		return false;


	req_bw = dal_dpsst_ls_bandwidth_in_kbps_from_timing(timing);
	max_bw =
		dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(link_setting);

	if (req_bw < max_bw) {
		/* remember the biggest mode here, during
		 * initial link training (to get
		 * verified_link_cap), LS sends event about
		 * cannot train at reported cap to upper
		 * layer and upper layer will re-enumerate modes.
		 * this is not necessary if the lower
		 * verified_link_cap is enough to drive
		 * all the modes */

		if (flags.DYNAMIC_VALIDATION == 1)
			dpsst->max_req_bw_for_verified_linkcap = dal_max(
				dpsst->max_req_bw_for_verified_linkcap, req_bw);
		return true;
	} else
		return false;
}

static bool get_gtc_sync_status(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->gtc_status == GTC_SYNC_STATUS_SYNC_MAINTENANCE)
		return true;

	return false;
}

static void set_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);

	dpsst->psr_caps = *psr_caps;
}

static void get_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);

	*psr_caps = dpsst->psr_caps;
}

static bool is_link_psr_supported(struct link_service *ls)
{
	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);

	/* Only eDP supports PSR. During retrieve Sink capabilities, the PSR
	 * version and capabilities are cached. */
	return (dpsst->psr_caps.psr_version != 0);
}

static bool is_stream_drr_supported(struct link_service *ls)
{
	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);

	/* This does not necessarily mean DRR feature is supported, but it
	 * indicates stream can support DRR since the video frame can render
	 * without valid MSA params. Other DRR EDID requirements still apply */
	return (dpsst->dpcd_caps.allow_invalid_MSA_timing_param != 0);
}

static void update_dynamic_psr_caps(struct link_service *ls,
		const struct hw_crtc_timing *timing)
{
	uint32_t num_vblank_lines = timing->v_total -
			timing->v_addressable -
			timing->v_overscan_top -
			timing->v_overscan_bottom;
	uint32_t vblank_pixels = timing->h_total * num_vblank_lines;
	uint32_t vblank_time_in_us = (vblank_pixels * 1000) /
			timing->pixel_clock;

	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);
	if (dpsst->psr_caps.psr_rfb_setup_time > vblank_time_in_us) {
		dpsst->psr_caps.psr_sdp_transmit_line_num_deadline =
				num_vblank_lines;
		dpsst->psr_caps.psr_frame_capture_indication_req = true;
	} else {
		uint32_t sdp_transmit_deadline_in_us = vblank_time_in_us -
				dpsst->psr_caps.psr_rfb_setup_time;
		/* +1 to round up to next line */
		uint32_t line_time_in_us = ((timing->h_total * 1000) /
				timing->pixel_clock) + 1;

		dpsst->psr_caps.psr_sdp_transmit_line_num_deadline =
				sdp_transmit_deadline_in_us / line_time_in_us;
		dpsst->psr_caps.psr_frame_capture_indication_req = false;
	}
}

static void update_sink_psr_dpcd_config(struct link_service *ls)
{
	union dpcd_psr_configuration psr_configuration;
	struct dpsst_link_service *dpsst =
			container_of(ls,
					struct dpsst_link_service,
					link_service);

	dal_memset(&psr_configuration, '\0',
			sizeof(union dpcd_psr_configuration));
	psr_configuration.bits.ENABLE = dal_ls_is_link_psr_supported(ls);
	psr_configuration.bits.CRC_VERIFICATION = 1;
	psr_configuration.bits.FRAME_CAPTURE_INDICATION =
			dpsst->psr_caps.psr_frame_capture_indication_req;

	/* Check for PSR v2 */
	if (dpsst->psr_caps.psr_version == 0x2) {
		/* For PSR v2, selective update. Indicates whether sink should
		 * start capturing immediately following active scan line, or
		 * starting with the 2nd active scan line. */
		psr_configuration.bits.LINE_CAPTURE_INDICATION = 0;

		/* For PSR v2, determines whether Sink should generate IRQ_HPD
		 * when CRC mismatch is detected. */
		psr_configuration.bits.IRQ_HPD_WITH_CRC_ERROR = 1;
	}

	dal_dpsst_ls_write_dpcd_data(ls, DPCD_ADDRESS_PSR_ENABLE_CFG,
			&psr_configuration.raw, sizeof(psr_configuration.raw));
}

static void disable_mst_mode(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	union mstm_cntl mstm_cntl;

	dal_memset(&mstm_cntl, '\0', sizeof(union mstm_cntl));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->dpcd_caps.dpcd_rev.bits.MAJOR == 1 &&
			dpsst->dpcd_caps.dpcd_rev.bits.MINOR >= 2) {
		dal_dpsst_ls_write_dpcd_data(ls,
				DPCD_ADDRESS_MSTM_CNTL,
				&mstm_cntl.raw, 1);

	}
}

static void retrieve_link_setting(struct link_service *ls,
	struct link_settings *link_setting)
{
	bool lane_status_ok;
	union lane_count_set lane_count_set;
	union lane_status status_01;
	union lane_status status_23;
	uint8_t dpcd_set[2];
	uint8_t dpcd_status[2];
	uint8_t link_bw_set;

	lane_status_ok = false;
	dal_memset(dpcd_set, '\0', sizeof(dpcd_set));
	dal_memset(dpcd_status, '\0', sizeof(dpcd_status));
	dal_memset(&lane_count_set, '\0', sizeof(lane_count_set));
	dal_memset(&status_01, '\0', sizeof(status_01));
	dal_memset(&status_23, '\0', sizeof(status_23));

	dal_dpsst_ls_read_dpcd_data(ls,
					DPCD_ADDRESS_LINK_BW_SET,
					dpcd_set, 2);

	link_bw_set = dpcd_set[0];
	lane_count_set.raw = dpcd_set[1];

	dal_dpsst_ls_read_dpcd_data(ls,
					DPCD_ADDRESS_LANE_01_STATUS,
					dpcd_status, 2);

	status_01.raw = dpcd_status[0];
	status_23.raw = dpcd_status[1];

	switch (lane_count_set.bits.LANE_COUNT_SET) {
	case 4:
		if (status_23.bits.CR_DONE_0 != 1 ||
			status_23.bits.CHANNEL_EQ_DONE_0 != 1 ||
			status_23.bits.SYMBOL_LOCKED_0 != 1 ||
			status_23.bits.CR_DONE_1 != 1 ||
			status_23.bits.CHANNEL_EQ_DONE_1 != 1 ||
			status_23.bits.SYMBOL_LOCKED_1 != 1)
			break;
	case 2:
		if (status_01.bits.CR_DONE_1 != 1 ||
			status_01.bits.CHANNEL_EQ_DONE_1 != 1 ||
			status_01.bits.SYMBOL_LOCKED_1 != 1)
			break;
	case 1:
		if (status_01.bits.CR_DONE_0 != 1 ||
			status_01.bits.CHANNEL_EQ_DONE_0 != 1 ||
			status_01.bits.SYMBOL_LOCKED_0 != 1)
			break;

		lane_status_ok = true;
		break;
	default:
		break;

	}
	if (lane_status_ok) {
		link_setting->lane_count = lane_count_set.bits.LANE_COUNT_SET;
		link_setting->link_rate = link_bw_set;
	} else {
		link_setting->lane_count = LANE_COUNT_UNKNOWN;
		link_setting->link_rate = LINK_RATE_UNKNOWN;
	}

}

static void retrieve_psr_link_cap(
		struct link_service *ls,
		enum dpcd_edp_revision edp_revision)
{
	struct dpsst_link_service *dpsst =
		container_of(ls, struct dpsst_link_service, link_service);

	/* PSR related capabilities. Check is PSR feature is enabled first.
	 * PSR is only supported on eDP revision 1.3 and above */
	if (dal_adapter_service_is_feature_supported(FEATURE_PSR_ENABLE) &&
			(edp_revision >= DPCD_EDP_REVISION_EDP_V1_3)) {
		/* Read PSR capabilities from DPCD */
		dal_dpsst_ls_read_dpcd_data(ls,
			DPCD_ADDRESS_PSR_SUPPORT_VER,
			(uint8_t *)(&dpsst->psr_caps.psr_version),
			sizeof(dpsst->psr_caps.psr_version));

		/* According to VESA eDP 1.3 spec, bits 7:0 is the PSR
		 * support capability and version information. */
		if (dpsst->psr_caps.psr_version != 0) {
			union psr_capabilities psr_capabilities;

			dal_dpsst_ls_read_dpcd_data(ls,
				DPCD_ADDRESS_PSR_CAPABILITY,
				(uint8_t *)(&psr_capabilities),
				sizeof(psr_capabilities));

			dpsst->psr_caps.psr_exit_link_training_req =
				(psr_capabilities.bits.EXIT_LT_NOT_REQ == 0);

			/* In eDP spec, RFB Setup Time defines the following
			 * setup times based on bits 3:1 of PSR_CAPABILITIES:
			 *  0: 330us
			 *  1: 275us
			 *  2: 220us
			 *  3: 165us
			 *  4: 110us
			 *  5: 55us
			 *  6: 0us
			 *  7: reserved
			 */
			dpsst->psr_caps.psr_rfb_setup_time = 55 *
				(6 - psr_capabilities.bits.RFB_SETUP_TIME);
		}
	}

	/* For PSR test support. This code enables PSR on non-supported panels
	 * through runtime parameters. */
	if (dal_adapter_service_is_feature_supported(FEATURE_FORCE_PSR)) {
		union psr_capabilities psr_capabilities;

		dpsst->psr_caps.psr_version = 1;

		if (dal_adapter_service_get_feature_value(
				FEATURE_PSR_SETUP_TIME_TEST,
				&psr_capabilities.raw,
				sizeof(psr_capabilities.raw))) {
			/* Let test override setting control PSR caps. */
			dpsst->psr_caps.psr_exit_link_training_req =
				(psr_capabilities.bits.EXIT_LT_NOT_REQ == 0);
			dpsst->psr_caps.psr_rfb_setup_time = 55 *
				(6 - psr_capabilities.bits.RFB_SETUP_TIME);
		}
	}
}

static void retrieve_link_cap(struct link_service *ls)
{
	uint8_t dpcd_data[
			DPCD_ADDRESS_EDP_CONFIG_CAP -
			DPCD_ADDRESS_DPCD_REV + 1];

	struct dpsst_link_service *dpsst;
	union down_stream_port_count down_strm_port_count;
	union edp_configuration_cap edp_config_cap;
	union max_down_spread max_down_spread;

	dal_memset(dpcd_data, '\0', sizeof(dpcd_data));
	dal_memset(&down_strm_port_count,
		'\0', sizeof(union down_stream_port_count));
	dal_memset(&edp_config_cap, '\0',
		sizeof(union edp_configuration_cap));
	dal_memset(&max_down_spread, '\0',
		sizeof(union max_down_spread));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	dal_dpsst_ls_read_dpcd_data(ls,
		DPCD_ADDRESS_DPCD_REV,
		dpcd_data, sizeof(dpcd_data));

	dpsst->dpcd_caps.dpcd_rev.raw = dpcd_data[
		DPCD_ADDRESS_DPCD_REV -
		DPCD_ADDRESS_DPCD_REV];

	dpsst->down_str_port.raw = dpcd_data[
		DPCD_ADDRESS_DOWNSTREAM_PORT_PRESENT -
		DPCD_ADDRESS_DPCD_REV];

	down_strm_port_count.raw = dpcd_data[
		DPCD_ADDRESS_DOWNSTREAM_PORT_COUNT -
		DPCD_ADDRESS_DPCD_REV];
	dpsst->dpcd_caps.allow_invalid_MSA_timing_param =
		down_strm_port_count.bits.IGNORE_MSA_TIMING_PARAM;

	dpsst->dpcd_caps.max_ln_count.raw = dpcd_data[
		DPCD_ADDRESS_MAX_LANE_COUNT - DPCD_ADDRESS_DPCD_REV];

	max_down_spread.raw = dpcd_data[
		DPCD_ADDRESS_MAX_DOWNSPREAD - DPCD_ADDRESS_DPCD_REV];

	dal_dpsst_ls_read_dpcd_data(ls,
		DPCD_ADDRESS_NUM_OF_AUDIO_ENDPOINTS,
		&dpsst->num_of_audio_endpoint,
		sizeof(uint8_t));

	dpsst->reported_link_cap.lane_count =
		dpsst->dpcd_caps.max_ln_count.bits.MAX_LANE_COUNT;
	dpsst->reported_link_cap.link_rate = dpcd_data[
		DPCD_ADDRESS_MAX_LINK_RATE - DPCD_ADDRESS_DPCD_REV];
	dpsst->reported_link_cap.link_spread =
		max_down_spread.bits.MAX_DOWN_SPREAD ?
		LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	if (dpsst->prev_sink_count == UNKNOWN_SINK_COUNT) {

		uint8_t sink_count = 0;

		dal_dpsst_ls_read_dpcd_data(ls,
			DPCD_ADDRESS_SINK_COUNT,
			&sink_count,
			sizeof(uint8_t));
		dpsst->prev_sink_count = sink_count;

	}

	edp_config_cap.raw = dpcd_data[
		DPCD_ADDRESS_EDP_CONFIG_CAP - DPCD_ADDRESS_DPCD_REV];
	dpsst->dpcd_caps.alt_scrambler_supported =
		edp_config_cap.bits.ALT_SCRAMBLER_RESET;

	dpsst->edp_revision = DPCD_EDP_REVISION_EDP_UNKNOWN;
	/* Display control registers starting at DPCD 700h are only valid and
	 * enabled if this eDP config cap bit is set. */
	if (edp_config_cap.bits.DPCD_DISPLAY_CONTROL_CAPABLE) {
		/* Read the Panel's eDP revision at DPCD 700h. */
		dal_dpsst_ls_read_dpcd_data(ls,
			DPCD_ADDRESS_EDP_REV,
			(uint8_t *)(&dpsst->edp_revision),
			sizeof(dpsst->edp_revision));
	}

	retrieve_psr_link_cap(ls, dpsst->edp_revision);
}

static bool enable_stream(struct link_service *ls, uint32_t display_index,
	struct hw_path_mode *path_mode)
{
	struct dpsst_link_service *dpsst;
	bool success;

	success = false;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if ((ls->strm_state != STREAM_STATE_DISABLED) ||
		(ls->strm_state != STREAM_STATE_OPTIMIZED_READY) ||
		(ls->strm_state != STREAM_STATE_POWER_SAVE))
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"%s: Link service stream state is invalid: %d\n",
			__func__, ls->strm_state);


	if ((ls->strm_state == STREAM_STATE_ACTIVE) ||
		(ls->strm_state == STREAM_STATE_ENABLED))
		return true;

	if (ls->strm_state == STREAM_STATE_OPTIMIZED_READY) {

		dal_ls_try_enable_link_base(
			ls,
			path_mode,
			&ls->cur_link_setting);

		dal_ls_try_enable_stream(
			ls,
			path_mode,
			&ls->cur_link_setting);

		dal_ls_update_stream_features(
			ls,
			path_mode);

		return true;
	}

	if (!ls->link_st.FAKE_CONNECTED) {
		disable_mst_mode(ls);

		if (dpsst->verified_link_cap.lane_count ==
			LANE_COUNT_UNKNOWN)
			dal_dpsst_ls_verify_link_cap(
				ls,
				path_mode,
				&dpsst->verified_link_cap);

		/* There is one test failing in compliance with the DPR-120 box
		 * (400.3.1.1). This happens because DPR-120 box brings HPD low
		 * immediately once link training succeeds and we disable/enable
		 * link after that, still issuing commands. It looks like in the
		 * 2.5ms that would be considered a short pulse, the DPR-120 box
		 * still replies to commands and it looks like it gets confused
		 * thinking a new link training begins
		 */
		dal_sleep_in_milliseconds(10);

		success = dal_dpsst_ls_try_enable_link_with_hbr2_fallback(
			ls, path_mode);
		dal_ls_try_enable_stream(ls, path_mode,
			&ls->cur_link_setting);
	}

	dal_ls_update_stream_features(
		ls,
		path_mode);

	ls->strm_state = STREAM_STATE_ENABLED;

	dal_display_path_set_target_powered_on(
		path_mode->display_path,
		DISPLAY_TRI_STATE_TRUE);
	return success;
}

static bool disable_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode)
{
	struct enable_stream_param stream_params;

	dal_memset(&stream_params, '\0', sizeof(struct enable_stream_param));

	if (ls->strm_state != STREAM_STATE_ENABLED &&
		ls->strm_state != STREAM_STATE_OPTIMIZED_READY &&
		ls->strm_state != STREAM_STATE_POWER_SAVE)
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"%s: Link service stream state (on display index %d) is invalid: %d\n",
			__func__,
			display_index,
			ls->strm_state);

	if (ls->strm_state == STREAM_STATE_DISABLED ||
		ls->strm_state == STREAM_STATE_POWER_SAVE)
		return true;

	dal_dpsst_ls_set_test_pattern(
		ls,
		path_mode,
		DP_TEST_PATTERN_VIDEO_MODE,
		0,
		0,
		0);

	return dal_ls_disable_stream_base(ls, display_index, path_mode);
}

static bool optimized_enable_stream(struct link_service *ls,
	uint32_t display_index,
	struct display_path *display_path)
{
	struct link_settings link_setting = {0};
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	ls->cur_link_setting.link_rate = LINK_RATE_HIGH;
	ls->cur_link_setting.link_spread = LINK_SPREAD_DISABLED;
	ls->cur_link_setting.lane_count = LANE_COUNT_FOUR;

	if (!ls->link_st.FAKE_CONNECTED) {

		retrieve_link_setting(ls, &link_setting);

		if (link_setting.lane_count != LANE_COUNT_UNKNOWN
			&& link_setting.link_rate != LINK_RATE_UNKNOWN) {

			ASSERT(dpsst->verified_link_cap.lane_count !=
			LANE_COUNT_UNKNOWN);

			ls->cur_link_setting = link_setting;
			ls->strm_state = STREAM_STATE_OPTIMIZED_READY;

			/*video stream enabled*/
			dal_display_path_set_target_powered_on(
			display_path,
			DISPLAY_TRI_STATE_TRUE);
			/*video stream unblanked*/
			dal_display_path_set_target_blanked(
			display_path,
			DISPLAY_TRI_STATE_FALSE);
			return true;
		}

	}
	return false;
}

static void program_drr(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	union down_spread_ctrl downspread_read;
	union down_spread_ctrl downspread_write;

	/* Actual HW programming to enable/disable the DRR feature.
	 * Must be done after CRTC programmed. Also needs to be programmed for
	 * the case where optimization skips full timing reprogramming. */
	dal_hw_sequencer_program_drr(ls->hwss, path_mode);

	/* Update DPCD 107h (downspread_cntl bit), which allows sending
	 * invalid MSA timing parameters. Normal use case scenario is when DRR
	 * is enabled and will call this interface to update this bit.
	 * - DRR enabled > Program bit to ifnore MSA in Sink.
	 * - DRR disabled > Program bit to use MSA in Sink. */

	if (dal_dpsst_ls_read_dpcd_data(ls,
		DPCD_ADDRESS_DOWNSPREAD_CNTL,
		&downspread_read.raw,
		sizeof(downspread_read)) !=
			DDC_RESULT_SUCESSFULL)
			return;

	downspread_write.raw = downspread_read.raw;

	if ((path_mode->mode.timing.ranged_timing.vertical_total_min !=
		path_mode->mode.timing.ranged_timing.vertical_total_max) &&
		(!path_mode->mode.timing.ranged_timing.control.force_disable_drr))
		downspread_write.bits.IGNORE_MSA_TIMING_PARAM = 1;
	else
		downspread_write.bits.IGNORE_MSA_TIMING_PARAM = 0;

	/* This is a wordk-around. Screen flashes if link training DPCD is
	 * writing when link is stable. */
	if (downspread_write.raw != downspread_read.raw)
		dal_dpsst_ls_write_dpcd_data(ls, DPCD_ADDRESS_DOWNSPREAD_CNTL,
			&downspread_write.raw, sizeof(downspread_write));
}

static void update_stream_features(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	if (dal_ls_is_link_psr_supported(ls)) {
		struct dpsst_link_service *dpsst =
				container_of(ls,
						struct dpsst_link_service,
						link_service);

		update_dynamic_psr_caps(ls, &path_mode->mode.timing);

		update_sink_psr_dpcd_config(ls);

		dal_hw_sequencer_psr_setup(ls->hwss, path_mode,
				&dpsst->psr_caps);

		dal_hw_sequencer_psr_enable(ls->hwss, path_mode->display_path);
	} else {
		program_drr(ls, path_mode);
	}
}

static void dpcd_set_amd_tx_signature(struct link_service *ls)
{
	int8_t amd_signature_buffer[] = {
		DPCD_AMD_IEEE_TX_SIGNATURE_BYTE1,
		DPCD_AMD_IEEE_TX_SIGNATURE_BYTE2,
		DPCD_AMD_IEEE_TX_SIGNATURE_BYTE3
	};

	dal_dpsst_ls_write_dpcd_data(
	ls,
	DPCD_ADDRESS_SOURCE_DEVICE_ID_START,
	amd_signature_buffer,
	sizeof(amd_signature_buffer));

}

static void get_rx_signature(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	struct dp_device_vendor_id sink_id;
	struct dp_device_vendor_id branch_id;
	uint32_t branch_dev_id;

	dal_memset(&sink_id, '\0', sizeof(struct dp_device_vendor_id));
	dal_memset(&branch_id, '\0', sizeof(struct dp_device_vendor_id));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/* read IEEE sink device id*/
	dal_dpsst_ls_read_dpcd_data(ls,
	DPCD_ADDRESS_SINK_DEVICE_ID_START,
	(uint8_t *)(&sink_id),
	sizeof(sink_id));

	/* cache the sinkDevId*/
	dpsst->dpcd_caps.sink_dev_id = (sink_id.ieee_oui[0] << 16) |
	(sink_id.ieee_oui[1] << 8) | sink_id.ieee_oui[2];

	/* read IEEE branch device id*/
	dal_dpsst_ls_read_dpcd_data(
	ls,
	DPCD_ADDRESS_BRANCH_DEVICE_ID_START,
	(uint8_t *)(&branch_id),
	sizeof(branch_id));

	/* extract IEEE id*/
	branch_dev_id = (branch_id.ieee_oui[0] << 16) |
	(branch_id.ieee_oui[1] << 8) | branch_id.ieee_oui[2];

	/* cache the branch_dev_id and branchDevName*/
	dpsst->dpcd_caps.branch_dev_id = branch_dev_id;

	dal_memmove(
	dpsst->dpcd_caps.branch_dev_name,
	branch_id.ieee_device_id,
	sizeof(dpsst->dpcd_caps.branch_dev_name));

}

static const struct link_settings *get_bandwidth_priority_table(
					struct link_service *ls,
					uint32_t i)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->reported_link_cap.link_rate == LINK_RATE_RBR2)
		return &bandwidth_priority_table_rbr2[i];
	else
		return &bandwidth_priority_table[i];
}

static const uint32_t get_bandwidth_priority_table_len(
	struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->reported_link_cap.link_rate == LINK_RATE_RBR2)
		return ARRAY_SIZE(bandwidth_priority_table_rbr2);
	else
		return ARRAY_SIZE(bandwidth_priority_table);
}

static const struct link_settings *get_link_training_fallback_table(
					struct link_service *ls,
					uint32_t i)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->reported_link_cap.link_rate == LINK_RATE_RBR2)
		return &link_training_fallback_table_rbr2[i];
	else
		return &link_training_fallback_table[i];

}

static const uint32_t get_link_training_fallback_table_len(
	struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->reported_link_cap.link_rate == LINK_RATE_RBR2)
		return ARRAY_SIZE(link_training_fallback_table_rbr2);
	else
		return ARRAY_SIZE(link_training_fallback_table);

}

static bool validate_link(struct link_service *ls,
	const struct display_path *display_path,
	const struct link_settings *link_setting)
{
	enum hwss_result result;
	struct validate_link_param param = {0};

	dal_memset(&param, '\0', sizeof(struct validate_link_param));

	param.display_path = display_path;
	param.link_idx = ls->link_idx;
	param.link_settings = *link_setting;

	result = dal_hw_sequencer_validate_link(
			ls->hwss,
			&param);

	return (result == HWSS_RESULT_OK);
}

static bool is_link_setting_supported(
	struct link_service *ls,
	const struct display_path *display_path,
	const struct link_settings *link_setting,
	const struct link_settings *max_link_setting)
{
	if (link_setting->lane_count >
		max_link_setting->lane_count ||
		link_setting->link_rate >
		max_link_setting->link_rate)
		return false;

	return validate_link(
		ls,
		display_path,
		link_setting);
}

static bool get_converter_capability(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);
	/* The converterCaps.regDwnStrmPort.bits.formatConv
	 * and converterCaps.regDwnStrmPort.bits.detailedCaps are
	 * mutually exclusive - only one or the other can be set.
	 * That means:
	 * if converterCaps.regDwnStrmPort.bits.formatConv == 1,
	 * then converter will *not* have detailed
	 * capabilities, and will be considered as "no converter" case.
	 * Independently of availability of detailed caps, byte 0 of the
	 * 4-byte range at DpcdAddress_DwnStrmPort0Caps is valid. */
	/* Read capabilities of the converter. */

	dal_dpsst_ls_read_dpcd_data(
	ls,
	DPCD_ADDRESS_DWN_STRM_PORT0_CAPS,
	dpsst->down_stream_caps.raw,
	sizeof(union dwnstream_portx_caps));
	tmds_converter_capability_translate(dpsst, &dpsst->down_str_port,
			&dpsst->down_stream_caps);

	return true;

}
/************************************************************/
static void handle_sst_hpd_irq(void *interrupt_params);

static void handle_hpd_irq_downstream_port_status_change(
	struct link_service *ls,
	union hpd_irq_data *irq_data);
/*************************************************************/

static void register_dp_sink_interrupt(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	struct dal_interrupt_params int_params = {0};
	struct dal_context *dal_context = ls->dal_context;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = dpsst->dp_sink_irq_info.src;
	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.no_mutex_wait = false;
	int_params.one_shot = false;

	if (dpsst->dp_sink_irq_info.handler_idx ==
			DAL_INVALID_IRQ_HANDLER_IDX) {
		dpsst->dp_sink_irq_info.handler_idx =
				dal_register_interrupt(dal_context,
						&int_params,
						handle_sst_hpd_irq,
						ls);
	}
}

static void unregister_dp_sink_interrupt(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	struct dal_context *dal_context = ls->dal_context;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	ASSERT(dpsst->dp_sink_irq_info.src != DAL_IRQ_SOURCE_INVALID);
	if (dpsst->dp_sink_irq_info.handler_idx !=
			DAL_INVALID_IRQ_HANDLER_IDX) {

		dal_unregister_interrupt(
				dal_context,
				dpsst->dp_sink_irq_info.src,
				dpsst->dp_sink_irq_info.handler_idx);

		dpsst->dp_sink_irq_info.handler_idx =
				DAL_INVALID_IRQ_HANDLER_IDX;

	}
}

static void connect_link(
	struct link_service *ls,
	const struct display_path *display_path,
	bool initial_detection)
{
	int32_t i;
	const struct link_settings *cur_ls;
	struct link_settings invalid;
	struct dpsst_link_service *dpsst;

	invalid.lane_count = LANE_COUNT_UNKNOWN;
	invalid.link_rate = LINK_RATE_UNKNOWN;
	invalid.link_spread = LINK_SPREAD_DISABLED;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (ls->link_st.CONNECTED == 1 &&
		ls->link_st.INVALIDATED == 0)
		return;

	/* if returning from S3, the previous value of sink count
	 * is not valid anymore */
	if (ls->link_st.INVALIDATED)
		dpsst->prev_sink_count = UNKNOWN_SINK_COUNT;

	if (!ls->link_prop.INTERNAL ||
		(ls->link_prop.INTERNAL &&
		ls->link_st.SINK_CAP_CONSTANT != 1)) {

		dpcd_set_amd_tx_signature(ls);
		get_rx_signature(ls);
		retrieve_link_cap(ls);

		/* downgrade max link setting if ASIC cannot
		 * support reported
		 */
		for (i = get_bandwidth_priority_table_len(ls) - 1;
			i >= 0; i--) {

			cur_ls = get_bandwidth_priority_table(ls, i);
			if (is_link_setting_supported(
				ls,
				display_path,
				cur_ls,
				&dpsst->reported_link_cap)) {
				dpsst->max_link_setting = *cur_ls;
				break;
			}
		}
		get_converter_capability(ls);

	}
	if (ls->link_prop.INTERNAL) {
		dpsst->verified_link_cap = dpsst->max_link_setting;
		ls->link_st.SINK_CAP_CONSTANT = 1;
	} else
		dpsst->verified_link_cap = invalid;


	ls->link_st.CONNECTED = 1;
	ls->link_st.INVALIDATED = 0;
	dpsst->teststate.raw = 0;

	if (!ls->link_prop.INTERNAL)
		register_dp_sink_interrupt(ls);

}

static void disconnect_link(struct link_service *ls)
{
	struct link_settings invalid = {LANE_COUNT_UNKNOWN};
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (!ls->link_prop.INTERNAL) {
		dpsst->reported_link_cap = invalid;
		dpsst->verified_link_cap = invalid;
		dpsst->max_link_setting = invalid;
		dpsst->preferred_link_setting = invalid;
	}

	ls->link_st.CONNECTED = 0;
	dpsst->prev_sink_count = UNKNOWN_SINK_COUNT;

	if (!ls->link_prop.INTERNAL)
		unregister_dp_sink_interrupt(ls);
}

static void release_hw(struct link_service *ls)
{
	unregister_dp_sink_interrupt(ls);
}

static bool associate_link(struct link_service *ls,
	uint32_t display_index,
	uint32_t link_index,
	bool is_internal_link)
{
	ls->link_idx = link_index;
	ls->link_notification_display_index = display_index;
	ls->link_prop.INTERNAL = is_internal_link;

	if (ls->link_prop.INTERNAL)
		register_dp_sink_interrupt(ls);

	return true;
}
/*TODO: when MstMgr calls this function,
* we should update function pointer
* to make it point to MstMgr's implementation
* of this function.
*/
static void decide_link_settings(struct link_service *ls,
	const struct hw_path_mode *path_mode,
	struct link_settings *link_setting)
{
	struct dpsst_link_service *dpsst;
	const struct link_settings *cur_ls;
	uint32_t req_bw;
	uint32_t link_bw;
	uint32_t i;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	req_bw = dal_dpsst_ls_bandwidth_in_kbps_from_timing(
			&path_mode->mode.timing);
	/* if preferred is specified through AMDDP, use it, if it's enough
	 * to drive the mode
	 */
	if ((dpsst->preferred_link_setting.lane_count !=
		LANE_COUNT_UNKNOWN) &&
		(dpsst->preferred_link_setting.link_rate <=
		dpsst->verified_link_cap.link_rate)) {

		link_bw =
		dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(
				&dpsst->preferred_link_setting);
		if (req_bw < link_bw) {
			*link_setting = dpsst->preferred_link_setting;
			return;
		}
	}

	/* search for first suitable setting for the requested
	 * bandwidth
	 */

	for (i = 0; i < get_bandwidth_priority_table_len(ls); i++) {
		cur_ls = get_bandwidth_priority_table(ls, i);
		link_bw =
			dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(
				cur_ls);
		if (req_bw < link_bw) {
			if (is_link_setting_supported(
				ls,
				path_mode->display_path,
				cur_ls,
				&dpsst->verified_link_cap)) {

				*link_setting = *cur_ls;
				return;
			}
		}
	}

	BREAK_TO_DEBUGGER();
	ASSERT(dpsst->verified_link_cap.lane_count !=
		LANE_COUNT_UNKNOWN);

	*link_setting = dpsst->verified_link_cap;
}

/*TODO: this function should be not have a func ptr
* sst implements this function and calls from this file
* mst will implement this function and call its function
* from its file.
*/
/*
static void send_lower_link_setting_applied(
	struct link_service *ls)
{
	*notify pending mode re-enumeration *
	*TODO:
	dal_tm_notify_lower_settings_applied(
	dal_ls_get_notification_display_index(ls));*
}
*/
static bool should_send_notification(
	struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	uint32_t max_supported_bw;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	max_supported_bw =
		dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(
		&dpsst->verified_link_cap);

	if (dpsst->max_req_bw_for_verified_linkcap >
		max_supported_bw) {

		/*this will be updated when we re-validate the modes*/
		dpsst->max_req_bw_for_verified_linkcap = 0;
		return true;
	}

	return false;
}

static enum dp_alt_scrambler_reset decide_assr(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/* We need to explicitly check that connector
	 * is not DP. Some Travis_VGA get reported
	 * by video bios as DP.
	 */
	if (ls->connector_enum_id != CONNECTOR_ID_DISPLAY_PORT) {

		switch (dpsst->dpcd_caps.branch_dev_id) {
		case DP_BRANCH_DEVICE_ID_2:
		/* alternate scrambler reset is required for Travis
		 * for the case when external chip does not
		 * provide sink device id, alternate scrambler
		 * scheme will  be overriden later by querying
		 * Encoder features
		 */
			if (dal_strncmp(
				dpsst->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_2,
				sizeof(
				dpsst->dpcd_caps.
				branch_dev_name)) == 0) {
				return DP_ALT_SCRAMBLER_RESET_SPECIAL;
			}
			break;
		case DP_BRANCH_DEVICE_ID_3:
		/* alternate scrambler reset is required for Travis
		 * for the case when external chip does not provide
		 * sink device id, alternate scrambler scheme will
		 * be overriden later by querying Encoder feature
		 */
			if (dal_strncmp(dpsst->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_3,
				sizeof(
				dpsst->dpcd_caps.
				branch_dev_name)) == 0) {
				return DP_ALT_SCRAMBLER_RESET_SPECIAL;
			}
			break;
		default:
			break;
		}

		if (dpsst->dpcd_caps.alt_scrambler_supported) {
			/* alternate scrambler reset capable sink,
			 * usually it is set on eDP sinks
			 * 1 indicates an eDP device that can use
			 * the alternate scramber reset value of FFFEh
			 */
			return DP_ALT_SCRAMBLER_RESET_STANDARD;
		}
	}

	return DP_ALT_SCRAMBLER_RESET_NONE;
}

static void dpcd_configure_assr(
	struct link_service *ls,
	enum dp_alt_scrambler_reset assr)
{
	union dpcd_edp_config edp_config_set;

	dal_memset(&edp_config_set, '\0', sizeof(union dpcd_edp_config));

	if (DP_ALT_SCRAMBLER_RESET_NONE != assr) {
		bool alt_scrambler_reset_enable = false;

		switch (assr) {
		case DP_ALT_SCRAMBLER_RESET_STANDARD:
		case DP_ALT_SCRAMBLER_RESET_SPECIAL:
			alt_scrambler_reset_enable = true;
			break;

		default:
			break;
		}

		/*set alternative scrambler reset in receiver*/
		dal_dpsst_ls_read_dpcd_data(
			ls,
			DPCD_ADDRESS_EDP_CONFIG_SET,
			&edp_config_set.raw,
			sizeof(edp_config_set.raw));

		if (edp_config_set.bits.ALT_SCRAMBLER_RESET_ENABLE
			!= alt_scrambler_reset_enable) {
			enum ddc_result result = DDC_RESULT_UNKNOWN;

			edp_config_set.bits.ALT_SCRAMBLER_RESET_ENABLE =
			alt_scrambler_reset_enable;
			result = dal_dpsst_ls_write_dpcd_data(
				ls,
				DPCD_ADDRESS_EDP_CONFIG_SET,
				&edp_config_set.raw,
				sizeof(edp_config_set.raw));

			ASSERT(result == DDC_RESULT_SUCESSFULL);
		}
	}
}

static uint8_t get_nibble_at_index(const uint8_t *buf,
	uint32_t index)
{
	uint8_t nibble;

	nibble = buf[index / 2];

	if (index % 2)
		nibble >>= 4;
	else
		nibble &= 0x0F;

	return nibble;
}

static bool is_cr_done(enum lane_count ln_count,
	union lane_status *dpcd_lane_status)
{
	bool done = true;
	uint32_t lane;
	/*LANEx_CR_DONE bits All 1's?*/
	for (lane = 0; lane <
	(uint32_t)(ln_count); lane++) {

		if (!dpcd_lane_status[lane].bits.CR_DONE_0)
			done = false;

	}
	return done;

}

static bool is_ch_eq_done(enum lane_count ln_count,
	union lane_status *dpcd_lane_status,
	union lane_align_status_updated *lane_status_updated)
{
	bool done = true;
	uint32_t lane;

	if (!lane_status_updated->bits.INTERLANE_ALIGN_DONE)
		done = false;
	else {
		for (lane = 0; lane <
		(uint32_t)(ln_count); lane++) {
			if (!dpcd_lane_status[lane].bits.SYMBOL_LOCKED_0 ||
				!dpcd_lane_status[lane].bits.CHANNEL_EQ_DONE_0)
				done = false;
		}
	}
	return done;

}

static bool is_tps3_supported(
	struct link_service *ls,
	struct display_path *display_path)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (!dal_hw_sequencer_is_supported_dp_training_pattern3(
			ls->hwss, display_path, ls->link_idx))
		return false;

	if (!dpsst->dpcd_caps.max_ln_count.bits.TPS3_SUPPORTED)
		return false;

	return true;
}

static enum pre_emphasis get_max_pre_emphasis_for_voltage_swing(
	enum voltage_swing voltage)
{
	enum pre_emphasis pre_emphasis;

	pre_emphasis = PRE_EMPHASIS_MAX_LEVEL;

	if (voltage <= VOLTAGE_SWING_MAX_LEVEL)
		pre_emphasis =
		voltage_swing_to_pre_emphasis[voltage];

	return pre_emphasis;

}

static void find_max_drive_settings(
	const struct link_training_settings *link_training_setting,
	struct link_training_settings *max_lt_setting)
{
	uint32_t lane;

	struct lane_settings max_requested;

	max_requested.VOLTAGE_SWING =
		link_training_setting->
		lane_settings[0].VOLTAGE_SWING;
	max_requested.PRE_EMPHASIS =
		link_training_setting->
		lane_settings[0].PRE_EMPHASIS;
	/*max_requested.postCursor2 =
	 * link_training_setting->laneSettings[0].postCursor2;*/

	/* Determine what the maximum of the requested settings are*/
	for (lane = 1; lane <
			link_training_setting->
			link_settings.lane_count;
			lane++) {
		if (link_training_setting->
			lane_settings[lane].VOLTAGE_SWING >
			max_requested.VOLTAGE_SWING)

			max_requested.VOLTAGE_SWING =
			link_training_setting->
			lane_settings[lane].VOLTAGE_SWING;


		if (link_training_setting->
				lane_settings[lane].PRE_EMPHASIS >
				max_requested.PRE_EMPHASIS)
			max_requested.PRE_EMPHASIS =
			link_training_setting->
			lane_settings[lane].PRE_EMPHASIS;


		/*
		if (link_training_setting->laneSettings[lane].postCursor2 >
		 max_requested.postCursor2)
		{
		max_requested.postCursor2 =
		link_training_setting->laneSettings[lane].postCursor2;
		}
		*/
	}

	/* make sure the requested settings are
	 * not higher than maximum settings*/
	if (max_requested.VOLTAGE_SWING > VOLTAGE_SWING_MAX_LEVEL)
		max_requested.VOLTAGE_SWING = VOLTAGE_SWING_MAX_LEVEL;

	if (max_requested.PRE_EMPHASIS > PRE_EMPHASIS_MAX_LEVEL)
		max_requested.PRE_EMPHASIS = PRE_EMPHASIS_MAX_LEVEL;
	/*
	if (max_requested.postCursor2 > PostCursor2_MaxLevel)
	max_requested.postCursor2 = PostCursor2_MaxLevel;
	*/

	/* make sure the pre-emphasis matches the voltage swing*/
	if (max_requested.PRE_EMPHASIS >
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING))
		max_requested.PRE_EMPHASIS =
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING);

	/*
	 * Post Cursor2 levels are completely independent from
	 * pre-emphasis (Post Cursor1) levels. But Post Cursor2 levels
	 * can only be applied to each allowable combination of voltage
	 * swing and pre-emphasis levels */
	 /* if ( max_requested.postCursor2 >
	  *  getMaxPostCursor2ForVoltageSwing(max_requested.voltageSwing))
	  *  max_requested.postCursor2 =
	  *  getMaxPostCursor2ForVoltageSwing(max_requested.voltageSwing);
	  */

	max_lt_setting->link_settings.link_rate =
		link_training_setting->link_settings.link_rate;
	max_lt_setting->link_settings.lane_count =
	link_training_setting->link_settings.lane_count;
	max_lt_setting->link_settings.link_spread =
		link_training_setting->link_settings.link_spread;

	for (lane = 0; lane <
		link_training_setting->link_settings.lane_count;
		lane++) {
		max_lt_setting->lane_settings[lane].VOLTAGE_SWING =
			max_requested.VOLTAGE_SWING;
		max_lt_setting->lane_settings[lane].PRE_EMPHASIS =
			max_requested.PRE_EMPHASIS;
		/*max_lt_setting->laneSettings[lane].postCursor2 =
		 * max_requested.postCursor2;
		 */
	}

}

static void update_drive_settings(
		struct link_training_settings *dest,
		struct link_training_settings src)
{
	uint32_t lane;

	for (lane = 0; lane <
		src.link_settings.lane_count; lane++) {

		dest->lane_settings[lane].VOLTAGE_SWING =
			src.lane_settings[lane].VOLTAGE_SWING;
		dest->lane_settings[lane].PRE_EMPHASIS =
			src.lane_settings[lane].PRE_EMPHASIS;
		dest->lane_settings[lane].POST_CURSOR2 =
			src.lane_settings[lane].POST_CURSOR2;
	}

}

static bool is_max_vs_reached(
	const struct link_training_settings *lt_settings)
{
	uint32_t lane;

	for (lane = 0; lane <
		(uint32_t)(lt_settings->link_settings.lane_count);
		lane++) {
		if (lt_settings->lane_settings[lane].VOLTAGE_SWING
			== VOLTAGE_SWING_MAX_LEVEL)
			return true;
	}
	return false;

}

static void get_lane_status_and_drive_settings(
	struct link_service *ls,
	const struct link_training_settings *link_training_setting,
	union lane_status *ln_status,
	union lane_align_status_updated *ln_status_updated,
	struct link_training_settings *req_settings)
{
	uint8_t dpcd_buf[6];
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX];
	struct link_training_settings request_settings;
	uint32_t lane;

	dal_memset(req_settings, '\0', sizeof(struct link_training_settings));
	dal_memset(dpcd_buf, '\0', sizeof(dpcd_buf));
	dal_memset(&dpcd_lane_adjust, '\0', sizeof(dpcd_lane_adjust));

	dal_dpsst_ls_read_dpcd_data(ls,
		DPCD_ADDRESS_LANE_01_STATUS,
		(uint8_t *)(dpcd_buf),
		sizeof(dpcd_buf));

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		ln_status[lane].raw =
			get_nibble_at_index(&dpcd_buf[0], lane);
		dpcd_lane_adjust[lane].raw =
			get_nibble_at_index(&dpcd_buf[4], lane);
	}

	ln_status_updated->raw = dpcd_buf[2];

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s:\n%x Lane01Status = %x\n %x Lane23Status = %x\n ",
		__func__,
		DPCD_ADDRESS_LANE_01_STATUS, dpcd_buf[0],
		DPCD_ADDRESS_LANE_23_STATUS, dpcd_buf[1]);

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s:\n %x Lane01AdjustRequest = %x\n %x Lane23AdjustRequest = %x\n",
		__func__,
		DPCD_ADDRESS_ADJUST_REQUEST_LANE0_1,
		dpcd_buf[4],
		DPCD_ADDRESS_ADJUST_REQUEST_LANE2_3,
		dpcd_buf[5]);

	/*copy to req_settings*/
	dal_memset(&request_settings, '\0',
	sizeof(struct link_training_settings));

	request_settings.link_settings.lane_count =
		link_training_setting->link_settings.lane_count;
	request_settings.link_settings.link_rate =
		link_training_setting->link_settings.link_rate;
	request_settings.link_settings.link_spread =
		link_training_setting->link_settings.link_spread;

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		request_settings.lane_settings[lane].VOLTAGE_SWING =
			(enum voltage_swing)(dpcd_lane_adjust[lane].bits.
				VOLTAGE_SWING_LANE);
		request_settings.lane_settings[lane].PRE_EMPHASIS =
			(enum pre_emphasis)(dpcd_lane_adjust[lane].bits.
				PRE_EMPHASIS_LANE);
	}

	/*Note: for postcursor2, read adjusted
	 * postcursor2 settings from*/
	/*DpcdAddress_AdjustRequestPostCursor2 =
	 *0x020C (not implemented yet)*/

	/* we find the maximum of the requested settings across all lanes*/
	/* and set this maximum for all lanes*/
	find_max_drive_settings(&request_settings, req_settings);

	/* if post cursor 2 is needed in the future,
	 * read DpcdAddress_AdjustRequestPostCursor2 = 0x020C
	 */

}

static void wait_for_training_aux_rd_interval(struct link_service *ls,
	uint32_t default_wait_in_micro_secs)
{
	uint8_t training_rd_interval;
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/* overwrite the delay if rev > 1.1*/
	if (dpsst->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
		/* DP 1.2 or later - retrieve delay through
		 * "DPCD_ADDR_TRAINING_AUX_RD_INTERVAL" register
		 */
		dal_dpsst_ls_read_dpcd_data(
			ls,
			DPCD_ADDRESS_TRAINING_AUX_RD_INTERVAL,
			&training_rd_interval,
			sizeof(training_rd_interval));
		default_wait_in_micro_secs = training_rd_interval ?
			(training_rd_interval * 4000) :
			default_wait_in_micro_secs;
	}

	dal_delay_in_microseconds(default_wait_in_micro_secs);

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s:\n wait = %d\n",
		__func__,
		default_wait_in_micro_secs);

}

static void dpcd_set_link_settings(
	struct link_service *ls,
	const struct link_training_settings *lt_settings)
{
	uint8_t rate = (uint8_t)
	(lt_settings->link_settings.link_rate);

	union down_spread_ctrl downspread;
	union lane_count_set lane_count_set;
	struct dpsst_link_service *dpsst;
	uint8_t link_set_buffer[2];

	dal_memset(&downspread, '\0', sizeof(downspread));
	dal_memset(&lane_count_set, '\0', sizeof(lane_count_set));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	downspread.raw = (uint8_t)
	(lt_settings->link_settings.link_spread);

	lane_count_set.bits.LANE_COUNT_SET =
	lt_settings->link_settings.lane_count;

	lane_count_set.bits.ENHANCED_FRAMING = 1;
	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED =
	dpsst->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED;


	link_set_buffer[0] = rate;
	link_set_buffer[1] = lane_count_set.raw;

	dal_dpsst_ls_write_dpcd_data(ls, DPCD_ADDRESS_LINK_BW_SET,
	link_set_buffer, 2);
	dal_dpsst_ls_write_dpcd_data(ls, DPCD_ADDRESS_DOWNSPREAD_CNTL,
	&downspread.raw, sizeof(downspread));

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s\n %x rate = %x\n %x lane = %x\n %x spread = %x\n",
		__func__,
		DPCD_ADDRESS_LINK_BW_SET,
		lt_settings->link_settings.link_rate,
		DPCD_ADDRESS_LANE_COUNT_SET,
		lt_settings->link_settings.lane_count,
		DPCD_ADDRESS_DOWNSPREAD_CNTL,
		lt_settings->link_settings.link_spread);

}

static bool set_dp_phy_pattern(
	struct link_service *ls,
	struct display_path *display_path,
	enum dp_test_pattern test_pattern,
	const uint8_t *custom_pattern,
	uint32_t custom_pattern_size)
{
	struct set_dp_phy_pattern_param set_dp_phy_pattern_param = {0};
	enum hwss_result hwss_result;

	set_dp_phy_pattern_param.display_path = display_path;
	set_dp_phy_pattern_param.link_idx = ls->link_idx;
	set_dp_phy_pattern_param.test_pattern = test_pattern;
	set_dp_phy_pattern_param.custom_pattern = custom_pattern;
	set_dp_phy_pattern_param.cust_pattern_size = custom_pattern_size;
	set_dp_phy_pattern_param.alt_scrambler_reset = decide_assr(ls);

	hwss_result = dal_hw_sequencer_set_dp_phy_pattern(
	ls->hwss,
	&set_dp_phy_pattern_param);

	if (hwss_result != HWSS_RESULT_OK) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"Unexpected failure in %s", __func__);

		BREAK_TO_DEBUGGER();
		return false;
	}

	return true;

}

static void dpcd_set_training_pattern(
	struct link_service *ls,
	union dpcd_training_pattern dpcd_pattern)
{
	dal_dpsst_ls_write_dpcd_data(
	ls,
	DPCD_ADDRESS_TRAINING_PATTERN_SET,
	&dpcd_pattern.raw, 1);

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s\n %x pattern = %x\n",
		__func__,
		DPCD_ADDRESS_TRAINING_PATTERN_SET,
		dpcd_pattern.bits.TRAINING_PATTERN_SET);
}

static bool perform_post_lt_adj_req_sequence(
	struct link_service *ls,
	struct display_path *display_path,
	struct link_training_settings *lt_settings)
{
	enum lane_count lane_count =
	lt_settings->link_settings.lane_count;

	uint32_t adj_req_count;
	uint32_t adj_req_timer;
	bool req_drv_setting_changed;
	uint32_t lane;

	req_drv_setting_changed = false;
	for (adj_req_count = 0; adj_req_count < POST_LT_ADJ_REQ_LIMIT;
	adj_req_count++) {

		req_drv_setting_changed = false;

		for (adj_req_timer = 0;
			adj_req_timer < POST_LT_ADJ_REQ_TIMEOUT;
			adj_req_timer++) {

			struct link_training_settings req_settings;
			union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
			union lane_align_status_updated
				dpcd_lane_status_updated;

			get_lane_status_and_drive_settings(
			ls,
			lt_settings,
			dpcd_lane_status,
			&dpcd_lane_status_updated,
			&req_settings);

			if (dpcd_lane_status_updated.bits.
					POST_LT_ADJ_REQ_IN_PROGRESS == 0)
				return true;

			if (!is_cr_done(lane_count, dpcd_lane_status))
				return false;

			if (!is_ch_eq_done(
				lane_count,
				dpcd_lane_status,
				&dpcd_lane_status_updated))
				return false;

			for (lane = 0; lane < (uint32_t)(lane_count); lane++) {

				if (lt_settings->
				lane_settings[lane].VOLTAGE_SWING !=
				req_settings.lane_settings[lane].
				VOLTAGE_SWING ||
				lt_settings->lane_settings[lane].PRE_EMPHASIS !=
				req_settings.lane_settings[lane].PRE_EMPHASIS) {

					req_drv_setting_changed = true;
					break;
				}
			}

			if (req_drv_setting_changed) {
				update_drive_settings(
				lt_settings,
				req_settings);

				dal_dpsst_ls_set_drive_settings(
				ls, display_path, lt_settings);
				break;
			}

			dal_sleep_in_milliseconds(1);
		}

		if (!req_drv_setting_changed) {
			dal_logger_write(ls->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_LINK_SERVICE,
				"%s: Post Link Training Adjust Request Timed out\n",
				__func__);

			ASSERT(0);
			return true;
		}
	}
	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_WARNING,
		LOG_MINOR_COMPONENT_LINK_SERVICE,
		"%s: Post Link Training Adjust Request limit reached\n",
		__func__);

	ASSERT(0);
	return true;

}

static bool set_hw_training_pattern(struct link_service *ls,
	struct display_path *display_path,
	enum hw_dp_training_pattern pattern)
{
	enum dp_test_pattern test_pattern = DP_TEST_PATTERN_UNSUPPORTED;

	switch (pattern) {
	case HW_DP_TRAINING_PATTERN_1:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN1;
		break;
	case HW_DP_TRAINING_PATTERN_2:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN2;
		break;
	case HW_DP_TRAINING_PATTERN_3:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN3;
		break;
	default:
		break;
	}

	set_dp_phy_pattern(ls, display_path, test_pattern, NULL, 0);

	return true;

}

static enum dpcd_training_patterns
	hw_training_pattern_to_dpcd_training_pattern(
	struct link_service *ls,
	enum hw_dp_training_pattern pattern)
{
	enum dpcd_training_patterns dpcd_tr_pattern =
	DPCD_TRAINING_PATTERN_VIDEOIDLE;

	switch (pattern) {
	case HW_DP_TRAINING_PATTERN_1:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_1;
		break;
	case HW_DP_TRAINING_PATTERN_2:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_2;
		break;
	case HW_DP_TRAINING_PATTERN_3:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_3;
		break;
	default:
		ASSERT(0);
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_LINK_TRAINING,
			"%s: Invalid HW Training pattern: %d\n",
			__func__, pattern);
		break;
	}

	return dpcd_tr_pattern;

}

static void dpcd_set_lt_pattern_and_lane_settings(
	struct link_service *ls,
	const struct link_training_settings *lt_settings,
	enum hw_dp_training_pattern pattern)
{
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX];
	const uint32_t dpcd_base_lt_offset =
	DPCD_ADDRESS_TRAINING_PATTERN_SET;
	uint8_t dpcd_lt_buffer[5];
	union dpcd_training_pattern dpcd_pattern;
	struct dpsst_link_service *dpsst;
	uint32_t lane;

	dal_memset(&dpcd_lane, '\0', sizeof(dpcd_lane));
	dal_memset(dpcd_lt_buffer, '\0', sizeof(dpcd_lt_buffer));
	dal_memset(&dpcd_pattern, '\0', sizeof(dpcd_pattern));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/*****************************************************************
	* DpcdAddress_TrainingPatternSet
	*****************************************************************/
	dpcd_pattern.bits.TRAINING_PATTERN_SET =
	hw_training_pattern_to_dpcd_training_pattern(ls, pattern);

	dpcd_lt_buffer[
	DPCD_ADDRESS_TRAINING_PATTERN_SET - dpcd_base_lt_offset] =
	dpcd_pattern.raw;

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s\n %x pattern = %x\n",
		__func__,
		DPCD_ADDRESS_TRAINING_PATTERN_SET,
		dpcd_pattern.bits.TRAINING_PATTERN_SET);


	/*****************************************************************
	* DpcdAddress_Lane0Set -> DpcdAddress_Lane3Set
	*****************************************************************/
	for (lane = 0; lane <
		(uint32_t)(lt_settings->link_settings.lane_count); lane++) {

		dpcd_lane[lane].bits.VOLTAGE_SWING_SET =
		(uint8_t)(lt_settings->lane_settings[lane].VOLTAGE_SWING);
		dpcd_lane[lane].bits.PRE_EMPHASIS_SET =
		(uint8_t)(lt_settings->lane_settings[lane].PRE_EMPHASIS);

		dpcd_lane[lane].bits.MAX_SWING_REACHED =
		(lt_settings->lane_settings[lane].VOLTAGE_SWING ==
		VOLTAGE_SWING_MAX_LEVEL ? 1 : 0);
		dpcd_lane[lane].bits.MAX_PRE_EMPHASIS_REACHED =
		(lt_settings->lane_settings[lane].PRE_EMPHASIS ==
		PRE_EMPHASIS_MAX_LEVEL ? 1 : 0);
	}

	/* concatinate everything into one buffer*/
	dal_memmove(
	&dpcd_lt_buffer[DPCD_ADDRESS_LANE0_SET - dpcd_base_lt_offset],
	dpcd_lane,
	lt_settings->link_settings.lane_count);

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s:\n %x VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
		__func__,
		DPCD_ADDRESS_LANE0_SET,
		dpcd_lane[0].bits.VOLTAGE_SWING_SET,
		dpcd_lane[0].bits.PRE_EMPHASIS_SET,
		dpcd_lane[0].bits.MAX_SWING_REACHED,
		dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);


	if (ls->link_prop.INTERNAL) {
		/* for eDP write in 2 parts because the 5-byte burst is
		* causing issues on some eDP panels (EPR#366724)
		*/
		dal_dpsst_ls_write_dpcd_data(ls,
		DPCD_ADDRESS_TRAINING_PATTERN_SET,
		&dpcd_pattern.raw,
		sizeof(dpcd_pattern.raw));

		dal_dpsst_ls_write_dpcd_data(ls,
		DPCD_ADDRESS_LANE0_SET,
		(uint8_t *)(dpcd_lane),
		lt_settings->link_settings.lane_count);

		} else
		/* write it all in (1 + number-of-lanes)-byte burst*/
			dal_dpsst_ls_write_dpcd_data(ls,
			dpcd_base_lt_offset,
			dpcd_lt_buffer,
			1 + lt_settings->link_settings.lane_count);


	dpsst->ln_setting = lt_settings->lane_settings[0];

}


static void dpcd_set_lane_settings(
	struct link_service *ls,
	const struct link_training_settings *link_training_setting)
{
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX];
	uint32_t lane;
	struct dpsst_link_service *dpsst;

	dal_memset(&dpcd_lane, '\0', sizeof(dpcd_lane));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->
		link_settings.lane_count);
		lane++) {
		dpcd_lane[lane].bits.VOLTAGE_SWING_SET =
			(uint8_t)(link_training_setting->
			lane_settings[lane].VOLTAGE_SWING);
		dpcd_lane[lane].bits.PRE_EMPHASIS_SET =
			(uint8_t)(link_training_setting->
			lane_settings[lane].PRE_EMPHASIS);
		dpcd_lane[lane].bits.MAX_SWING_REACHED =
			(link_training_setting->
			lane_settings[lane].VOLTAGE_SWING ==
			VOLTAGE_SWING_MAX_LEVEL ? 1 : 0);
		dpcd_lane[lane].bits.MAX_PRE_EMPHASIS_REACHED =
			(link_training_setting->
			lane_settings[lane].PRE_EMPHASIS ==
			PRE_EMPHASIS_MAX_LEVEL ? 1 : 0);
	}

	dal_dpsst_ls_write_dpcd_data(ls,
	DPCD_ADDRESS_LANE0_SET,
	(uint8_t *)(dpcd_lane),
	link_training_setting->link_settings.lane_count);


	/*
	if (LTSettings.link.rate == LinkRate_High2)
	{
		DpcdTrainingLaneSet2 dpcd_lane2[lane_count_DPMax] = {0};
		for ( uint32_t lane = 0;
		lane < lane_count_DPMax; lane++)
		{
			dpcd_lane2[lane].bits.post_cursor2_set =
			static_cast<unsigned char>(
			LTSettings.laneSettings[lane].postCursor2);
			dpcd_lane2[lane].bits.max_post_cursor2_reached = 0;
		}
		m_pDpcdAccessSrv->WriteDpcdData(
		DpcdAddress_Lane0Set2,
		reinterpret_cast<unsigned char*>(dpcd_lane2),
		LTSettings.link.lanes);
	}
	*/

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_LINK_TRAINING,
		"%s\n %x VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
		__func__,
		DPCD_ADDRESS_LANE0_SET,
		dpcd_lane[0].bits.VOLTAGE_SWING_SET,
		dpcd_lane[0].bits.PRE_EMPHASIS_SET,
		dpcd_lane[0].bits.MAX_SWING_REACHED,
		dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);

	dpsst->ln_setting = link_training_setting->lane_settings[0];

}


static bool perform_clock_recovery_sequence(
	struct link_service *ls,
	struct display_path *display_path,
	struct link_training_settings *lt_settings)
{
	uint32_t retries_cr;
	uint32_t retry_count;
	uint32_t lane;
	struct link_training_settings req_settings;
	enum lane_count lane_count =
	lt_settings->link_settings.lane_count;
	enum hw_dp_training_pattern hw_tr_pattern =
	HW_DP_TRAINING_PATTERN_1;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated dpcd_lane_status_updated;

	retries_cr = 0;
	retry_count = 0;
	/* initial drive setting (VS/PE/PC2)*/
	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		lt_settings->lane_settings[lane].VOLTAGE_SWING =
		VOLTAGE_SWING_LEVEL0;
		lt_settings->lane_settings[lane].PRE_EMPHASIS =
		PRE_EMPHASIS_DISABLED;
		lt_settings->lane_settings[lane].POST_CURSOR2 =
		POST_CURSOR2_DISABLED;
	}

	set_hw_training_pattern(
	ls,
	display_path,
	hw_tr_pattern);

	/* najeeb - The synaptics MST hub can put the LT in
	* infinite loop by switching the VS
	*/
	/* between level 0 and level 1 continuously, here
	* we try for CR lock for LinkTrainingMaxCRRetry count*/
	while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
	(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {

		dal_memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
		dal_memset(&dpcd_lane_status_updated, '\0',
		sizeof(dpcd_lane_status_updated));

		/* 1. call HWSS to set lane settings*/
		dal_hw_sequencer_set_lane_settings(
		ls->hwss,
		display_path,
		lt_settings);

		/* 2. update DPCD of the receiver*/
		if (!retries_cr)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration.*/
			dpcd_set_lt_pattern_and_lane_settings(
			ls,
			lt_settings,
			hw_tr_pattern);
		else
			dpcd_set_lane_settings(
			ls,
			lt_settings);


		/* 3. wait receiver to lock-on*/
		wait_for_training_aux_rd_interval(
		ls,
		100);

		/* 4. Read lane status and requested drive
		* settings as set by the sink
		*/
		get_lane_status_and_drive_settings(
		ls,
		lt_settings,
		dpcd_lane_status,
		&dpcd_lane_status_updated,
		&req_settings);


		/* 5. check CR done*/
		if (is_cr_done(lane_count, dpcd_lane_status))
			return true;

		/* 6. max VS reached*/
		if (is_max_vs_reached(lt_settings))
			return false;

		/* 7. same voltage*/
		/* Note: VS same for all lanes,
		* so comparing first lane is sufficient*/
		if (lt_settings->lane_settings[0].VOLTAGE_SWING ==
			req_settings.lane_settings[0].VOLTAGE_SWING)
			retries_cr++;
		else
			retries_cr = 0;


			/* 8. update VS/PE/PC2 in lt_settings*/
			update_drive_settings(lt_settings, req_settings);

			retry_count++;
	}

	if (retry_count >= LINK_TRAINING_MAX_CR_RETRY) {
		ASSERT(0);
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"%s: Link Training Error, could not get CR after %d tries. Possibly voltage swing issue",
			__func__,
			LINK_TRAINING_MAX_CR_RETRY);

	}

	return false;

}

static bool perform_channel_equalization_sequence(
	struct link_service *ls,
	struct display_path *display_path,
	struct link_training_settings *lt_settings)
{
	enum lane_count lane_count =
	lt_settings->link_settings.lane_count;
	struct link_training_settings req_settings;
	enum hw_dp_training_pattern hw_tr_pattern;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated dpcd_lane_status_updated;
	uint32_t retries_ch_eq;

	if (is_tps3_supported(ls, display_path))
		hw_tr_pattern = HW_DP_TRAINING_PATTERN_3;
	else
		hw_tr_pattern = HW_DP_TRAINING_PATTERN_2;

	set_hw_training_pattern(
	ls,
	display_path,
	hw_tr_pattern);

	for (retries_ch_eq = 0; retries_ch_eq <=
		LINK_TRAINING_MAX_RETRY_COUNT;
		retries_ch_eq++) {

		dal_memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
		dal_memset(&dpcd_lane_status_updated, '\0',
		sizeof(dpcd_lane_status_updated));
		/* 1. call HWSS*/
		dal_hw_sequencer_set_lane_settings(
		ls->hwss,
		display_path,
		lt_settings);

		/* 2. update DPCD*/
		if (!retries_ch_eq)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration*/
			dpcd_set_lt_pattern_and_lane_settings(
			ls,
			lt_settings,
			hw_tr_pattern);
		else
			dpcd_set_lane_settings(
			ls,
			lt_settings);

		/* 3. wait for receiver to lock-on*/
		wait_for_training_aux_rd_interval(ls, 400);

		/* 4. Read lane status and requested
		 * drive settings as set by the sink*/

		get_lane_status_and_drive_settings(
		ls,
		lt_settings,
		dpcd_lane_status,
		&dpcd_lane_status_updated,
		&req_settings);

		/* 5. check CR done*/
		if (!is_cr_done(lane_count, dpcd_lane_status))
			return false;

		/* 6. check CHEQ done*/
		if (is_ch_eq_done(lane_count,
			dpcd_lane_status,
			&dpcd_lane_status_updated))
			return true;

		/* 7. update VS/PE/PC2 in lt_settings*/
		update_drive_settings(lt_settings, req_settings);
	}

	return false;

}

static bool perform_link_training(
	struct link_service *ls,
	const struct hw_path_mode *path_mode,
	const struct link_settings *link_setting,
	bool skip_video_pattern)
{
	bool status;
	struct dpsst_link_service *dpsst;
	struct link_training_settings lt_settings;
	union dpcd_training_pattern dpcd_pattern;
	union lane_count_set lane_count_set;
	const int8_t *link_rate = "Unknown";

	status = false;
	dal_memset(&dpcd_pattern, '\0', sizeof(dpcd_pattern));
	dal_memset(&lt_settings, '\0', sizeof(lt_settings));
	dal_memset(&lane_count_set, '\0', sizeof(lane_count_set));

	lt_settings.link_settings.link_rate =
	link_setting->link_rate;
	lt_settings.link_settings.lane_count =
	link_setting->lane_count;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/*@todo[vdevulap] move SS to LS, should not be handled by displaypath*/
	lt_settings.link_settings.link_spread =
	dal_display_path_is_ss_supported(
	path_mode->display_path) ?
	LINK_SPREAD_05_DOWNSPREAD_30KHZ :
	LINK_SPREAD_DISABLED;

	/* 1. set link rate, lane count and spread*/
	dpcd_set_link_settings(ls, &lt_settings);

	/* 2. perform link training (set link training done
	 *  to false is done as well)*/
	if (perform_clock_recovery_sequence(ls,
		path_mode->display_path,
		&lt_settings)) {
		if (perform_channel_equalization_sequence(ls,
			path_mode->display_path,
			&lt_settings))
			status = true;
	}

	if (status || !skip_video_pattern) {

		/* 3. set training not in progress*/
		dpcd_pattern.bits.TRAINING_PATTERN_SET =
		DPCD_TRAINING_PATTERN_VIDEOIDLE;
		dpcd_set_training_pattern(ls, dpcd_pattern);

		/* 4. mainlink output idle pattern*/
		set_dp_phy_pattern(ls, path_mode->display_path,
		DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

		/* 5. post training adjust if required*/
		if (dpsst->dpcd_caps.max_ln_count.bits.
			POST_LT_ADJ_REQ_SUPPORTED == 1) {
			if (status == true) {
				if (perform_post_lt_adj_req_sequence(
					ls,
					path_mode->display_path,
					&lt_settings) == false)
					status = false;

			}


			lane_count_set.bits.LANE_COUNT_SET =
			lt_settings.link_settings.lane_count;
			lane_count_set.bits.ENHANCED_FRAMING = 1;
			lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED = 0;

			dal_dpsst_ls_write_dpcd_data(
			ls,
			DPCD_ADDRESS_LANE_COUNT_SET,
			&lane_count_set.raw,
			sizeof(lane_count_set));

		}
	}
	/* 6. print status message*/

	switch (lt_settings.link_settings.link_rate) {

	case LINK_RATE_LOW:
		link_rate = "Low";
		break;
	case LINK_RATE_HIGH:
		link_rate = "High";
		break;
	case LINK_RATE_HIGH2:
		link_rate = "High2";
		break;
	case LINK_RATE_RBR2:
		link_rate = "RBR2";
		break;
	default:
		break;
	}

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_MST,
		LOG_MINOR_MST_PROGRAMMING,
		"Link training for %x lanes %s rate %s\n",
		lt_settings.link_settings.lane_count,
		link_rate,
		status ? "succeeded" : "failed");

	dal_logger_write(ls->dal_context->logger,
		LOG_MAJOR_MST,
		LOG_MINOR_MST_PROGRAMMING,
		"Link training for %x lanes %s rate %s\n",
		lt_settings.link_settings.lane_count,
		link_rate,
		status ? "succeeded" : "failed");

	return status;


}

static bool is_dp_phy_pattern(enum dp_test_pattern test_pattern)
{
	if (DP_TEST_PATTERN_D102 == test_pattern ||
		DP_TEST_PATTERN_SYMBOL_ERROR == test_pattern ||
		DP_TEST_PATTERN_PRBS7 == test_pattern ||
		DP_TEST_PATTERN_80BIT_CUSTOM == test_pattern ||
		DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE == test_pattern ||
		DP_TEST_PATTERN_TRAINING_PATTERN1 == test_pattern ||
		DP_TEST_PATTERN_TRAINING_PATTERN2 == test_pattern ||
		DP_TEST_PATTERN_TRAINING_PATTERN3 == test_pattern ||
		DP_TEST_PATTERN_VIDEO_MODE == test_pattern)
		return true;
	else
		return false;

}

static void start_gtc_sync(struct link_service *ls)
{

	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/*if GTC is not synchronized yet, DP revision is
	 * at least 1.2 on Rx and audio is supported, start GTC synchronization.
	 */
	if ((dpsst->gtc_status == GTC_SYNC_STATUS_DISABLED) &&
		(dpsst->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) &&
		(dpsst->num_of_audio_endpoint != 0)) {
		/* Start GTC synchronization.*/
		/*TODO:
		if (m_pDpcdAccessSrv->StartGTCSync())*/
		dpsst->gtc_status = GTC_SYNC_STATUS_SYNC_MAINTENANCE;
	}
}

static void stop_gtc_sync(struct link_service *ls)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/* Stop GTC synchronization if it's synchronized.*/
	/* If GTC started synchronizing, DP revision is at
	 * least 1.2 on Rx and*/
	/* audio is supported, stop GTC synchronization.*/
	if (dpsst->gtc_status != GTC_SYNC_STATUS_DISABLED) {
		/* Stop GTC synchronization*/
		/*TODO:if (m_pDpcdAccessSrv->StopGTCSync())*/
		dpsst->gtc_status = GTC_SYNC_STATUS_DISABLED;
		/*TODO:else
		DALASSERT_MSG(false,
		("GTC synchronization can not be stopped"));*/
	}

}

static void disable_link(struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	/*stop GTC synchronization*/
	stop_gtc_sync(ls);

	/*call base function*/
	dal_ls_disable_link(ls, path_mode);

}

static void retrain_link(
	struct link_service *ls,
	struct hw_path_mode_set *path_set)
{
	uint32_t display_index;
	struct hw_path_mode *path_mode;
	struct display_path *display_path;

	ASSERT(dal_hw_path_mode_set_get_paths_number(path_set) == 1);

	/*For SST, only one entry will be added to
	 * the struct hw_path_modeSet*/
	/*so we always take index 0.*/
	if (!ls->link_st.FAKE_CONNECTED) {

		path_mode = dal_hw_path_mode_set_get_path_by_index(path_set, 0);

		display_path = path_mode->display_path;
		display_index = dal_display_path_get_display_index(
						display_path);

		dal_ls_blank_stream(ls, display_index, path_mode);
		disable_stream(ls, display_index, path_mode);
		enable_stream(ls, display_index, path_mode);
		dal_ls_unblank_stream(ls, display_index, path_mode);
	}
}

static bool try_enable_link(struct link_service *ls,
	const struct hw_path_mode *path_mode,
	const struct link_settings *link_setting)
{
	bool result;
	enum dp_alt_scrambler_reset assr;
	result = false;

	if (dal_ls_try_enable_link_base(ls,
		path_mode,
		link_setting)) {

		assr = decide_assr(ls);
		dpcd_configure_assr(ls, assr);
		if (perform_link_training(ls,
			path_mode,
			link_setting,
			false)) {
			start_gtc_sync(ls);
			result = true;
		}
	}
	return result;
}

static void send_link_failure_notification(
	struct link_service *ls,
	bool result)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (LANE_COUNT_UNKNOWN != dpsst->reported_link_cap.lane_count) {

		/*request from AMDDSAT or actual link failure*/
		if (result) {
			/*TODO:dal_tm_notify_link_training_failure();*/
			dpsst->link_training_preference.FAIL_LINK_TRAINING = 0;

		}
	}
}

static bool handle_hpd_irq_psr_sink(struct link_service *ls)
{
	uint32_t notification_display_idx;
	struct dpsst_link_service *dpsst;
	union dpcd_psr_configuration psr_configuration;

	dal_memset(&psr_configuration, '\0', sizeof(psr_configuration));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	notification_display_idx =
			dal_ls_get_notification_display_index(ls);

	 /* PSR specific DPCD registers are only defined in the eDP
	 * 1.3 spec. For normal DP 1.2 spec, these DPCD registers
	 * are reserved and the values are undefined. In some DP
	 * Sink, these registers may return non-zero and cause our
	 * driver to read back bad values when accessing these DPCD
	 * registers. We should only read PSR status registers in
	 * DPCD only if we've attached the DMCU object to the display
	 * path and we are supporting the PSR feature. Without test
	 * related runtime parameters set, the DMCU object is only attached
	 * to an eDP panel, in which values are defined by the eDP 1.3 spec. */
	if (!dal_ls_is_link_psr_supported(ls))
		return false;

	 /* If PSR is supported, we may be in the PSR active state,
	 * in which case main link is powered down. We must first
	 * check if any PSR errors are reported. If so, we should
	 * handle these errors by disabling and re-enabling PSR. */
	dal_dpsst_ls_read_dpcd_data(ls,
			DPCD_ADDRESS_PSR_ENABLE_CFG,
			&psr_configuration.raw,
			sizeof(psr_configuration));

	if (psr_configuration.bits.ENABLE) {
		uint8_t dpcd_buf[3];
		union psr_error_status psr_error_status;
		union psr_sink_psr_status psr_sink_psr_status;

		dal_memset(dpcd_buf, '\0', sizeof(dpcd_buf));
		dal_memset(&psr_error_status, '\0', sizeof(psr_error_status));
		dal_memset(&psr_sink_psr_status,
				'\0', sizeof(psr_sink_psr_status));

		 /* Read 3 bytes from Sink DPCD for PSR status */
		dal_dpsst_ls_read_dpcd_data(ls,
				DPCD_ADDRESS_PSR_ERROR_STATUS,
				dpcd_buf,
				sizeof(dpcd_buf));

		 /* DPCD 2006h ERROR STATUS */
		psr_error_status.raw = dpcd_buf[0];
		 /* DPCD 2008h SINK PANEL SELF REFRESH STATUS */
		psr_sink_psr_status.raw = dpcd_buf[2];

		if (psr_error_status.bits.LINK_CRC_ERROR ||
			psr_error_status.bits.RFB_STORAGE_ERROR) {

			/*Acknowledge and clear error bits*/
			dal_dpsst_ls_write_dpcd_data(ls,
			DPCD_ADDRESS_PSR_ERROR_STATUS,
			&psr_error_status.raw,
			sizeof(psr_error_status.raw));

			/* PSR error, disable and re-enable PSR*
			*TODO:dal_tm_handle_psr_error(
			 * notification_display_idx);*/

			 /* PSR related error was detected and handled. Main
			 * link may be off, so do not handle as a normal
			 * sink status change interrupt.
			 */

			dal_logger_write(ls->dal_context->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_HPD_IRQ,
				"PSR Error was handled. PSR error status reg: 0x%02X\n",
				psr_error_status.raw);

			return true;
		} else if (psr_sink_psr_status.bits.SINK_SELF_REFRESH_STATUS ==
				PSR_SINK_STATE_ACTIVE_DISPLAY_FROM_SINK_RFB) {
			/* According to the VESA EDP 1.3 spec,
			 * the sink is able to update PSR
			 * capabilities and write the PSR
			 * ESI bit 0 to 1 and trigger an
			 * IRQ_HPD to notify driver of the
			 * change in PSR capability. The sink
			 * is not allowed to make further
			 * changes to the PSR capability until
			 * driver clears this bit. Write bit 0 to
			 * 1 in order to clear the capability
			 * change flag if the sink reported
			 * PSR capability change.
			 * This indicates to the sink that it
			 * is free to make further changes to
			 * the PSR capability. */

			dal_logger_write(ls->dal_context->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_LINK_SERVICE,
				"PSR: Unexpected IRQ_HPD triggered by Sink with no PSR Error Status! May point to panel issue!");

			 /* In this case no error is detected,
			 * but PSR is active.*
			 * We should return with IRQ_HPD
			 * handled without checking
			 * for loss of sync since PSR
			 * would have powered down main link.*/
			return true;
		}
	}
	return false;
}

static void handle_hpd_irq_downstream_port_status_change(
	struct link_service *ls,
	union hpd_irq_data *irq_data)
{
	struct dpsst_link_service *dpsst;
	union downstream_port down_stream_port;
	enum dpcd_downstream_port_type converter;
	uint32_t current_sink_count;

	dal_memset(&down_stream_port, '\0', sizeof(down_stream_port));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	current_sink_count = irq_data->bytes.sink_cnt.bits.SINK_COUNT;

	if (current_sink_count == dpsst->prev_sink_count &&
		dpsst->dpcd_caps.branch_dev_id != DP_BRANCH_DEVICE_ID_3)
		return;

	dpsst->prev_sink_count = current_sink_count;

	 /* This short pulse can arrive from a DP to VGA
	 * (or DVI/HDMI) converter (active dongle), converter
	 * sends a short pulse if a CRT/DVI/HDMI is plugged
	 * into it or removed from it. If the down stream
	 * device is *not* plugged-in into converter, the
	 * DP HPD sense bit is still high, but we can read
	 * it's sink count - should be zero (got disconnected)
	 * or one (got connected).
	 *

	 * TODO: By DP specification from version 1.1a, bit6
	 * of DPCD 0x204(DpcdAddress_LaneAlignStatusUpdated)
	 *  has been defined as below
	 *  Bit 6 = DOWNSTREAM_PORT_STATUS_CHANGED Bit 6 is
	 *  set when any of the downstream ports has changed status.
	 *


	 * But some converters may not set this bit when monitor
	 * is plugged in/out. To avoid usage regression, we do not
	 * check bit6 for now. We will ask dongle qualify team to
	 * run dongle test and then enable bit6 check.
	 */
	dal_dpsst_ls_read_dpcd_data(
	ls,
	DPCD_ADDRESS_DOWNSTREAM_PORT_PRESENT,
	&down_stream_port.raw,
	sizeof(down_stream_port));

	converter = (enum dpcd_downstream_port_type)
				(down_stream_port.bits.TYPE);

	dal_tm_handle_sink_connectivity_change(ls->tm,
			dal_ls_get_notification_display_index(ls));

}


static void handle_sst_hpd_irq(void *interrupt_params)
{
	struct link_service *ls = interrupt_params;
	union hpd_irq_data hpd_irq_dpcd_data;
	bool psr_irq_handled = false;
	enum ddc_result result = DDC_RESULT_UNKNOWN;

	if (ls->strm_state == STREAM_STATE_POWER_SAVE) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_HPD_IRQ,
			"%s: interrupt happens in STREAM_STATE_POWER_SAVE state, should be blocked, do nothing\n",
			__func__);
		ASSERT(false);
		return;
	}

	dal_memset(&hpd_irq_dpcd_data, 0, sizeof(hpd_irq_dpcd_data));

	 /* All the "handle_hpd_irq_xxx()" methods
	 * should be called only after
	 * dal_dpsst_ls_read_hpd_irq_data
	 * Order of calls is important too
	 */
	result = dal_dpsst_ls_read_hpd_irq_data(ls, &hpd_irq_dpcd_data);

	if (result != DDC_RESULT_SUCESSFULL) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_HPD_IRQ,
			"%s: DPCD read failed to obtain irq data\n",
			__func__);
		return;
	}

	 /* Handle service requests only if
	 * sink status is unchanged*/
	if (dal_dpsst_ls_handle_hpd_irq_device_service(
		ls,
		&hpd_irq_dpcd_data)) {
		return;
	}
	if (handle_hpd_irq_psr_sink(ls))
		/* PSR related error was detected and handled
		 * main link may be off, so do not handle as
		 * a normal sink status change interrupt */
		psr_irq_handled = true;

	if (!psr_irq_handled) {
		if (dal_dpsst_ls_handle_hpd_irq_link_status(
			ls,
			&hpd_irq_dpcd_data))
			return;
	}

	 /* If PSR related error handled, do not handle down
	 * stream status change
	 */
	if (!psr_irq_handled)
		handle_hpd_irq_downstream_port_status_change(
		ls,
		&hpd_irq_dpcd_data);
}

/* This method is not thread-safe and should
* not be called by MST code.*/
/*TODO:
static void dp_test_send_link_training(struct link_service *ls)
{
	struct link_settings link;
	struct dpsst_link_service *dpsst;
	struct dp_test_event_data *event_data;
	const uint32_t  dp_event_data_size;
	uint8_t dp_event_data_buf[ dp_event_data_size ];
	struct dp_test_request *dp_test_request;


	dal_memset(dp_event_data_buf, '\0', sizeof(dp_event_data_buf));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	dal_memset(&link, '\0', sizeof(link));*/

	/* get link settings for the link training*/
	/*TODO:
	dal_dpsst_ls_read_dpcd_data(
	DPCD_ADDRESS_TEST_LANE_COUNT, &link.lane_count, 1);

	dal_dpsst_ls_read_dpcd_data(
	DPCD_ADDRESS_TEST_LINK_RATE, &link.link_rate, 1);*/

	/* prepare event buffer*/
	/*TODO:
	dp_event_data_size = sizeof(struct dp_test_event_data) -
	sizeof(uint32_t) +
	sizeof(struct dp_test_request);*/

	/* fill in pattern request*/
	/*TODO:
	event_data = dp_event_data_buf;
	dp_test_request = event_data->params;

	event_data->size = sizeof(struct dp_test_request);
	dp_test_request->request = DP_TEST_REQUEST_LINK_TRAINING;
	dp_test_request->data.training.link_settings.link = link;*/

	/*notify DP test event*/
	/*TODO: m_pCallback->NotifyDPTestEvent(getNotificationDisplayIdx(),
	 *	event_data, dp_event_data_size);

}*/

/*TODO:
static void dp_test_send_link_test_pattern(struct link_service *ls)
{
	union link_test_pattern dpcd_test_pattern;
	union test_misc dpcd_test_params;
	struct dpsst_link_service *dpsst;
	enum dp_test_pattern test_pattern;
	enum hw_pixel_encoding pixel_encoding;
	enum hw_color_depth color_depth;
	*/
	/* prepare event buffer*/
	/*TODO:
	const uint32_t dp_event_data_size = sizeof(struct dp_test_event_data) -
	sizeof(uint32_t) +
	sizeof(struct dp_test_request);

	uint8_t dp_event_data_buf[dp_event_data_size];

	struct dp_test_event_data *event_data;
	struct dp_test_request *dp_test_request;

	dal_memset(dp_event_data_buf, '\0', sizeof(dp_event_data_buf));
	dal_memset(&dpcd_test_pattern, '\0', sizeof(dpcd_test_pattern));
	dal_memset(&dpcd_test_params, '\0', sizeof(dpcd_test_params));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);*/

	/* get link test pattern and pattern parameters*/
	/*TODO:
	dal_dpsst_ls_read_dpcd_data(DPCD_ADDRESS_TEST_PATTERN,
	&dpcd_test_pattern.raw,
	sizeof(dpcd_test_pattern));

	dal_dpsst_ls_read_dpcd_data(DPCD_ADDRESS_TEST_MISC1,
	&dpcd_test_params.raw,
	sizeof(dpcd_test_params));
	*/

	/* translate request*/
	/*TODO:
	switch ( dpcd_test_pattern.bits.PATTERN) {

	case LINK_TEST_PATTERN_COLOR_RAMP:
		test_pattern = DP_TEST_PATTERN_COLOR_RAMP;
		break;
*/
	/* black and white*/
/*TODO:
	case LINK_TEST_PATTERN_VERTICAL_BARS:
		test_pattern = DP_TEST_PATTERN_VERTICAL_BARS;
		break;

	case LINK_TEST_PATTERN_COLOR_SQUARES:
		test_pattern =
		(dpcd_test_params.bits.DYN_RANGE ==
		TEST_DYN_RANGE_VESA ?
		DP_TEST_PATTERN_COLOR_SQUARES :
		DP_TEST_PATTERN_COLOR_SQUARES_CEA);
		break;

	default:
	test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
	break;
	}

	switch ( dpcd_test_params.bits.CLR_FORMAT) {

	case TEST_COLOR_FORMAT_RGB:
		pixel_encoding = HW_PIXEL_ENCODING_RGB;
		break;

	case TEST_COLOR_FORMAT_YCBCR422:
		pixel_encoding = HW_PIXEL_ENCODING_YCBCR422;
		break;

	case TEST_COLOR_FORMAT_YCBCR444:
		pixel_encoding = HW_PIXEL_ENCODING_YCBCR444;
		break;

	default:
		pixel_encoding = HW_PIXEL_ENCODING_UNKNOWN;
		break;
	}

	switch ( dpcd_test_params.bits.BPC) {

	case TEST_BIT_DEPTH_6:
		color_depth = COLOR_DEPTH_666;
		break;
	case TEST_BIT_DEPTH_8:
		color_depth = COLOR_DEPTH_888;
		break;
	case TEST_BIT_DEPTH_10:
		color_depth = COLOR_DEPTH_101010;
		break;
	case TEST_BIT_DEPTH_12:
		color_depth = COLOR_DEPTH_121212;
		break;
	case TEST_BIT_DEPTH_16:
		color_depth = COLOR_DEPTH_161616;
		break;
	default:
		color_depth = COLOR_DEPTH_UNKNOWN;
		break;
	}
	*/

	/* fill in pattern request*/
	/*TODO:
	event_data = dp_event_data_buf;
	dp_test_request = event_data->params;

	event_data->size = sizeof(struct dp_test_request);
	dp_test_request->request = DP_TEST_REQUEST_SET_LINK_TEST_PATTERN;
	dp_test_request->pattern = test_pattern;
	dp_test_request->data.link.pixel_encoding = pixel_encoding;
	dp_test_request->data.link.color_depth = color_depth;*/

	/*TODO:
	 * callback->notify_dp_test_event (get_notification_display_index(),
	 * event_data,
	 * event_data_size);


}*/
/*TODO:
static void dp_test_send_phy_test_pattern(struct link_service *ls)
{
	union Phytest_pattern  dpcd_test_pattern;*/
	/* 0 - lanes 0,1; 1 - lanes 2,3 */
	/*TODO:
	union LaneAdjust  dpcd_lane_adjustment[2];
	uint8_t dpcd_post_cursor2_adjustment = 0;
	struct dpsst_link_service *dpsst;

	uint8_t test80_bit_pattern[
	(DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_79_72 -
	DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_7_0) + 1 ] ;

	enum dp_test_pattern test_pattern;
	struct lane_settings lane_settings[LANE_COUNT_DP_MAX];
	union lane_adjust dpcd_lane_adjust;
	uint32_t lane;*/

	/* prepare event buffer*/
	/*TODO:
	const uint32_t  dp_event_data_size =
	sizeof(struct dp_test_event_data) -
	sizeof(uint32_t) +
	sizeof(struct dp_test_request);

	uint8_t dp_event_data_buf[ dp_event_data_size ];

	struct dp_test_event_data *event_data;
	struct dp_test_request *dp_test_request;

	dal_memset(dp_event_data_buf, '\0', sizeof(dp_event_data_buf));
	dal_memset(test80_bit_pattern, '\0', sizeof(test80_bit_pattern));
	dal_memset(&dpcd_test_pattern, '\0', sizeof(dpcd_test_pattern));
	dal_memset(&dpcd_lane_adjustment, '\0', sizeof(dpcd_lane_adjustment));
	dal_memset(&lane_settings, '\0', sizeof(lane_settings));
	dal_memset(&dpcd_lane_adjust, '\0', sizeof(dpcd_lane_adjust));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);
	*/
	/* get phy test pattern and pattern parameters from DP receiver*/
	/*TODO:
	dal_dpsst_ls_read_dpcd_data(DPCD_ADDRESS_TEST_PHY_PATTERN,
	&dpcd_test_pattern.raw,
	sizeof(dpcd_test_pattern));

	dal_dpsst_ls_read_dpcd_data(
	DPCD_ADDRESS_ADJUST_REQUEST_LANE0_1,
	&dpcd_lane_adjustment[0].raw,
	sizeof(dpcd_lane_adjustment));
	*/
	/* get post cursor 2 parameters*/
	/* For DP 1.1a or eariler, this DPCD register's value is 0*/
	/* For DP 1.2 or later:*/
	/* Bits 1:0 = POST_CURSOR2_LANE0; Bits 3:2 = POST_CURSOR2_LANE1*/
	/* Bits 5:4 = POST_CURSOR2_LANE2; Bits 7:6 = POST_CURSOR2_LANE3*/
	/*TODO:
	dal_dpsst_ls_read_dpcd_data(
	DPCD_ADDRESS_ADJUST_REQUEST_POST_CURSOR2,
	&dpcd_post_cursor2_adjustment,
	sizeof(dpcd_post_cursor2_adjustment));
	*/

	/* translate request*/
	/*TODO:
	switch ( dpcd_test_pattern.bits.pattern) {
	case PHY_TEST_PATTERN_D10_2:
		test_pattern = DP_TEST_PATTERN_D102;
		break;
	case PHY_TEST_PATTERN_SYMBOL_ERROR:
		test_pattern = DP_TEST_PATTERN_SYMBOL_ERROR;
		break;
	case PHY_TEST_PATTERN_PRBS7:
		test_pattern = DP_TEST_PATTERN_PRBS7;
		break;
	case PHY_TEST_PATTERN_80BIT_CUSTOM:
		test_pattern = DP_TEST_PATTERN_80_BIT_CUSTOM;
		break;
	case PHY_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
		test_pattern = DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE;
		break;
	default:
		test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
		break;
	}

	if ( test_pattern == DP_TEST_PATTERN_80_BIT_CUSTOM)*/
		/* get 80 bits pattern from test equipment*/
	/*TODO:
		dal_dpsst_ls_read_dpcd_data(
		DPCD_ADDRESS_TEST_80BIT_CUSTOM_PATTERN_7_0,
		test80_bit_pattern,
		sizeof(test80_bit_pattern));*/

	/* prepare parameters for event*/
	/*TODO:
	for (lane = 0; lane
			 < (ls->cur_link_setting.lane_count); lane++) {

		dpcd_lane_adjust.raw = get_nibble_at_index(
		&dpcd_lane_adjustment[0].raw, lane);

		lane_settings[lane].VOLTAGE_SWING =
		dpcd_lane_adjust.bits.VOLTAGE_SWING_LANE;

		lane_settings[lane].PRE_EMPHASIS =
		dpcd_lane_adjust.bits.PRE_EMPHASIS_LANE;

		lane_settings[lane].POST_CURSOR2 =
		dpcd_post_cursor2_adjustment >> (lane * 2) & 0x03;
	}
	*/

	/* fill in pattern request*/
	/*TODO:
	event_data = (struct dp_test_event_data*)(dp_event_data_buf);
	dp_test_request = (struct dp_test_request *)(event_data->params);

	event_data->size= sizeof(struct dp_test_request);
	dp_test_request->request = DP_TEST_REQUEST_SET_PHY_TEST_PATTERN;
	dp_test_request->pattern = test_pattern;
	dp_test_request->data.phy.link_settings.link.lane_count =
	ls->cur_link_setting.lane_count;
	dp_test_request->data.phy.link_settings.link.link_rate =
	ls->cur_link_setting.link_rate

	dal_memmove(dp_test_request->data.phy.link_settings.lane_settings,
	lane_settings, sizeof(lane_settings));
	dal_memmove(dp_test_request->data.phy.TEST_80_BIT_PATTERNS,
	test80_bit_pattern,
	sizeof(test80_bit_pattern));
	*/
	/*notify DP test event*/
	/*
	 TODO:m_pCallback->NotifyDPTestEvent(
	 getNotificationDisplayIdx(), event_data, dp_event_data_size);


}*/
/*TODO:
static void dp_test_send_audio_test_pattern(struct link_service *ls,
	bool disable_video)
{
	struct dp_test_pattern test_pattern =
	DP_TEST_PATTERN_AUDIO_OPERATOR_DEFINED;
	*/
	/* retrieve test pattern parameter from DP receiver*/
	/*TODO:
	union audio_test_mode dpcd_test_mode;
	struct audio_test_pattern_type dpcd_pattern_type;

	union audio_test_pattern_period
	dpcd_pattern_period[AUDIO_CHANNELS_COUNT];

	struct dpsst_link_service *dpsst;
	uint32_t sampling_rate_in_hz;
	uint32_t channel;
	uint32_t i;
	struct dp_test_event_data *event_data;
	struct dp_test_request *dp_test_request;
	*/
	/* prepare event buffer*/
	/*TODO:
	const uint32_t  dp_event_data_size = sizeof(struct DPTestEventData) -
		sizeof(uint32_t) +
		sizeof(struct DPTestRequest);

	uint8_t dp_event_data_buf[ dp_event_data_size ];
	dal_memset(dp_event_data_buf, '\0', sizeof(dp_event_data_buf));
	dal_memset(&dpcd_test_mode, '\0', sizeof(dpcd_test_mode));
	dal_memset(&dpcd_pattern_type, '\0', sizeof(dpcd_pattern_type));
	dal_memset(&dpcd_pattern_period, '\0', sizeof(dpcd_pattern_period));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);*/

	/* get audio test mode and test pattern parameters*/
	/*TODO:
	 * dal_dpsst_ls_read_dpcd_data(DPCD_ADDRESS_TEST_AUDIO_MODE,
						&dpcd_test_mode.raw,
						sizeof(dpcd_test_mode));

	dal_dpsst_ls_read_dpcd_data(
		DPCD_ADDRESS_TEST_AUDIO_PATTERN_TYPE,
						&dpcd_pattern_type.value,
						sizeof(dpcd_pattern_type));
	*/
	/* read pattern periods for requested channels
	 * when sawTooth pattern is requested
	 */
	/*TODO:
	if ( dpcd_pattern_type.value == AUDIO_TEST_PATTERN_SAWTOOTH) {

		test_pattern = DP_TEST_PATTERN_AUDIO_SAWTOOTH;*/

		/* read period for each channeltest_pattern*/
		/*TODO:
		for (channel = 0; channel <=
		dpcd_test_mode.bits.CHANNEL_COUNT; channel++) {
			dal_dpsst_ls_read_dpcd_data(
			DPCD_ADDRESS_TEST_AUDIO_PERIOD_CH_1 + channel,
			&dpcd_pattern_period[ channel ].raw,
			sizeof(union audio_test_pattern_period));
		}
	}

	*/
	/* translate sampling rate*/
	/*TODO:
	switch ( dpcd_test_mode.bits.SAMPLING_RATE) {

	case AUDIO_SAMPLING_RATE_32KHZ:
		sampling_rate_in_hz = 32000;
		break;
	case AUDIO_SAMPLING_RATE_44_1KHZ:
		sampling_rate_in_hz = 44100;
		break;
	case AUDIO_SAMPLING_RATE_48KHZ:
		sampling_rate_in_hz = 48000;
		break;
	case AUDIO_SAMPLING_RATE_88_2KHZ:
		sampling_rate_in_hz = 88200;
		break;
	case AUDIO_SAMPLING_RATE_96KHZ:
		sampling_rate_in_hz = 96000;
		break;
	case AUDIO_SAMPLING_RATE_176_4KHZ:
		sampling_rate_in_hz = 176400;
		break;
	case AUDIO_SAMPLING_RATE_192KHZ:
		sampling_rate_in_hz = 192000;
		break;
	default:
		sampling_rate_in_hz = 0;
		break;
	}
	*/
	/* fill in pattern request*/
	/*TODO:event_data = (struct dp_test_event_data*)(dp_event_data_buf);
	dp_test_request = (struct dp_test_request)(event_data->params);

	event_data->size = sizeof(struct dp_test_request);
	dp_test_request->request = DP_TEST_REQUEST_SET_AUDIO_TEST_PATTERN;
	dp_test_request->pattern = test_pattern;
	dp_test_request->data.audio.sampling_rate_in_hz = sampling_rate_in_hz;
	dp_test_request->data.audio.channel_count =
				dpcd_test_mode.bits.CHANNEL_COUNT + 1;
	*/
	/* copy pattern period for Sawtooth*/
	/*TODO:
	if (test_pattern == DP_TEST_PATTERN_AUDIO_SAWTOOTH) {
		for (i = 0; i <
		dp_test_request->data.audio.channel_count; i++) {

			dp_test_request->data.audio.pattern_period[ i ] =
			dpcd_pattern_period[ i ].bits.pattern_period;
		}
	}
	*/
	/* notify DP Test Event
	TODO: m_pCallback->NotifyDPTestEvent(
		getNotificationDisplayIdx(), event_data, dp_event_data_size);

}*/
/*TODO:
static void dp_test_stereo_3d(bool enable_stereo_3d)
{*/
	/* prepare event buffer*/
	/*TODO:
	const uint32_t  dp_event_data_size =
	sizeof(struct DPTestEventData) - sizeof(uint32_t) +
	sizeof(struct DPTestRequest);

	struct dp_test_event_data *event_data;
	struct dp_test_request *dp_test_request;

	uint8_t dp_event_data_buf[ dp_event_data_size ];
	dal_memset(dp_event_data_buf, '\0', sizeof(dp_event_data_buf));
	*/
	/* fill in request data*/
	/*TODO:
	event_data = (struct dp_test_event_data*)(dp_event_data_buf);
	dp_test_request = (struct dp_test_request)(event_data->params);

	event_data->size = sizeof(struct dp_test_request);
	dp_test_request->request = DP_TEST_REQUEST_STEREO_3D;
	dp_test_request->data.stereo3D.enableStereo3D = enable_stereo_3d;
	*/
	/*notify DP test event*/
	/*TODO: m_pCallback->NotifyDPTestEvent(
	*	getNotificationDisplayIdx(),
	*	pEventData, dpEventDataSize);

}

static void handle_automated_test(struct link_service *ls)
{
	union test_request test_request;
	union test_response test_response;
	struct dpsst_link_service *dpsst;

	dal_memset (&test_request, '\0', sizeof(test_request));
	dal_memset (&test_response, '\0', sizeof(test_response));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);
	*/
	/*get test request*/
	/*TODO:dal_dpsst_ls_read_dpcd_data(DPCD_ADDRESS_TEST_REQUEST,
			test_request.raw,
			sizeof(test_request));

	if (test_request.bits.LINK_TRAINING) {*/

		/*ACK first to let DP RX test box monitor LT sequence*/
	/*TODO:test_response.bits.ACK = 1;
	dal_dpsst_ls_write_dpcd_data(DPCD_ADDRESS_TEST_RESPONSE,
							&test_response.raw,
							sizeof(test_response));
	*/
		/* send test event before next step*/
	/*TODO:	dp_test_send_link_training();*/

		/* no acknowledge request is needed again*/
	/*TODO:test_response.bits.ACK = 0;
	}
	if ( test_request.bits.AUDIO_TEST_PATTERN ||
			test_request.bits.AUDIO_TEST_NO_VIDEO) {*/
		/* (HW is missing audio test patterns, instead request
		 * will be saved till reported to test application for
		 * playing audio test pattern through media player)
		 * sink may request to disable video output during audio test
		 */
	/*TODO:dp_test_send_audio_test_pattern(
		test_request.bits.AUDIO_TEST_NO_VIDEO);
		test_response.bits.ACK = 1;
	}
	if ( test_request.bits.LINK_TEST_PATTERN)i {*/
		/* send test event before next step*/
	/*TODO:dp_test_send_link_test_pattern();
		test_response.bits.ACK = 1;
	}
	if ( test_request.bits.PHY_TEST_PATTERN) {*/
		/* send test event before next step*/
	/*TODO:dp_test_send_phy_test_pattern();
		test_response.bits.ACK = 1;
	}

	if ( test_request.bits.EDID_READ)*/
		/* ZAZTODO: implement EDID test read*/
	/*TODO:		test_response.bits.ACK = 1;


	if ( test_request.bits.TEST_STEREO_3D){
		if ( !dpsst->teststate.bits.STEREO_3D_RUNNING) {*/
			/* Test is *not* running - we got a request
			 * to start it.
			 * ACK to signal DP RX test box to start
			 * monitor MSA 3D status. */
	/*TODO:test_response.bits.ACK = 1;*/

			/* Enable 3D.*/
	/*TODO:dp_test_stereo_3D(true);
			dpsst->teststate.bits.STEREO_3D_RUNNING = 1;
		}
	} else {
		if (dpsst->teststate.bits.STEREO_3D_RUNNING) {*/
			/* Test is running - we got a request to stop it.
			 * ACK to signal DP RX test box to stop monitor
			 * MSA 3D status and start monitoring 2D.*/
	/*TODO:test_response.bits.ACK = 1;*/

			/* Disable 3D and switch to 2D.*/
	/*TODO:dp_test_stereo_3D(false);
			dpsst->teststate.bits.STEREO_3D_RUNNING = 0;
		}
	}
	if ( !test_request.raw)*/
		/* no requests, revert all test signals*/
		/* ZAZTODO: revert all test signals*/
	/*TODO:test_response.bits.ACK = 1;*/

	/* send request acknowledgement*/
	/*TODO:
	if ( test_response.bits.ACK)
	dal_dpsst_ls_write_dpcd_data(DPCD_ADDRESS_TEST_RESPONSE,
		&test_response.raw,
		sizeof(test_response));

}*/



static bool retry_link_training_workaround(struct link_service *ls,
	const struct hw_path_mode *path_mode,
	struct link_settings *link_setting)
{
	bool success;
	const struct monitor_patch_info *patch_info;
	struct dcs *dcs;
	uint32_t max_retry;
	uint32_t sleep_in_ms;

	dcs = dal_display_path_get_dcs(path_mode->display_path);
	patch_info = dal_dcs_get_monitor_patch_info(dcs,
		MONITOR_PATCH_TYPE_RETRY_LINK_TRAINING_ON_FAILURE);

	success = false;
	max_retry = 600;

	if (patch_info != NULL)
		max_retry = patch_info->param;

	sleep_in_ms = 0;

	while (success != true && sleep_in_ms < max_retry) {
		dal_sleep_in_milliseconds(200);
		sleep_in_ms += 200;
		success = try_enable_link(ls, path_mode, link_setting);
	}

	return success;

}

static void destruct(struct dpsst_link_service *link_serivce)
{
	/* NOTHING IN THE BASE to destroy, so just a place holder*/
}

static void destroy(struct link_service **ls)
{

	struct dpsst_link_service *dpsst;

	dpsst = container_of(*ls, struct dpsst_link_service, link_service);
	destruct(dpsst);
	dal_free(dpsst);
	*ls = NULL;
}

static const struct link_service_funcs funcs = {
	.destroy = destroy,
	.validate_mode_timing = validate_mode_timing,
	.get_gtc_sync_status = get_gtc_sync_status,
	.enable_stream = enable_stream,
	.disable_stream = disable_stream,
	.optimized_enable_stream = optimized_enable_stream,
	.update_stream_features = update_stream_features,
	.blank_stream = dal_link_service_blank_stream,
	.unblank_stream = dal_link_service_unblank_stream,
	.pre_mode_change = dal_link_service_pre_mode_change,
	.post_mode_change = dal_link_service_post_mode_change,
	.power_on_stream = dal_link_service_power_on_stream,
	.power_off_stream = dal_link_service_power_off_stream,
	.connect_link = connect_link,
	.disconnect_link = disconnect_link,
	.associate_link = associate_link,
	.get_notification_display_idx =
		dal_link_service_get_notification_display_idx,
	.invalidate_downstream_devices =
		dal_link_service_invalidate_downstream_devices,
	.are_mst_displays_cofunctional =
		dal_link_service_are_mst_displays_cofunctional,
	.get_mst_sink_info =
		dal_link_service_get_mst_sink_info,
	.is_mst_network_present =
		dal_link_service_is_mst_network_present,
	.is_sink_present_at_display_index =
			dal_link_service_is_sink_present_at_display_index,
	.is_link_psr_supported = is_link_psr_supported,
	.is_stream_drr_supported = is_stream_drr_supported,
	.set_link_psr_capabilities =
			set_link_psr_capabilities,
	.get_link_psr_capabilities =
			get_link_psr_capabilities,
	.obtain_mst_ddc_service =
		dal_link_service_obtain_mst_ddc_service,
	.release_mst_ddc_service =
		dal_link_service_release_mst_ddc_service,
	.release_hw = release_hw,
	.retrain_link = retrain_link,
	.should_send_notification =
		should_send_notification,
	.decide_link_settings = decide_link_settings,

};

static bool construct(struct dpsst_link_service *dpsst_link_service,
	struct link_service_init_data *init_data)
{
	struct link_service *link_service = &dpsst_link_service->link_service;
	/*TODO
	if (init_data->num_of_displays == 0 ||
		init_data->irqmgr == NULL ||
		init_data->irq_src_hpd_rx == NULL ||
		init_data->hwss == NULL ||
		*init_data->irq_src_hpd_rx == IRQSOURCE_INVALID)
		return false;*/

	if (!dal_link_service_construct(link_service, init_data))
		return false;

	dpsst_link_service->gtc_status = GTC_SYNC_STATUS_DISABLED;
	dpsst_link_service->num_of_audio_endpoint = 0;
	dpsst_link_service->test_pattern_enabled = false;

	dpsst_link_service->dp_sink_irq_info.handler_idx =
	DAL_INVALID_IRQ_HANDLER_IDX;

	dpsst_link_service->dp_sink_irq_info.src =
	init_data->irq_src_hpd_rx;

	dpsst_link_service->teststate.raw = 0;

	dpsst_link_service->prev_sink_count = UNKNOWN_SINK_COUNT;

	link_service->funcs = &funcs;
	dpsst_link_service->tm = init_data->tm;

	return true;

}

struct link_service *dal_dpsst_ls_create(
	struct link_service_init_data *init_data)
{
	struct dpsst_link_service *dpsst_link_service = dal_alloc(

	sizeof(struct dpsst_link_service));

	if (dpsst_link_service == NULL)
		return NULL;


	if (construct(dpsst_link_service, init_data))
		return &dpsst_link_service->link_service;


	dal_free(dpsst_link_service);
	return NULL;
}

enum ddc_result dal_dpsst_ls_read_hpd_irq_data(
	struct link_service *ls,
	union hpd_irq_data *irq_data)
{
	/* The HW reads 16 bytes from 200h on HPD,
	 * but if we get an AUX_DEFER, the HW cannot retry
	 * and this causes the CTS tests 4.3.2.1 - 3.2.4 to
	 * fail, so we now explicitly read 6 bytes which is
	 * the req from the above mentioned test cases.
	 */
	return dal_dpsst_ls_read_dpcd_data(
	ls,
	DPCD_ADDRESS_SINK_COUNT,
	irq_data->raw,
	sizeof(union hpd_irq_data));

}


bool dal_dpsst_ls_try_enable_link_with_hbr2_fallback(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	bool success;
	bool link_training_failed;
	struct link_settings link_settings;
	struct dpsst_link_service *dpsst;

	success = false;
	link_training_failed = false;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	decide_link_settings(ls, path_mode, &link_settings);

	success = try_enable_link(ls, path_mode, &link_settings);

	if (!success)
		success = retry_link_training_workaround(ls,
				path_mode,
				&link_settings);

	ASSERT(success == true);

	if (!success && link_settings.link_rate == LINK_RATE_HIGH2) {

		/* allow fallback when link was driving HBR2*/
		/* TPS3 is not robust enough for HBR2 which is
		 * why LT might succeed for HBR2 but subsequently go bad
		 * since DCE 6 we'll need to make sure we
		 * disable the link before
		 * enabling it with different link settings
		 */
		disable_link(ls, path_mode);

		link_training_failed = true;
		 /* don't update verifylinkCap when simulating link
		  * failure otherwise we will get 2 pop-ups*/
		if (dpsst->link_training_preference.FAIL_LINK_TRAINING ||
				!dal_dpsst_ls_verify_link_cap(ls,
				path_mode, &link_settings)) {
			/* enable the verified settings*/
			decide_link_settings(ls, path_mode, &link_settings);
			success = try_enable_link(
					ls,
					path_mode,
					&link_settings);
		}

		ls->cur_link_setting = link_settings;

		/* If all the link settings fail, then verifyLinkCap
		 * will hold the fail safe link settings*/
		/* enable fail safe mode*/
		if (!success) {

			BREAK_TO_DEBUGGER();
			ls->cur_link_setting = dpsst->verified_link_cap;
			/*try to enable the link with
			 * the fail safe link settings*/
			success = try_enable_link(ls,
			path_mode, &ls->cur_link_setting);
		}
		/* no HBR2*/
	} else {

		ls->cur_link_setting = link_settings;
		link_training_failed = !success;
	}

	/*we send notification only once, even if link
	 * training fails multiple times
	 */
	if (link_training_failed) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_LINK_TRAINING,
			"%s: Link training failed, sending notification for user pop-up\n",
			__func__);

		/* sends the notification only
		 * if the request comes from AMDDSAT or
		 * an actual link training failure occurs.
		 */
		send_link_failure_notification(
		ls, link_training_failed);
	}

	return success;

}

bool dal_dpsst_ls_verify_link_cap(
	struct link_service *ls,
	const struct hw_path_mode *path_mode, /*[in]*/
	struct link_settings *failed_link_setting)  /*[in]*/
{
	struct link_settings max_link_cap;
	struct dpsst_link_service *dpsst;
	struct dal_timer_interrupt_params flags;
	bool success;
	bool skip_link_training;
	struct dcs *dcs;
	const struct link_settings *cur;
	bool skip_video_pattern;
	uint32_t i;

	success = false;
	skip_link_training = false;
	dal_memset(&max_link_cap, '\0', sizeof(max_link_cap));
	dal_memset(&flags, '\0', sizeof(flags));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (dpsst->overridden_link_setting.lane_count !=
		LANE_COUNT_UNKNOWN)
		max_link_cap = dpsst->overridden_link_setting;
	else
		max_link_cap = dpsst->reported_link_cap;

	dcs = dal_display_path_get_dcs(path_mode->display_path);
	if (dal_dcs_get_monitor_patch_flags(dcs).flags.FORCE_LINK_RATE) {

		uint32_t link_rate = dal_dcs_get_monitor_patch_info(
			dcs,
			MONITOR_PATCH_TYPE_FORCE_LINK_RATE)->param;
		max_link_cap.link_rate = (enum link_rate)(link_rate);
	}

	/* try to train the link from high to low to
	 * find the physical link capability
	 */
	for (i = 0; i < get_link_training_fallback_table_len(ls) &&
		!success; i++) {
		cur = get_link_training_fallback_table(ls, i);

		if (failed_link_setting->lane_count != LANE_COUNT_UNKNOWN &&
			cur->link_rate >= failed_link_setting->link_rate)
			continue;


		if (!is_link_setting_supported(ls, path_mode->display_path,
			cur, &max_link_cap))
			continue;

		skip_video_pattern = true;
		if (cur->link_rate == LINK_RATE_LOW)
			skip_video_pattern = false;

		if (dal_ls_try_enable_link_base(ls, path_mode, cur)) {
			if (skip_link_training)
				success = true;
			else {
				uint8_t num_retries = 3;
				uint8_t j;
				uint8_t delay_between_retries = 10;

				for (j = 0; j < num_retries; ++j) {
					success = perform_link_training(
						ls,
						path_mode,
						cur,
						skip_video_pattern);

					if (success)
						break;

					dal_sleep_in_milliseconds(
						delay_between_retries);

					delay_between_retries += 10;
				}
			}
		}

		if (success)
			dpsst->verified_link_cap = *cur;

		/* always disable the link before trying another
		 * setting or before returning we'll enable it later
		 * based on the actual mode we're driving
		 */
		disable_link(ls, path_mode);
	}

	/* Link Training failed for all Link Settings
	 *  (Lane Count is still unknown)
	 */
	if (!success) {
		/* If all LT fails for all settings,
		 * set verified = failed safe (1 lane low)
		 */
		dpsst->verified_link_cap.lane_count = LANE_COUNT_ONE;
		dpsst->verified_link_cap.link_rate = LINK_RATE_LOW;

		dpsst->verified_link_cap.link_spread =
		LINK_SPREAD_DISABLED;
	}

	dpsst->max_link_setting = dpsst->verified_link_cap;

	/* if the verifiedLinkCap cannot
	 * accommodate previous validated mode, ask upper layer
	 * to re-enumerate mode*/
	if (dal_ls_should_send_notification(ls)) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_LINK_TRAINING,
			"%s: Link settings were reduced, sending notification for mode re-enumeration\n",
			__func__);

		/* notify lower settings applied
		 * (i.e mode reenumeration required)*/
		/* register timer interrupt to call
		 * back after setmode finished*/

		/*TODO:
		dpsst->notify_lower_setting_applied =
		m_pIrqMgr->RegisterTimerInterrupt(this, 1, &flags);*/
	}

	return success;
}

bool dal_dpsst_ls_set_overridden_trained_link_settings(
	struct link_service *ls,
	const struct link_settings *link_settings)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	ASSERT(link_settings != NULL);
	ASSERT(dpsst->reported_link_cap.lane_count != LANE_COUNT_UNKNOWN);


	if (link_settings->lane_count != LANE_COUNT_UNKNOWN) {
		uint32_t lanes;
		uint32_t rate;

		/* Overriding settings cannot be higher then
		 * trained (or reported -
		 * in case link was not trained) link settings
		 */
		if (dpsst->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN) {

			lanes = dal_min(link_settings->lane_count,
					dpsst->verified_link_cap.lane_count);
			rate  = dal_min(link_settings->link_rate,
					dpsst->verified_link_cap.link_rate);
		} else {
			lanes = dal_min(link_settings->lane_count,
					dpsst->reported_link_cap.lane_count);
			rate  = dal_min(link_settings->link_rate,
					dpsst->reported_link_cap.link_rate);
		}

		dpsst->overridden_link_setting.lane_count =
		(enum lane_count)(lanes);
		dpsst->overridden_link_setting.link_rate  =
		(enum link_rate)(rate);

		return true;
	} else {
		dpsst->overridden_link_setting.lane_count =
		link_settings->lane_count;
		dpsst->overridden_link_setting.link_rate  =
		link_settings->link_rate;

		return false;
	}

}

void dal_dpsst_ls_set_link_training_preference(
	struct link_service *ls,
	const struct link_training_preference *link_training_preference)
{
	struct dpsst_link_service *dpsst;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	if (link_training_preference != NULL &&
		dpsst->reported_link_cap.lane_count != LANE_COUNT_UNKNOWN)

		dpsst->link_training_preference = *link_training_preference;


}

struct link_training_preference dal_dpsst_ls_get_link_training_preference(
		struct link_service *ls)
{
	struct dpsst_link_service *dpsst;
	struct link_training_preference lt_preference;

	lt_preference.FAIL_LINK_TRAINING = 0;

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	return dpsst->link_training_preference;

}

uint32_t dal_dpsst_ls_bandwidth_in_kbps_from_timing(
	const struct hw_crtc_timing *timing)
{
	uint32_t bits_per_channel = 0;
	uint32_t kbps;

	switch (timing->flags.COLOR_DEPTH) {

	case HW_COLOR_DEPTH_666:
		bits_per_channel = 6;
		break;
	case HW_COLOR_DEPTH_888:
		bits_per_channel = 8;
		break;
	case HW_COLOR_DEPTH_101010:
		bits_per_channel = 10;
		break;
	case HW_COLOR_DEPTH_121212:
		bits_per_channel = 12;
		break;
	case HW_COLOR_DEPTH_141414:
		bits_per_channel = 14;
		break;
	case HW_COLOR_DEPTH_161616:
		bits_per_channel = 16;
		break;
	default:
		break;
	}
	ASSERT(bits_per_channel != 0);

	kbps = timing->pixel_clock;
	kbps *= bits_per_channel;

	if (timing->flags.Y_ONLY != 1)
		/*Only YOnly make reduce bandwidth by 1/3 compares to RGB*/
		kbps *= 3;

	return kbps;

}

uint32_t dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(
	const struct link_settings *link_setting)
{
	uint32_t link_rate_in_kbps = link_setting->link_rate *
		LINK_RATE_REF_FREQ_IN_KHZ;

	uint32_t lane_count  = link_setting->lane_count;
	uint32_t kbps = link_rate_in_kbps;

	kbps *= lane_count;
	kbps *= 8;   /* 8 bits per byte*/

	return kbps;

}

bool dal_dpsst_ls_handle_hpd_irq_link_status(
	struct link_service *ls,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	uint8_t irq_reg_rx_power_state;
	enum ddc_result dpcd_result = DDC_RESULT_UNKNOWN;
	union lane_status lane_status;
	uint32_t lane;
	bool sink_status_changed;
	bool return_code;

	sink_status_changed = false;
	return_code = false;
	/*1. Check that we can handle interrupt: Not in FS DOS,
	 *  Not in "Display Timeout" state, Link is trained.
	 */


	/* DP CTS reference sink bug: After HPD short pulse, when the SW
	 * driver read 1 bytes from 0x600, sometimes it returns 16 bytes. We
	 * should make sure SW driver checks if DPCD is successful no matter
	 * whether the DP sink work normally or not. If read from dpcd 0x600 is
	 * not successful, assume 0x600=D0.
	 */
	dpcd_result = dal_dpsst_ls_read_dpcd_data(ls,
	DPCD_ADDRESS_POWER_STATE,
	&irq_reg_rx_power_state,
	sizeof(irq_reg_rx_power_state));

	if (dpcd_result != DDC_RESULT_SUCESSFULL) {
		irq_reg_rx_power_state = DP_PWR_STATE_D0;
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_HPD_IRQ,
			"%s: DPCD read failed to obtain power state.\n",
			__func__);
	}

	if (/*TODO:dal_tm_can_process_sink_interrupt() &&*/
		irq_reg_rx_power_state == DP_PWR_STATE_D0 &&
		ls->cur_link_setting.lane_count > 0) {

		/*2. Check that Link Status changed, before re-training.*/


		/*parse lane status*/
		for (lane = 0; lane <
			(uint32_t)(ls->cur_link_setting.lane_count) &&
			!sink_status_changed; lane++) {

			/* check status of lanes 0,1
			 * changed DpcdAddress_Lane01Status (0x202)*/
			lane_status.raw = get_nibble_at_index(
			&hpd_irq_dpcd_data->bytes.lane01_status.raw,
			lane);

			if (!lane_status.bits.CHANNEL_EQ_DONE_0 ||
				!lane_status.bits.CR_DONE_0 ||
				!lane_status.bits.SYMBOL_LOCKED_0) {
				/* if one of the channel
				 * equalization, clock
				 * recovery or symbol
				 * lock is dropped
				 * consider it as
				 * (link has been
				 * dropped) dp sink
				 * status has changed
				 */
				sink_status_changed = true;
				break;
			}

		}

		/* Check interlane align.*/
		if (sink_status_changed ||
			!hpd_irq_dpcd_data->bytes.lane_Status_Updated.bits.
			INTERLANE_ALIGN_DONE) {

			dal_logger_write(ls->dal_context->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_HW_TRACE_HPD_IRQ,
				"%s: Link Status changed.\n",
				__func__);

			/* dump AUX log*/
			/*LogEntry* pLog = GetLog()->
			 Open(LogMajor_HardwareTrace,
			LogMinor_HardwareTrace_HpdIrq);
			pLog->Append("HpdIrq registers 200h-205h:");
			pLog->AppendBcd(
			pHpdIrqDpcdData->raw,
			sizeof(*pHpdIrqDpcdData));
			GetLog()->Close(pLog);*/

			/*if sink status changed, retrain link*/
			/*TODO:dal_tm_notify_sink_status_change(
			dal_ls_get_notification_display_index(ls));*/

			return_code = true;
		}
	}

	return return_code;

}

bool dal_dpsst_ls_handle_hpd_irq_device_service(
	struct link_service *ls,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	bool is_device_service_irq;
	union device_service_irq device_irq;

	is_device_service_irq = false;
	dal_memset(&device_irq, '\0', sizeof(device_irq));

	/* check content of DPCD 00201h (DpcdAddress_DeviceServiceIrqVector)*/

	if (hpd_irq_dpcd_data->bytes.device_Service_Irq.bits.AUTOMATED_TEST) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_HW_TRACE_HPD_IRQ,
			"Got auto-test request.\n");

		is_device_service_irq = true;
		/* acknowledge automated test mode*/
		dal_dpsst_ls_write_dpcd_data(
		ls,
		DPCD_ADDRESS_DEVICE_SERVICE_IRQ_VECTOR,
		&hpd_irq_dpcd_data->bytes.device_Service_Irq.raw,
		sizeof(hpd_irq_dpcd_data->bytes.device_Service_Irq.raw));

		/* handle automated test request*/
		/*TODO:handle_automated_test();*/

	}

	return is_device_service_irq;

}

void dal_dpsst_ls_set_drive_settings(
	struct link_service *ls,
	struct display_path *display_path,
	struct link_training_settings *lt_settings)
{
	/* call HWSS to set*/
	dal_hw_sequencer_set_lane_settings(
	ls->hwss, display_path, lt_settings);


	/* update DPCD*/
	dpcd_set_lane_settings(ls, lt_settings);

}

bool dal_dpsst_ls_set_test_pattern(
	struct link_service *ls,
	struct hw_path_mode *path_mode,
	enum dp_test_pattern test_pattern,
	struct link_training_settings *lt_settings,
	const uint8_t *custom_pattern,
	uint32_t cust_pattern_size)
{
	struct dpsst_link_service *dpsst;
	uint8_t link_qual_pattern[LANE_COUNT_DP_MAX]; /*for DP1.2*/
	union dpcd_training_pattern training_pattern;/* for DP1.1*/
	bool result;
	uint32_t lane;

	dal_memset(link_qual_pattern, '\0', sizeof(link_qual_pattern));
	dal_memset(&training_pattern, '\0', sizeof(training_pattern));

	dpsst = container_of(ls, struct dpsst_link_service, link_service);

	/* Reset CRTC Test Pattern if it is currently
	 * running and request is VideoMode
	 * Reset DP Phy Test Pattern if it is currently running
	 * and request is VideoMode
	 */
	if (dpsst->test_pattern_enabled &&
		DP_TEST_PATTERN_VIDEO_MODE == test_pattern) {

		/* Set CRTC Test Pattern*/
		dal_hw_sequencer_set_test_pattern(ls->hwss,
		path_mode, test_pattern,
		lt_settings, custom_pattern,
		cust_pattern_size);



		result = set_dp_phy_pattern(
		ls,
		path_mode->display_path,
		test_pattern,
		custom_pattern,
		cust_pattern_size);

		/* Unblank Stream*/
		dal_ls_unblank_stream(
		ls,
		dal_display_path_get_display_index(
		path_mode->display_path),
		path_mode);

		dal_hw_sequencer_mute_audio_endpoint(
		ls->hwss,
		path_mode->display_path,
		false);

		/* Reset Test Pattern state*/
		dpsst->test_pattern_enabled = false;

		return result;
	}

	/* Check for PHY Test Patterns*/
	if (is_dp_phy_pattern(test_pattern)) {
		enum dpcd_phy_test_patterns pattern;
		/* Set DPCD Lane Settings before running test pattern*/
		if (lt_settings != NULL) {
			dal_hw_sequencer_set_lane_settings(
			ls->hwss,
			path_mode->display_path,
			lt_settings);
			dpcd_set_lane_settings(ls, lt_settings);
		}

		/* Blank stream if running test pattern*/
		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {

			dal_hw_sequencer_mute_audio_endpoint(
			ls->hwss,
			path_mode->display_path,
			true);

			dal_ls_blank_stream(
			ls,
			dal_display_path_get_display_index(
			path_mode->display_path),
			path_mode);
		}

		result = set_dp_phy_pattern(
				ls,
				path_mode->display_path,
				test_pattern,
				custom_pattern,
				cust_pattern_size);

		if (result && test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {

			/* Set Test Pattern state*/
			dpsst->test_pattern_enabled = true;
			if (lt_settings != NULL)
				dpcd_set_link_settings(ls, lt_settings);

		}

		switch (test_pattern) {

		case DP_TEST_PATTERN_VIDEO_MODE:
			pattern = PHY_TEST_PATTERN_NONE;
			break;
		case DP_TEST_PATTERN_D102:
			pattern = PHY_TEST_PATTERN_D10_2;
			break;
		case DP_TEST_PATTERN_SYMBOL_ERROR:
			pattern = PHY_TEST_PATTERN_SYMBOL_ERROR;
			break;
		case DP_TEST_PATTERN_PRBS7:
			pattern = PHY_TEST_PATTERN_PRBS7;
			break;
		case DP_TEST_PATTERN_80BIT_CUSTOM:
			pattern = PHY_TEST_PATTERN_80BIT_CUSTOM;
			break;
		case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
			pattern = PHY_TEST_PATTERN_HBR2_COMPLIANCE_EYE;
			break;
		default:
			return result;

		}

		if (DP_TEST_PATTERN_VIDEO_MODE == test_pattern &&
			!dal_display_path_is_target_powered_on(
			path_mode->display_path))
			return result;

		if (dpsst->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {

			/* tell receiver that we are sending
			 * qualification pattern*/
			/* DP 1.2 or later - DP receiver's
			 * link quality pattern is
			 * setusing DPCD LINK_QUAL_LANEx_SET
			 * register (0x10B~0x10E)
			 */
			for (lane = 0; lane
					< LANE_COUNT_DP_MAX; lane++) {

				link_qual_pattern[lane] = (uint8_t)(pattern);
			}

			dal_dpsst_ls_write_dpcd_data(ls,
			DPCD_ADDRESS_LINK_QUAL_LANE0_SET,
			link_qual_pattern,
			sizeof(link_qual_pattern));

		} else if (dpsst->dpcd_caps.dpcd_rev.raw >= DPCD_REV_10 ||
					dpsst->dpcd_caps.dpcd_rev.raw == 0) {
			/* tell receiver that we are
			 * sending qualification pattern*/
			/* DP 1.1a or earlier -
			 * DP receiver's link quality pattern is
			 * set using DPCD TRAINING_PATTERN_SET ->
			 * LINK_QUAL_PATTERN_SET
			 * register (0x102)
			 */
			dal_dpsst_ls_read_dpcd_data(
			ls,
			DPCD_ADDRESS_TRAINING_PATTERN_SET,
			&training_pattern.raw,
			sizeof(training_pattern));

			training_pattern.bits.LINK_QUAL_PATTERN_SET = pattern;

			dal_dpsst_ls_write_dpcd_data(
			ls,
			DPCD_ADDRESS_TRAINING_PATTERN_SET,
			&training_pattern.raw,
			sizeof(training_pattern));
		}

		return result;
	} else {/* CRTC Patterns*/

		dal_hw_sequencer_set_test_pattern(
		ls->hwss, path_mode,
		test_pattern,
		lt_settings,
		custom_pattern,
		cust_pattern_size);

		/* Set Test Pattern state*/
		dpsst->test_pattern_enabled = true;

		return true;
	}

}

enum ddc_result dal_dpsst_ls_read_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	uint8_t *data,
	uint32_t size)
{
	struct ddc *ddc;
	enum ddc_result result;
	struct aux_payload payload;
	struct aux_command command;
	struct i2caux *i2caux;

	if (size > DEFAULT_AUX_MAX_DATA_SIZE) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"Attempting to read more that 16 bytes from aux\n");

		BREAK_TO_DEBUGGER();
		return DDC_RESULT_FAILED_INVALID_OPERATION;
	}

	dal_memset(&payload, '\0', sizeof(payload));
	dal_memset(&command, '\0', sizeof(command));

	i2caux = dal_adapter_service_get_i2caux(ls->adapter_service);

	ddc = dal_adapter_service_obtain_ddc(
		ls->adapter_service, ls->connector_id);


	payload.i2c_over_aux = false;
	payload.write = false;
	payload.address = address;
	payload.length = size;
	payload.data = data;

	command.payloads = &payload;
	command.number_of_payloads = 1;
	/*TODO: this should be set dynamically through
	 * the corresponding get functions
	 */
	command.defer_delay = 0;
	command.max_defer_write_retry = 0;

	result = dal_i2caux_submit_aux_command(
			i2caux, ddc, &command);

	dal_adapter_service_release_ddc(ls->adapter_service, ddc);

	return result;
}

enum ddc_result dal_dpsst_ls_write_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	const uint8_t *data,
	uint32_t size)
{
	struct ddc *ddc;
	enum ddc_result result;
	struct aux_payload payload;
	struct aux_command command;
	struct i2caux *i2caux;

	if (size > DEFAULT_AUX_MAX_DATA_SIZE) {
		dal_logger_write(ls->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_LINK_SERVICE,
			"Attempting to read more than 16 bytes from aux\n");

		BREAK_TO_DEBUGGER();
		return DDC_RESULT_FAILED_INVALID_OPERATION;
	}

	dal_memset(&payload, '\0', sizeof(payload));
	dal_memset(&command, '\0', sizeof(command));

	i2caux = dal_adapter_service_get_i2caux(ls->adapter_service);
	ddc = dal_adapter_service_obtain_ddc(
		ls->adapter_service, ls->connector_id);


	payload.i2c_over_aux = false;
	payload.write = true;
	payload.address = address;
	payload.length = size;
	payload.data = (uint8_t *)(data);

	command.payloads = &payload;
	command.number_of_payloads = 1;
	/*TODO: this should be set dynamically through
	 * the corresponding get functions
	 */
	command.defer_delay = 0;
	command.max_defer_write_retry = 0;

	result = dal_i2caux_submit_aux_command(
			i2caux, ddc, &command);

	dal_adapter_service_release_ddc(
	ls->adapter_service, ddc);

	/*TODO: should return the actual result*/
	return DDC_RESULT_SUCESSFULL;
}
