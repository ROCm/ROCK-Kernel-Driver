/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <linux/string.h>
#include <linux/acpi.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_edid.h>

#include "amdgpu.h"
#include "dc.h"
#include "dc_services.h"

#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_types.h"

/* dc_helpers_parse_edid_caps
 *
 * Parse edid caps
 *
 * @edid:	[in] pointer to edid
 *  edid_caps:	[in] pointer to edid caps
 * @return
 *	void
 * */
enum dc_edid_status dc_helpers_parse_edid_caps(
		struct dc_context *ctx,
		const struct dc_edid *edid,
		struct dc_edid_caps *edid_caps)
{
	struct edid *edid_buf = (struct edid *) edid->raw_edid;
	struct cea_sad *sads;
	int sad_count = -1;
	int sadb_count = -1;
	int i = 0;
	int j = 0;
	uint8_t *sadb = NULL;

	enum dc_edid_status result = EDID_OK;

	if (!edid_caps || !edid)
		return EDID_BAD_INPUT;

	if (!drm_edid_is_valid(edid_buf))
		result = EDID_BAD_CHECKSUM;

	edid_caps->manufacturer_id = (uint16_t) edid_buf->mfg_id[0] |
					((uint16_t) edid_buf->mfg_id[1])<<8;
	edid_caps->product_id = (uint16_t) edid_buf->prod_code[0] |
					((uint16_t) edid_buf->prod_code[1])<<8;
	edid_caps->serial_number = edid_buf->serial;
	edid_caps->manufacture_week = edid_buf->mfg_week;
	edid_caps->manufacture_year = edid_buf->mfg_year;

	/* One of the four detailed_timings stores the monitor name. It's
	 * stored in an array of length 13. */
	for (i = 0; i < 4; i++) {
		if (edid_buf->detailed_timings[i].data.other_data.type == 0xfc) {
			while (edid_buf->detailed_timings[i].data.other_data.data.str.str[j] && j < 13) {
				if (edid_buf->detailed_timings[i].data.other_data.data.str.str[j] == '\n')
					break;

				edid_caps->display_name[j] =
					edid_buf->detailed_timings[i].data.other_data.data.str.str[j];
				j++;
			}
		}
	}

	sad_count = drm_edid_to_sad((struct edid *) edid->raw_edid, &sads);
	if (sad_count <= 0) {
		DRM_INFO("SADs count is: %d, don't need to read it\n",
				sad_count);
		return result;
	}

	edid_caps->audio_mode_count = sad_count < DC_MAX_AUDIO_DESC_COUNT ? sad_count : DC_MAX_AUDIO_DESC_COUNT;
	for (i = 0; i < edid_caps->audio_mode_count; ++i) {
		struct cea_sad *sad = &sads[i];

		edid_caps->audio_modes[i].format_code = sad->format;
		edid_caps->audio_modes[i].channel_count = sad->channels;
		edid_caps->audio_modes[i].sample_rate = sad->freq;
		edid_caps->audio_modes[i].sample_size = sad->byte2;
	}

	sadb_count = drm_edid_to_speaker_allocation((struct edid *) edid->raw_edid, &sadb);

	if (sadb_count < 0) {
		DRM_ERROR("Couldn't read Speaker Allocation Data Block: %d\n", sadb_count);
		sadb_count = 0;
	}

	if (sadb_count)
		edid_caps->speaker_flags = sadb[0];
	else
		edid_caps->speaker_flags = DEFAULT_SPEAKER_LOCATION;

	kfree(sads);
	kfree(sadb);

	return result;
}


static struct amdgpu_connector *get_connector_for_sink(
	struct drm_device *dev,
	const struct dc_sink *sink)
{
	struct drm_connector *connector;
	struct amdgpu_connector *aconnector = NULL;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);
		if (aconnector->dc_sink == sink)
			break;
	}

	return aconnector;
}

static struct amdgpu_connector *get_connector_for_link(
	struct drm_device *dev,
	const struct dc_link *link)
{
	struct drm_connector *connector;
	struct amdgpu_connector *aconnector = NULL;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);
		if (aconnector->dc_link == link)
			break;
	}

	return aconnector;
}

/*
 * Writes payload allocation table in immediate downstream device.
 */
bool dc_helpers_dp_mst_write_payload_allocation_table(
		struct dc_context *ctx,
		const struct dc_stream *stream,
		struct dp_mst_stream_allocation_table *table,
		bool enable)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;
	struct amdgpu_crtc *amdgpu_crtc;
	struct drm_crtc *crtc;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;
	int slots = 0;
	bool ret;
	int clock;
	int bpp;
	int pbn = 0;
	uint8_t i;
	uint8_t vcid = 0;
	bool find_stream_for_sink;

	aconnector = get_connector_for_sink(dev, stream->sink);
	crtc = aconnector->base.state->crtc;

	if (!crtc)
		return false;

	if (!aconnector->mst_port)
		return false;

	mst_mgr = &aconnector->mst_port->mst_mgr;
	mst_port = aconnector->port;

	if (enable) {
		clock = crtc->state->mode.clock;
		/* TODO remove following hardcode value */
		bpp = 30;

		/* TODO need to know link rate */

		pbn = drm_dp_calc_pbn_mode(clock, bpp);

		ret = drm_dp_mst_allocate_vcpi(mst_mgr, mst_port, pbn, &slots);
		/* mst_port->vcpi.vcpi is vc_id for this stream.*/
		vcid = mst_port->vcpi.vcpi;

		if (!ret)
			return false;

	} else {
		drm_dp_mst_reset_vcpi_slots(mst_mgr, mst_port);
	}

	ret = drm_dp_update_payload_part1(mst_mgr);

	/* mst_mgr->->payloads are VC payload notify MST branch using DPCD or
	 * AUX message. The sequence is slot 1-63 allocated sequence for each
	 * stream. AMD ASIC stream slot allocation should follow the same
	 * sequence. copy DRM MST allocation to dc */

	mutex_lock(&mst_mgr->payload_lock);

	/* number of active streams */
	for (i = 0; i < mst_mgr->max_payloads; i++) {
		if (mst_mgr->payloads[i].num_slots == 0)
			break;
	}

	table->stream_count = i;

	for (i = 0; i < table->stream_count; i++) {
		table->stream_allocations[i].slot_count =
				mst_mgr->proposed_vcpis[i]->num_slots;
		/*
		 * mst_mgr->pbn_div is fixed value after link training for
		 * current link PHY
		 */
		table->stream_allocations[i].pbn_per_slot = mst_mgr->pbn_div;

		/*
		 * find which payload is for current stream after
		 * drm_dp_update_payload_part1, payload and proposed_vcpis
		 * are sync to the same allocation sequence. vcpi is not saved
		 * into payload by drm_dp_update_payload_part1. In order to
		 * find sequence of a payload within allocation sequence, we
		 * need check vcpi from proposed_vcpis
		 */

		table->stream_allocations[i].pbn =
				mst_mgr->proposed_vcpis[i]->pbn;

		if (mst_mgr->proposed_vcpis[i]->vcpi == vcid)
			table->cur_stream_payload_idx = i;

		find_stream_for_sink = false;

		list_for_each_entry(
			connector,
			&dev->mode_config.connector_list,
			head) {
			const struct dc_sink *dc_sink;
			struct dc_target *dc_target;
			uint8_t j;

			aconnector = to_amdgpu_connector(connector);

			/* not mst connector */
			if (!aconnector->mst_port)
				continue;

			mst_port = aconnector->port;

			if (mst_port->vcpi.vcpi !=
					mst_mgr->proposed_vcpis[i]->vcpi)
				continue;

			/* find connector with same vcid as payload */

			dc_sink = aconnector->dc_sink;

			/*
			 * find stream to drive this sink
			 * crtc -> target -> stream -> sink
			 */
			crtc = aconnector->base.state->crtc;
			amdgpu_crtc = to_amdgpu_crtc(crtc);
			dc_target = amdgpu_crtc->target;

			for (j = 0; j < dc_target->stream_count; j++) {
				if (dc_target->streams[j]->sink ==
					dc_sink)
					break;
			}

			if (j < dc_target->stream_count) {
				/*
				 * find sink --> stream --> target -->
				 * connector
				 */
				table->stream_allocations[i].stream =
					dc_target->streams[j];
				/* exit loop connector */
				find_stream_for_sink = true;
				break;
			}
		}

		if (!find_stream_for_sink) {
			/*
			 * TODO: do not find stream for sink. This should not
			 * happen
			 */
			ASSERT(0);
		}
	}

	mutex_unlock(&mst_mgr->payload_lock);

	if (ret)
		return false;

	return true;
}

/*
 * Polls for ACT (allocation change trigger) handled and sends
 * ALLOCATE_PAYLOAD message.
 */
bool dc_helpers_dp_mst_poll_for_allocation_change_trigger(
		struct dc_context *ctx,
		const struct dc_stream *stream)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct amdgpu_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	int ret;

	aconnector = get_connector_for_sink(dev, stream->sink);

	if (!aconnector->mst_port)
		return false;

	mst_mgr = &aconnector->mst_port->mst_mgr;

	ret = drm_dp_check_act_status(mst_mgr);

	if (ret)
		return false;

	return true;
}

bool dc_helpers_dp_mst_send_payload_allocation(
		struct dc_context *ctx,
		const struct dc_stream *stream,
		bool enable)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct amdgpu_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;
	int ret;

	aconnector = get_connector_for_sink(dev, stream->sink);

	mst_port = aconnector->port;

	if (!aconnector->mst_port)
		return false;

	mst_mgr = &aconnector->mst_port->mst_mgr;

	ret = drm_dp_update_payload_part2(mst_mgr);

	if (ret)
		return false;

	if (!enable)
		drm_dp_mst_deallocate_vcpi(mst_mgr, mst_port);

	return true;
}

void dc_helpers_dp_mst_handle_mst_hpd_rx_irq(void *param)
{
	uint8_t esi[8] = { 0 };
	uint8_t dret;
	bool new_irq_handled = true;
	struct amdgpu_connector *aconnector = (struct amdgpu_connector *)param;

	/* DPCD 0x2002 - 0x2008 for down stream IRQ from MST, eDP etc. */
	dret = drm_dp_dpcd_read(
		&aconnector->dm_dp_aux.aux,
		DP_SINK_COUNT_ESI, esi, 8);

	while ((dret == 8) && new_irq_handled) {
		uint8_t retry;

		DRM_DEBUG_KMS("ESI %02x %02x %02x\n", esi[0], esi[1], esi[2]);

		/* handle HPD short pulse irq */
		drm_dp_mst_hpd_irq(&aconnector->mst_mgr, esi, &new_irq_handled);

		if (new_irq_handled) {
			/* ACK at DPCD to notify down stream */
			for (retry = 0; retry < 3; retry++) {
				uint8_t wret;

				wret = drm_dp_dpcd_write(
					&aconnector->dm_dp_aux.aux,
					DP_SINK_COUNT_ESI + 1,
					&esi[1],
					3);
				if (wret == 3)
					break;
			}

			/* check if there is new irq to be handle */
			dret = drm_dp_dpcd_read(
				&aconnector->dm_dp_aux.aux,
				DP_SINK_COUNT_ESI, esi, 8);
		}
	}
}

bool dc_helpers_dp_mst_start_top_mgr(
		struct dc_context *ctx,
		const struct dc_link *link)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct amdgpu_connector *aconnector = get_connector_for_link(dev, link);

	aconnector->is_mst_connector = true;

	return true;
}

void dc_helpers_dp_mst_stop_top_mgr(
		struct dc_context *ctx,
		const struct dc_link *link)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct drm_device *dev = adev->ddev;
	struct amdgpu_connector *aconnector = get_connector_for_link(dev, link);

	drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, false);

	aconnector->is_mst_connector = false;
}
