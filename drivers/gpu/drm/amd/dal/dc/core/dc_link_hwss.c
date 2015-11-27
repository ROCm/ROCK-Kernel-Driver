/* Copyright 2015 Advanced Micro Devices, Inc. */

#include "dc_services.h"
#include "dc.h"
#include "inc/core_dc.h"
#include "include/ddc_service_types.h"
#include "include/i2caux_interface.h"
#include "link_hwss.h"
#include "include/connector_interface.h"
#include "hw_sequencer.h"
#include "include/ddc_service_interface.h"

enum dc_status core_link_read_dpcd(
	struct core_link* link,
	uint32_t address,
	uint8_t *data,
	uint32_t size)
{
	if (dal_ddc_service_read_dpcd_data(link->ddc, address, data, size)
			!= DDC_RESULT_SUCESSFULL)
		return DC_ERROR_UNEXPECTED;

	return DC_OK;
}

enum dc_status core_link_write_dpcd(
	struct core_link* link,
	uint32_t address,
	const uint8_t *data,
	uint32_t size)
{
	if (dal_ddc_service_write_dpcd_data(link->ddc, address, data, size)
			!= DDC_RESULT_SUCESSFULL)
		return DC_ERROR_UNEXPECTED;

	return DC_OK;
}

void dp_receiver_power_ctrl(struct core_link *link, bool on)
{
	uint8_t state;

	state = on ? DP_POWER_STATE_D0 : DP_POWER_STATE_D3;

	core_link_write_dpcd(link, DPCD_ADDRESS_POWER_STATE, &state,
			sizeof(state));
}


/* TODO: HBR2 need raise clock for DP link training */
enum dc_status dp_enable_link_phy(
	struct core_link *link,
	enum signal_type signal,
	enum engine_id engine,
	const struct link_settings *link_settings)
{
	enum dc_status status = DC_OK;

	if (link->dc->hwss.encoder_enable_output(
					link->link_enc,
					link_settings,
					engine,
					CLOCK_SOURCE_ID_EXTERNAL,
					signal,
					COLOR_DEPTH_UNDEFINED,
					0) != ENCODER_RESULT_OK)
		status = DC_ERROR_UNEXPECTED;

	dp_receiver_power_ctrl(link, true);

	return status;
}

void dp_disable_link_phy(struct core_link *link, enum signal_type signal)
{
	if (!link->dp_wa.bits.KEEP_RECEIVER_POWERED)
		dp_receiver_power_ctrl(link, false);

	link->dc->hwss.encoder_disable_output(link->link_enc, signal);

	/* Clear current link setting.*/
	dc_service_memset(&link->cur_link_settings, 0,
			sizeof(link->cur_link_settings));
}

void dp_disable_link_phy_mst(struct core_link *link, struct core_stream *stream)
{
	int i, j;

	for (i = 0; i < link->enabled_stream_count; i++) {
		if (link->enabled_streams[i] == stream) {
			link->enabled_stream_count--;
			for (j = i; i < link->enabled_stream_count; j++)
				link->enabled_streams[j] = link->enabled_streams[j+1];
		}
	}
	/* MST disable link only when no stream use the link */
	if (link->enabled_stream_count > 0) {
		return;
	}

	if (!link->dp_wa.bits.KEEP_RECEIVER_POWERED)
		dp_receiver_power_ctrl(link, false);

	link->dc->hwss.encoder_disable_output(link->link_enc, stream->signal);

	/* Clear current link setting.*/
	dc_service_memset(&link->cur_link_settings, 0,
			sizeof(link->cur_link_settings));
}

bool dp_set_hw_training_pattern(
	struct core_link *link,
	enum hw_dp_training_pattern pattern)
{
	enum dp_test_pattern test_pattern = DP_TEST_PATTERN_UNSUPPORTED;
	struct encoder_set_dp_phy_pattern_param pattern_param = {0};
	struct link_encoder *encoder = link->link_enc;

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

	pattern_param.dp_phy_pattern = test_pattern;
	pattern_param.custom_pattern = NULL;
	pattern_param.custom_pattern_size = 0;
	pattern_param.dp_panel_mode = dp_get_panel_mode(link);

	link->ctx->dc->hwss.encoder_set_dp_phy_pattern(encoder, &pattern_param);

	return true;
}


bool dp_set_hw_lane_settings(
	struct core_link *link,
	const struct link_training_settings *link_settings)
{
	struct link_encoder *encoder = link->link_enc;

	/* call Encoder to set lane settings */
	link->ctx->dc->hwss.encoder_dp_set_lane_settings(encoder, link_settings);

	return true;
}

enum dp_panel_mode dp_get_panel_mode(struct core_link *link)
{
	/* We need to explicitly check that connector
	 * is not DP. Some Travis_VGA get reported
	 * by video bios as DP.
	 */
	if (link->public.connector_signal != SIGNAL_TYPE_DISPLAY_PORT) {

		switch (link->dpcd_caps.branch_dev_id) {
		case DP_BRANCH_DEVICE_ID_2:
			if (strncmp(
				link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_2,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
				return DP_PANEL_MODE_SPECIAL;
			}
			break;
		case DP_BRANCH_DEVICE_ID_3:
			if (strncmp(link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_3,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
				return DP_PANEL_MODE_SPECIAL;
			}
			break;
		default:
			break;
		}

		if (link->dpcd_caps.panel_mode_edp) {
			return DP_PANEL_MODE_EDP;
		}
	}

	return DP_PANEL_MODE_DEFAULT;
}

void dp_set_hw_test_pattern(
	struct core_link *link,
	enum dp_test_pattern test_pattern)
{
	struct encoder_set_dp_phy_pattern_param pattern_param = {0};
	struct link_encoder *encoder = link->link_enc;

	pattern_param.dp_phy_pattern = test_pattern;
	pattern_param.custom_pattern = NULL;
	pattern_param.custom_pattern_size = 0;
	pattern_param.dp_panel_mode = dp_get_panel_mode(link);

	link->ctx->dc->hwss.encoder_set_dp_phy_pattern(encoder, &pattern_param);
}

