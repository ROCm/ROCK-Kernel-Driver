/*
 * Copyright 2012-13 Advanced Micro Devices, Inc.
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


#ifndef __AMDGPU_DM_TYPES_H__
#define __AMDGPU_DM_TYPES_H__

#include <drm/drmP.h>

struct plane_addr_flip_info;
struct amdgpu_framebuffer;
struct amdgpu_display_manager;
struct dc_validation_set;
struct dc_surface;

/*TODO Jodan Hersen use the one in amdgpu_dm*/
int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct amdgpu_crtc *amdgpu_crtc,
			uint32_t link_index);
int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
			struct amdgpu_connector *amdgpu_connector,
			uint32_t link_index,
			struct amdgpu_encoder *amdgpu_encoder);
int amdgpu_dm_encoder_init(struct drm_device *dev,
			struct amdgpu_encoder *amdgpu_encoder,
			uint32_t link_index,
			struct amdgpu_crtc *amdgpu_crtc);

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc);
void amdgpu_dm_connector_destroy(struct drm_connector *connector);
void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder);

void dm_add_display_info(
	struct drm_display_info *disp_info,
	struct amdgpu_display_manager *dm,
	uint32_t display_index);

int amdgpu_dm_connector_get_modes(struct drm_connector *connector);

struct amdgpu_connector *aconnector_from_drm_crtc(
		struct drm_crtc *crtc,
		struct drm_atomic_state *state);

int amdgpu_dm_atomic_commit(
	struct drm_device *dev,
	struct drm_atomic_state *state,
	bool async);
int amdgpu_dm_atomic_check(struct drm_device *dev,
			    	struct drm_atomic_state *state);

int dm_create_validation_set_for_target(
	struct drm_connector *connector,
	struct drm_display_mode *mode,
	struct dc_validation_set *val_set);
int dm_add_surface_to_validation_set(
	struct drm_plane *plane,
	struct drm_plane_state *state,
	struct dc_surface **surface);

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector);
struct drm_connector_state *amdgpu_dm_connector_atomic_duplicate_state(
	struct drm_connector *connector);
void amdgpu_dm_connector_atomic_destroy_state(
	struct drm_connector *connector,
	struct drm_connector_state *state);
int amdgpu_dm_connector_atomic_set_property(
	struct drm_connector *connector,
	struct drm_connector_state *state,
	struct drm_property *property,
	uint64_t val);

extern const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs;

#endif		/* __AMDGPU_DM_TYPES_H__ */
