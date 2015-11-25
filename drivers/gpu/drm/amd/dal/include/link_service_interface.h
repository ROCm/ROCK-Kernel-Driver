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

#ifndef __DAL_LINK_SERVICE_INTERFACE_H__
#define __DAL_LINK_SERVICE_INTERFACE_H__

#include "include/link_service_types.h"

/* forward declaration */
struct link_service;
struct hw_crtc_timing;
struct hw_path_mode;
struct display_path;
struct hw_path_mode_set;
struct link_training_preference;
enum ddc_result;

struct link_service *dal_link_service_create(
	struct link_service_init_data *init_data);

void dal_link_service_destroy(
	struct link_service **ls);

enum link_service_type dal_ls_get_link_service_type(
	struct link_service *link_service);

bool dal_ls_validate_mode_timing(
	struct link_service *ls,
	uint32_t display_index,
	const struct hw_crtc_timing *timing,
	struct link_validation_flags flags);

bool dal_ls_get_mst_sink_info(
	struct link_service *ls,
	uint32_t display_index,
	struct mst_sink_info *sink_info);

bool dal_ls_get_gtc_sync_status(
	struct link_service *ls);

bool dal_ls_enable_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_disable_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *poath_mode);

bool dal_ls_optimized_enable_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct display_path *display_path);

void dal_ls_update_stream_features(
	struct link_service *ls,
	const struct hw_path_mode *path_mode);

bool dal_ls_blank_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_unblank_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_pre_mode_change(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_post_mode_change(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_power_on_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

bool dal_ls_power_off_stream(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode);

void dal_ls_retrain_link(
	struct link_service *ls,
	struct hw_path_mode_set *path_set);

bool dal_ls_get_current_link_setting(
	struct link_service *ls,
	struct link_settings *link_settings);

void dal_ls_connect_link(
	struct link_service *ls,
	const struct display_path *display_path,
	bool initial_detection);

void dal_ls_disconnect_link(
	struct link_service *ls);

bool dal_ls_is_mst_network_present(
	struct link_service *ls);

void dal_ls_invalidate_down_stream_devices(
	struct link_service *ls);

bool dal_ls_are_mst_displays_cofunctional(
	struct link_service *ls,
	const uint32_t *array_display_index,
	uint32_t len);

bool dal_ls_is_sink_present_at_display_index(
	struct link_service *ls,
	uint32_t display_index);

struct ddc_service *dal_ls_obtain_mst_ddc_service(
	struct link_service *ls,
	uint32_t display_index);

void dal_ls_release_mst_ddc_service(
	struct link_service *ls,
	struct ddc_service *ddc_service);

void dal_ls_release_hw(
	struct link_service *ls);

bool dal_ls_associate_link(
	struct link_service *ls,
	uint32_t display_index,
	uint32_t link_index,
	bool is_internal_link);

bool dal_dpsst_ls_set_overridden_trained_link_settings(
	struct link_service *ls,
	const struct link_settings *link_settings);

void dal_dpsst_ls_set_link_training_preference(
	struct link_service *ls,
	const struct link_training_preference *ltp);

struct link_training_preference
	dal_dpsst_ls_get_link_training_preference(
	struct link_service *ls);

bool dal_ls_should_send_notification(
	struct link_service *ls);

uint32_t dal_ls_get_notification_display_index(
	struct link_service *ls);

enum ddc_result dal_dpsst_ls_read_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	uint8_t *data,
	uint32_t size);

enum ddc_result dal_dpsst_ls_write_dpcd_data(
	struct link_service *ls,
	uint32_t address,
	const uint8_t *data,
	uint32_t size);

bool dal_ls_is_link_psr_supported(struct link_service *ls);

bool dal_ls_is_stream_drr_supported(struct link_service *ls);

void dal_ls_set_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps);

void dal_ls_get_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps);

#endif /* __DAL_LINK_SERVICE_INTERFACE_H__ */
