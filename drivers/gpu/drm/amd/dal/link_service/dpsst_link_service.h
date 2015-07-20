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
#ifndef __DAL_DPSST_LINK_SERVICE_H__
#define __DAL_DPSST_LINK_SERVICE_H__

#include "link_service.h"

struct interrupt_info;
union hpd_irq_data;

struct irq_registration_info {
	irq_handler_idx handler_idx;
	enum dal_irq_source src;
};

enum {
	DPCD_AMD_IEEE_TX_SIGNATURE_BYTE1 = 0x00,
	DPCD_AMD_IEEE_TX_SIGNATURE_BYTE2 = 0x00,
	DPCD_AMD_IEEE_TX_SIGNATURE_BYTE3 = 0x1A
};

enum {
	POST_LT_ADJ_REQ_LIMIT = 6,
	POST_LT_ADJ_REQ_TIMEOUT = 200
};

enum {
	LINK_TRAINING_MAX_RETRY_COUNT = 5,
	/* to avoid infinite loop where-in the receiver
	 * switches between different VS
	 */
	LINK_TRAINING_MAX_CR_RETRY = 100
};
enum gtc_sync_status {
	GTC_SYNC_STATUS_DISABLED, GTC_SYNC_STATUS_SYNC_MAINTENANCE
};

enum {
	UNKNOWN_SINK_COUNT = 0xFFFFFFFF
};

struct dpcd_caps {
	union dpcd_rev dpcd_rev;
	union max_lane_count max_ln_count;
	bool allow_invalid_MSA_timing_param;
	bool alt_scrambler_supported;
	uint32_t sink_dev_id;
	uint32_t branch_dev_id;
	int8_t branch_dev_name[6];
};

/* States of Compliance Test Specification (CTS DP1.2).*/
union compliance_test_state {
	struct {
		uint8_t STEREO_3D_RUNNING:1;
		uint8_t RESERVED:7;
	} bits;
	uint8_t raw;
};

struct link_training_preference {
	uint32_t FAIL_LINK_TRAINING:1;
};

struct tmds_converter_capability {
	uint32_t max_tmds_clock;
	uint32_t max_color_depth;
	bool port_present;
	enum dpcd_downstream_port_detailed_type port_detailed_type;
	enum dpcd_downstream_port_type port_type;
	bool use_default_caps;
};

struct dpsst_link_service {
	struct link_service link_service;

	irq_handler_idx notify_lower_setting_applied;
	struct irq_registration_info dp_sink_irq_info;
	irq_handler_idx internal_reenable_link;

	struct link_settings reported_link_cap;
	struct link_settings verified_link_cap;
	struct link_settings max_link_setting;
	struct link_settings preferred_link_setting;
	struct link_settings overridden_link_setting;

	struct lane_settings ln_setting;
	struct link_training_preference link_training_preference;

	uint32_t max_req_bw_for_verified_linkcap;

	bool test_pattern_enabled;

	struct dpcd_caps dpcd_caps;
	/*number of audio endpoint
	 * supported in sink device.
	 * Read from DPCD 0x022*/
	uint8_t num_of_audio_endpoint;
	union compliance_test_state teststate;

	/*boolean indicating GTC is synchronized or not*/
	enum gtc_sync_status gtc_status;

	/*DPCD structures for converter caps*/
	union downstream_port down_str_port;
	union dwnstream_portx_caps down_stream_caps;

	/*parses the raw data of converter caps*/
	struct tmds_converter_capability  tmds_conv_capability;

	/*To prevent display detection on a short pulse
	 * if the previous sink count is the same as the new one.*/
	uint32_t prev_sink_count;

	/*eDP related capabilities*/
	enum dpcd_edp_revision edp_revision;
	struct psr_caps psr_caps;
	struct topology_mgr *tm;
};

struct link_service *dal_dpsst_ls_create(
	struct link_service_init_data *init_data);

bool dal_dpsst_ls_try_enable_link_with_hbr2_fallback(
	struct link_service *ls,
	const struct hw_path_mode *path_mode);

bool dal_dpsst_ls_verify_link_cap(struct link_service *ls,
	const struct hw_path_mode *path_mode,
	struct link_settings *failed_link_setting);

uint32_t dal_dpsst_ls_bandwidth_in_kbps_from_timing(
	const struct hw_crtc_timing *timing);

uint32_t dal_dpsst_ls_bandwidth_in_kbps_from_link_settings(
	const struct link_settings *link_setting);

void handle_interrupt(const struct interrupt_info *interrupt_info);

bool dal_dpsst_ls_handle_hpd_irq_link_status(
	struct link_service *ls,
	union hpd_irq_data *irq_data);

bool dal_dpsst_ls_handle_hpd_irq_device_service(
	struct link_service *ls,
	union hpd_irq_data *irq_data);

enum ddc_result dal_dpsst_ls_read_hpd_irq_data(
	struct link_service *ls,
	union hpd_irq_data *irq_data);

void dal_dpsst_ls_set_drive_settings(
	struct link_service *ls,
	struct display_path *display_path,
	struct link_training_settings *lt_settings);

bool dal_dpsst_ls_set_test_pattern(
	struct link_service *ls,
	struct hw_path_mode *path_mode,
	enum dp_test_pattern test_pattern,
	struct link_training_settings *lt_settings,
	const uint8_t *custom_pattern,
	uint32_t cust_pattern_size);
#endif /*__DAL_DPSST_LINK_SERVICE_H__*/
