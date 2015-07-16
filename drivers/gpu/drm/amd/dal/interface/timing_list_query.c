/* Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/timing_list_query_interface.h"
#include "include/timing_service_interface.h"
#include "include/mode_timing_list_interface.h"
#include "include/mode_manager_interface.h"
#include "include/dal_interface.h"

struct dal_timing_list_query {
	/* instance of DAL which created this query */
	struct dal *parent_dal;
	struct timing_service *timing_srv;
	struct dcs *dcs;
	uint32_t display_index;
	/* mode timing list for display_path */
	struct mode_timing_list *mode_timing_list;
	uint32_t added_timing_count;
	struct display_pixel_encoding_support pes;

	/* following var is used for caching. In DAL2 it static variable inside
	 * function. Was decided to refactor it */
	struct display_color_depth_support cds;
};


/******************************************************************************
 *	Private functions
 *****************************************************************************/

static bool construct(
	struct dal_timing_list_query *tlsq,
	struct timing_list_query_init_data *init_data)
{
	tlsq->dcs = init_data->dcs;
	tlsq->display_index = init_data->display_index;
	tlsq->parent_dal = init_data->dal;
	tlsq->timing_srv = init_data->timing_srv;

	tlsq->mode_timing_list =
			dal_timing_service_get_mode_timing_list_for_path(
					tlsq->timing_srv,
					tlsq->display_index);

	return NULL != tlsq->mode_timing_list;
}

static void destruct(struct dal_timing_list_query *tlsq)
{
	if (tlsq->added_timing_count) {
		dal_mode_manager_update_disp_path_func_view_tbl(
			dal_get_mode_manager(tlsq->parent_dal),
			tlsq->display_index,
			dal_timing_service_get_mode_timing_list_for_path(
				tlsq->timing_srv,
				tlsq->display_index));

		dal_timing_service_dump(tlsq->timing_srv,
				tlsq->display_index);
	}
}

/**
 *****************************************************************************
 *  Function: get_next_display_supported_pixel_encoding()
 *
 *     helper function to get next supported pixelEncoding from DCS data
 *
 *  @param [in] update_from_dcs	- get from DCS or use cache
 *  @param [in] pe		- the specific PixelEncoding
 *
 *  @return
 *    return true if found
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */

static bool get_next_display_supported_pixel_encoding(
		struct dal_timing_list_query *tlsq,
		bool update_from_dcs,
		enum pixel_encoding pe)
{
	bool ret = true;
	/* TODO: can not complete because of dependency on DCS.
	* Finish when DCS is ready. */
	return ret;
}

/******************************************************************************
 *	Public interface implementation.
 *****************************************************************************/
struct dal_timing_list_query *dal_timing_list_query_create(
		struct timing_list_query_init_data *init_data)
{
	struct dal_timing_list_query *tlsq;

	tlsq = dal_alloc(sizeof(*tlsq));
	if (!tlsq)
		return NULL;

	if (construct(tlsq, init_data))
		return tlsq;

	BREAK_TO_DEBUGGER();
	dal_free(tlsq);
	return NULL;
}

void dal_timing_list_query_destroy(struct dal_timing_list_query **tlsq)
{
	if (!tlsq || !*tlsq) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct(*tlsq);

	dal_free(*tlsq);

	*tlsq = NULL;
}

/* Get count of mode timings in the list. */
uint32_t dal_timing_list_query_get_mode_timing_count(
	const struct dal_timing_list_query *tlsq)
{
	if (NULL == tlsq) {
		/* getting here if OS ignores error in
		 * dal_timing_list_query_create() */
		BREAK_TO_DEBUGGER();
		return 0;
	}

	return dal_mode_timing_list_get_count(tlsq->mode_timing_list);
}

const struct mode_timing *dal_timing_list_query_get_mode_timing_at_index(
	const struct dal_timing_list_query *tlsq,
	uint32_t index)
{
	if (NULL == tlsq) {
		/* getting here if OS ignores error in
		 * dal_timing_list_query_create() */
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	return dal_mode_timing_list_get_timing_at_index(
		tlsq->mode_timing_list, index);
}

static enum display_color_depth display_color_depth_from_dcs_color_depth(
	enum color_depth_index color_depth)
{
	switch (color_depth) {
	case COLOR_DEPTH_INDEX_666:
		return DISPLAY_COLOR_DEPTH_666;
	case COLOR_DEPTH_INDEX_888:
		return DISPLAY_COLOR_DEPTH_888;
	case COLOR_DEPTH_INDEX_101010:
		return DISPLAY_COLOR_DEPTH_101010;
	case COLOR_DEPTH_INDEX_121212:
		return DISPLAY_COLOR_DEPTH_121212;
	case COLOR_DEPTH_INDEX_141414:
		return DISPLAY_COLOR_DEPTH_141414;
	case COLOR_DEPTH_INDEX_161616:
		return DISPLAY_COLOR_DEPTH_161616;
	default:
		return DISPLAY_COLOR_DEPTH_UNDEFINED;
	}
}

static bool get_next_color_depth_from_dcs_support(
	struct display_color_depth_support *cds,
	enum display_color_depth current_cd,
	enum display_color_depth *next_cd)
{
	enum color_depth_index i;
	bool next_bit = (current_cd == DISPLAY_COLOR_DEPTH_UNDEFINED);

	for (i = COLOR_DEPTH_INDEX_666; i < COLOR_DEPTH_INDEX_LAST; i <<= 1) {
		if ((cds->mask & i) == 0)
			continue;

		if (next_bit) {
			*next_cd = display_color_depth_from_dcs_color_depth(i);
			if (*next_cd > DISPLAY_COLOR_DEPTH_888 &&
				cds->deep_color_native_res_only)
				continue;

			return true;
		}

		/* found current bit */
		if (current_cd == display_color_depth_from_dcs_color_depth(i))
			next_bit = true;
	}

	return false;
}

static bool get_next_display_supported_color_depth(
	struct dal_timing_list_query *tlsq,
	bool update_from_dcs,
	enum display_color_depth *cd)
{
	bool ret = true;

	/* we update the data from DCS on demand
	 * so we can re-use the cached data
	 */
	if (update_from_dcs) {
		tlsq->cds.mask = 0;
		ret = dal_dcs_get_display_color_depth(tlsq->dcs, &tlsq->cds);
	}

	if (ret)
		ret = get_next_color_depth_from_dcs_support(
			&tlsq->cds, *cd, cd);

	return ret;
}

/**
  *****************************************************************************
  * timing_list_query_add_timing() is used to add timing to a target timing
  * mode list.
  * In case device disconnected, the previously added modes will stay in
  * the target mode list until a newly connected device arrival and added
  * timing will be removed automatically by DAL.
  *****************************************************************************
  */
bool dal_timing_list_query_add_timing(struct dal_timing_list_query *tlsq,
		const struct crtc_timing *crtc_timing,
		enum timing_support_level support_level)
{
	enum timing_source ts;
	struct mode_timing mt;
	/* we use this boolean to track whether we need to ask DCS for
	 * the information */
	bool query_dcs = true;
	bool ret;

	/* TODO: for wireless always return false, without adding
	 * the mode timing. */

	/* set proper timing source */
	if (TIMING_SUPPORT_LEVEL_NOT_GUARANTEED == support_level ||
			TIMING_SUPPORT_LEVEL_UNDEFINED == support_level) {
		ts = TIMING_SOURCE_DALINTERFACE_IMPLICIT;
	} else if (TIMING_SUPPORT_LEVEL_GUARANTEED == support_level ||
			TIMING_SUPPORT_LEVEL_NATIVE == support_level) {
		ts = TIMING_SOURCE_DALINTERFACE_EXPLICIT;
	} else {
		/* bad input */
		return false;
	}

	dal_memset(&mt, 0, sizeof(mt));

	mt.crtc_timing = *crtc_timing;
	mt.mode_info.timing_source = ts;

	/* call TS helper function to create the 'mt.mode_info' */
	dal_timing_service_create_mode_info_from_timing(crtc_timing,
			&mt.mode_info);

	mt.crtc_timing.vic = dal_timing_service_get_video_code_for_timing(
			tlsq->timing_srv, crtc_timing);

	/* Caller may not be able to provide pixel encoding or colour depth
	 * information. If such case occurs, we need to query DCS for
	 * the support */

	if (mt.crtc_timing.display_color_depth != DISPLAY_COLOR_DEPTH_UNDEFINED
			&& mt.crtc_timing.pixel_encoding
					!= PIXEL_ENCODING_UNDEFINED) {
		/* all defined, we can go ahead call TS */
		ret = dal_timing_service_add_mode_timing_to_path(
				tlsq->timing_srv, tlsq->display_index, &mt);
	} else if (mt.crtc_timing.display_color_depth
			!= DISPLAY_COLOR_DEPTH_UNDEFINED) {
		/* mt.crtc_timing.pixel_encoding is NOT defined.
		 * We need to get from DCS the supported pixel encoding. */
		enum pixel_encoding pe;

		query_dcs = true;
		pe = PIXEL_ENCODING_UNDEFINED;

		while (get_next_display_supported_pixel_encoding(tlsq,
				query_dcs, pe)) {
			mt.crtc_timing.pixel_encoding = pe;
			query_dcs = false;

			/* call TS to add mode timing */
			ret = dal_timing_service_add_mode_timing_to_path(
					tlsq->timing_srv, tlsq->display_index,
					&mt);
		}
	} else {
		/* both display color depth and pixel encoding not defined
		 * we need to get from DCS the supported
		 */
		enum pixel_encoding pe = PIXEL_ENCODING_UNDEFINED;

		query_dcs = true;
		while (get_next_display_supported_pixel_encoding(
			tlsq,
			query_dcs,
			pe)) {
			enum display_color_depth cd =
				DISPLAY_COLOR_DEPTH_UNDEFINED;
			mt.crtc_timing.pixel_encoding = pe;
			while (get_next_display_supported_color_depth(
				tlsq, query_dcs, &cd)) {
				mt.crtc_timing.display_color_depth = cd;
				query_dcs = false;

				if (dal_timing_service_add_mode_timing_to_path(
					tlsq->timing_srv,
					tlsq->display_index,
					&mt))
					ret = true;
			}

			if (query_dcs)
				/* looks like color depth query failed, no need
				 * to continue */
				break;
		}
	}

	if (ret) {
		dal_mode_manager_update_disp_path_func_view_tbl(
			dal_get_mode_manager(tlsq->parent_dal),
			tlsq->display_index,
			dal_timing_service_get_mode_timing_list_for_path(
				tlsq->timing_srv,
				tlsq->display_index));
		tlsq->added_timing_count++;
	}

	return ret;
}
