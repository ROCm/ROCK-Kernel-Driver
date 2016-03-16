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

/**
 * This file defines helper functions provided by the Display Manager to
 * Display Core.
 */
#ifndef __DM_HELPERS__
#define __DM_HELPERS__

#include "dc_types.h"
#include "dc.h"

struct dp_mst_stream_allocation_table;

enum conn_event {
	CONN_EVENT_MODE_SET,
	CONN_EVENT_DETECTION,
	CONN_EVENT_LINK_TRAINING,
	CONN_EVENT_LINK_LOSS,
	CONN_EVENT_UNDERFLOW,
};

enum dc_edid_status dm_helpers_parse_edid_caps(
	struct dc_context *ctx,
	const struct dc_edid *edid,
	struct dc_edid_caps *edid_caps);


/* Connectivity log format:
 * [time stamp]   [drm] [Major_minor] [connector name] message.....
 * eg:
 * [   26.590965] [drm] [Conn_LKTN]	  [DP-1] HBRx4 pass VS=0, PE=0^
 * [   26.881060] [drm] [Conn_Mode]	  [DP-1] {2560x1080, 2784x1111@185580Khz}^
 */

#define CONN_DATA_DETECT(link, hex_data, hex_len, ...) \
		dm_helper_conn_log(link->ctx, &link->public, hex_data, hex_len, \
				CONN_EVENT_DETECTION, ##__VA_ARGS__)

#define CONN_DATA_LINK_LOSS(link, hex_data, hex_len, ...) \
		dm_helper_conn_log(link->ctx, &link->public, hex_data, hex_len, \
				CONN_EVENT_LINK_LOSS, ##__VA_ARGS__)

#define CONN_MSG_LT(link, ...) \
		dm_helper_conn_log(link->ctx, &link->public, NULL, 0, \
				CONN_EVENT_LINK_TRAINING, ##__VA_ARGS__)

#define CONN_MSG_MODE(link, ...) \
		dm_helper_conn_log(link->ctx, &link->public, NULL, 0, \
				CONN_EVENT_MODE_SET, ##__VA_ARGS__)
/*
 * Writes payload allocation table in immediate downstream device.
 */
bool dm_helpers_dp_mst_write_payload_allocation_table(
		struct dc_context *ctx,
		const struct dc_stream *stream,
		struct dp_mst_stream_allocation_table *proposed_table,
		bool enable);

/*
 * Polls for ACT (allocation change trigger) handled and
 */
bool dm_helpers_dp_mst_poll_for_allocation_change_trigger(
		struct dc_context *ctx,
		const struct dc_stream *stream);
/*
 * Sends ALLOCATE_PAYLOAD message.
 */
bool dm_helpers_dp_mst_send_payload_allocation(
		struct dc_context *ctx,
		const struct dc_stream *stream,
		bool enable);

void dm_helpers_dp_mst_handle_mst_hpd_rx_irq(
		void *param);

bool dm_helpers_dp_mst_start_top_mgr(
		struct dc_context *ctx,
		const struct dc_link *link,
		bool boot);

void dm_helpers_dp_mst_stop_top_mgr(
		struct dc_context *ctx,
		const struct dc_link *link);

/**
 * OS specific aux read callback.
 */
bool dm_helpers_dp_read_dpcd(
		struct dc_context *ctx,
		const struct dc_link *link,
		uint32_t address,
		uint8_t *data,
		uint32_t size);

/**
 * OS specific aux write callback.
 */
bool dm_helpers_dp_write_dpcd(
		struct dc_context *ctx,
		const struct dc_link *link,
		uint32_t address,
		const uint8_t *data,
		uint32_t size);

bool dm_helpers_submit_i2c(
		struct dc_context *ctx,
		const struct dc_link *link,
		struct i2c_command *cmd);

void dm_helper_conn_log(struct dc_context *ctx,
		const struct dc_link *link,
		uint8_t *hex_data,
		int hex_data_count,
		enum conn_event event,
		const char *msg,
		...);

#endif /* __DM_HELPERS__ */
