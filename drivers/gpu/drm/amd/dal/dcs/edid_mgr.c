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

#include "include/ddc_service_types.h"

#include "edid/edid13.h"
#include "edid/edid14.h"
#include "edid/edid20.h"
#include "edid/edid_ext_cea.h"
#include "edid/edid_ext_di.h"
#include "edid/edid_ext_vtb.h"
#include "edid/edid_ext_unknown.h"

#include "edid_patch.h"
#include "edid_mgr.h"

static struct edid_base *create_edid_base_block(
	uint32_t len,
	const uint8_t *buf,
	struct timing_service *ts)
{
	struct edid_base *edid_base = NULL;

	if (dal_edid14_is_v_14(len, buf))
		edid_base = dal_edid14_create(ts, len, buf);
	else if (dal_edid13_is_v_13(len, buf))
		edid_base = dal_edid13_create(ts, len, buf);
	else if (dal_edid20_is_v_20(len, buf))
		edid_base = dal_edid20_create(ts, len, buf);

	if (edid_base)
		dal_edid_validate(edid_base);

	return edid_base;
}


static struct edid_base *create_edid_ext_block(
	uint32_t len,
	const uint8_t *buf,
	uint32_t edid_ver,
	struct timing_service *ts,
	struct edid_patch *edid_patch)
{
	struct edid_base *ext = NULL;

	if (dal_edid_ext_cea_is_cea_ext(len, buf)) {
		struct edid_ext_cea_init_data init_data;

		init_data.ts = ts;
		init_data.len = len;
		init_data.buf = buf;
		init_data.edid_patch = edid_patch;
		ext = dal_edid_ext_cea_create(&init_data);
	} else if (dal_edid_ext_di_is_di_ext(len, buf))
		ext = dal_edid_ext_di_create(ts, len, buf);
	else if (dal_edid_ext_vtb_is_vtb_ext(len, buf))
		ext = dal_edid_ext_vtb_create(ts, len, buf, edid_ver);
	else if (dal_edid_ext_unknown_is_unknown_ext(len, buf))
		ext = dal_edid_ext_unknown_create(ts, len, buf);

	if (ext)
		dal_edid_validate(ext);

	return ext;
}

static struct edid_base *create_edid_block(
	struct edid_mgr *edid_mgr,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_base *head = NULL;
	struct edid_base *edid = NULL;
	uint32_t i;

	head = create_edid_base_block(len, buf, edid_mgr->ts);
	if (!head || dal_edid_get_errors(head)->BAD_CHECKSUM) {
		BREAK_TO_DEBUGGER();
		return head;
	}

	edid = head;
	len -= dal_edid_get_size(head);
	buf += dal_edid_get_size(head);

	for (i = 0; i < dal_edid_get_num_of_extension(head); ++i) {

		struct edid_base *ext =
			create_edid_ext_block(
				len,
				buf,
				dal_edid_get_version(head),
				edid_mgr->ts,
				edid_mgr->edid_patch);

		if (!ext) {
			BREAK_TO_DEBUGGER();
			break;
		}

		dal_edid_set_next_block(edid, ext);

		len -= dal_edid_get_size(ext);
		buf += dal_edid_get_size(ext);

		edid = ext;
	}

	return head;
}

static void free_edid_handle(
	struct edid_mgr *edid_mgr,
	struct edid_handle *edid_handle)
{
	if (!edid_handle)
		return;

	if (edid_mgr->edid_list == edid_handle->edid_list)
		edid_mgr->edid_list = NULL;

	if (edid_handle->edid_list) {
		dal_edid_list_destroy(edid_handle->edid_list);
		edid_handle->edid_list = NULL;
	}

	if (edid_handle->edid_buffer) {
		dal_free(edid_handle->edid_buffer);
		edid_handle->edid_buffer = NULL;
	}

	if (edid_handle->patched_edid_buffer) {
		dal_free(edid_handle->patched_edid_buffer);
		edid_handle->patched_edid_buffer = NULL;
	}

	edid_handle->buffer_size = 0;
}


static bool edid_handle_construct(
	struct edid_mgr *edid_mgr,
	struct edid_handle *edid_handle,
	bool apply_patches)
{
	if (apply_patches &&
		dal_edid_patch_get_patches_number(edid_mgr->edid_patch) > 0)
		edid_handle->patched_edid_buffer =
			dal_alloc(edid_handle->buffer_size);

	if (edid_handle->patched_edid_buffer) {
		dal_memmove(edid_handle->patched_edid_buffer,
			edid_handle->edid_buffer,
			edid_handle->buffer_size);
		dal_edid_patch_apply(
			edid_mgr->edid_patch,
			edid_handle->patched_edid_buffer);
		edid_handle->edid_list = create_edid_block(
			edid_mgr,
			edid_handle->buffer_size,
			edid_handle->patched_edid_buffer);
	} else
		edid_handle->edid_list = create_edid_block(
			edid_mgr,
			edid_handle->buffer_size,
			edid_handle->edid_buffer);

	if (!edid_handle->edid_list)
		free_edid_handle(edid_mgr, edid_handle);

	return edid_handle->edid_list != NULL;
}

static bool edid_handle_destruct(
	struct edid_handle *edid_handle)
{
	return false;
}

static bool allocate_edid_handle(
	struct edid_mgr *edid_mgr,
	struct edid_handle *edid_handle,
	uint32_t len,
	const uint8_t *buf)
{
	free_edid_handle(edid_mgr, edid_handle);

	edid_handle->edid_buffer = dal_alloc(len);

	if (!edid_handle->edid_buffer)
		return false;

	dal_memmove(edid_handle->edid_buffer, buf, len);
	edid_handle->buffer_size = len;
	dal_edid_patch_initialize(
		edid_mgr->edid_patch,
		edid_handle->edid_buffer,
		edid_handle->buffer_size);

	return true;
}

static bool is_same_edid_raw_data(
	struct edid_handle *edid_handle,
	uint32_t len,
	const uint8_t *buf)
{
	/* We consider comparison failed when we do not have edid blocks*/
	if (buf == NULL ||
		edid_handle->edid_list == NULL ||
		len != edid_handle->buffer_size)
		return false;

	return dal_memcmp(edid_handle->edid_buffer, buf, len) == 0;
}



enum edid_retrieve_status dal_edid_mgr_override_raw_data(
	struct edid_mgr *edid_mgr,
	uint32_t len,
	const uint8_t *buf)
{
	struct edid_handle *edid_handle =
		edid_mgr->override_edid_handle.edid_list != NULL ?
				&edid_mgr->override_edid_handle :
				&edid_mgr->edid_handle;

	if (len == 0 || buf == NULL) {
		/* Request to delete override Edid*/
		if (edid_mgr->override_edid_handle.edid_list == NULL) {
			free_edid_handle(
				edid_mgr,
				&edid_mgr->override_edid_handle);
			return EDID_RETRIEVE_SAME_EDID;
		}

		/*We need to return back to physical Edid -
		 * consider it as successful override to new EDID*/
		dal_edid_patch_initialize(
			edid_mgr->edid_patch,
			edid_handle->edid_buffer,
			edid_handle->buffer_size);
		free_edid_handle(
			edid_mgr,
			&edid_mgr->override_edid_handle);
		return EDID_RETRIEVE_SUCCESS;
	}

	/* New override same as current override/physical: Nothing to do */
	if (is_same_edid_raw_data(edid_handle, len, buf))
		return EDID_RETRIEVE_SAME_EDID;

	/* Allocate buffer for override Edid and copy there new one */
	if (!allocate_edid_handle(
		edid_mgr,
		&edid_mgr->override_edid_handle,
		len,
		buf))
		return EDID_RETRIEVE_FAIL;

	/* Initialized Override Edid handle without patching it
	 * (are we sure we do not want ot patch it?) */
	if (!edid_handle_construct(
		edid_mgr, &edid_mgr->override_edid_handle, false))
		return EDID_RETRIEVE_FAIL;

	/* successfully */
	edid_mgr->edid_list = edid_mgr->override_edid_handle.edid_list;

	return EDID_RETRIEVE_SUCCESS;
}

enum edid_retrieve_status dal_edid_mgr_update_edid_raw_data(
	struct edid_mgr *edid_mgr,
	uint32_t len,
	const uint8_t *buf)
{
	enum edid_retrieve_status ret = EDID_RETRIEVE_FAIL;

	if (edid_mgr->edid_handle.edid_list)
		ret = EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS;

	/* Request to delete Edid */
	if (len == 0 || buf == NULL) {
		free_edid_handle(edid_mgr, &edid_mgr->edid_handle);
		free_edid_handle(edid_mgr, &edid_mgr->override_edid_handle);
	} else {

		/* If new Edid buffer same as current - ignore*/
		if (is_same_edid_raw_data(&edid_mgr->edid_handle, len, buf)) {
			ret = EDID_RETRIEVE_SAME_EDID;
		} else {
			/* Allocate buffer for physical Edid
			 * and copy there new one */
			if (allocate_edid_handle(
				edid_mgr,
				&edid_mgr->edid_handle, len, buf)) {
				edid_mgr->edid_list =
					edid_mgr->edid_handle.edid_list;
				ret = EDID_RETRIEVE_SUCCESS;
			}
		}

	}

	/* On failure we update previous status here, on success we will
	 * update prev status in UpdateEdidFromLastRetrieved */
	if (ret == EDID_RETRIEVE_FAIL ||
		ret == EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS)
		edid_mgr->prev_status = ret;

	return ret;
}

enum edid_retrieve_status dal_edid_mgr_update_edid_from_last_retrieved(
	struct edid_mgr *edid_mgr)
{
/* Initialize physical Edid handle including patching it. If we successfully
 * initialized - then override Edid has to be removed (otherwise it will mask
 *  new Edid), so override Edid good until we detect new monitor */

	enum edid_retrieve_status ret = EDID_RETRIEVE_FAIL;

	if (edid_mgr->edid_handle.edid_buffer != NULL) {

		if (edid_handle_construct(
			edid_mgr, &edid_mgr->edid_handle, true)) {

			edid_handle_destruct(&edid_mgr->override_edid_handle);
			ret = EDID_RETRIEVE_SUCCESS;
			edid_mgr->edid_list = edid_mgr->edid_handle.edid_list;

		} else if (edid_mgr->prev_status == EDID_RETRIEVE_SUCCESS) {

			BREAK_TO_DEBUGGER();
			ret = EDID_RETRIEVE_FAIL_WITH_PREVIOUS_SUCCESS;
		}
	} else {
		BREAK_TO_DEBUGGER();
	}

	edid_mgr->prev_status = ret;
	return ret;
}

uint32_t dal_edid_mgr_get_edid_raw_data_size(
	const struct edid_mgr *edid_mgr)
{
	if (edid_mgr->override_edid_handle.edid_list != NULL)
		return edid_mgr->override_edid_handle.buffer_size;
	else if (edid_mgr->edid_handle.edid_list != NULL)
		return edid_mgr->edid_handle.buffer_size;

	return 0;
}

const uint8_t *dal_edid_mgr_get_edid_raw_data(
	const struct edid_mgr *edid_mgr,
	uint32_t *size)
{
	const struct edid_handle *edid_handle = NULL;

	if (edid_mgr->override_edid_handle.edid_list)
		edid_handle = &edid_mgr->override_edid_handle;
	else if (edid_mgr->edid_handle.edid_list)
		edid_handle = &edid_mgr->edid_handle;

	if (!edid_handle)
		return NULL;

	if (size)
		*size = edid_handle->buffer_size;

	return edid_handle->patched_edid_buffer != NULL ?
		edid_handle->patched_edid_buffer : edid_handle->edid_buffer;
}

struct edid_base *dal_edid_mgr_get_edid(
	const struct edid_mgr *edid_mgr)
{
	return edid_mgr->edid_list;
}

const struct monitor_patch_info *dal_edid_mgr_get_monitor_patch_info(
	const struct edid_mgr *edid_mgr,
	enum monitor_patch_type type)
{
	return dal_edid_patch_get_monitor_patch_info(
		edid_mgr->edid_patch, type);
}

bool dal_edid_mgr_set_monitor_patch_info(
	struct edid_mgr *edid_mgr,
	struct monitor_patch_info *info)
{
	return dal_edid_patch_set_monitor_patch_info(
		edid_mgr->edid_patch, info);
}

union dcs_monitor_patch_flags dal_edid_mgr_get_monitor_patch_flags(
	const struct edid_mgr *edid_mgr)
{
	return dal_edid_patch_get_monitor_patch_flags(edid_mgr->edid_patch);
}

void dal_edid_mgr_update_dp_receiver_id_based_monitor_patches(
	struct edid_mgr *edid_mgr,
	struct dp_receiver_id_info *info)
{
	dal_edid_patch_update_dp_receiver_id_based_monitor_patches(
		edid_mgr->edid_patch,
		info);
}

static bool construct(struct edid_mgr *edid_mgr,
	struct timing_service *ts,
	struct adapter_service *as)
{
	edid_mgr->ts = ts;

	edid_mgr->edid_patch = dal_edid_patch_create(as);

	if (!edid_mgr->edid_patch)
		return false;

	return true;
}

struct edid_mgr *dal_edid_mgr_create(
	struct timing_service *ts,
	struct adapter_service *as)
{
	struct edid_mgr *edid_mgr = dal_alloc(sizeof(struct edid_mgr));

	if (!edid_mgr)
		return NULL;

	if (construct(edid_mgr, ts, as))
		return edid_mgr;

	dal_free(edid_mgr);
	return NULL;
}

static void destruct(struct edid_mgr *edid_mgr)
{
	free_edid_handle(edid_mgr, &edid_mgr->edid_handle);
	free_edid_handle(edid_mgr, &edid_mgr->override_edid_handle);
	dal_edid_patch_destroy(&edid_mgr->edid_patch);
}

void dal_edid_mgr_destroy(struct edid_mgr **mgr)
{

	if (!mgr || !*mgr)
		return;

	destruct(*mgr);
	dal_free(*mgr);
	*mgr = NULL;

}

