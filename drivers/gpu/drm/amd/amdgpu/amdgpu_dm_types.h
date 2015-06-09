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

/*TODO Jodan Hersen use the one in amdgpu_dm*/
int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct amdgpu_crtc *amdgpu_crtc,
			int display_idx);
int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
			struct amdgpu_connector *amdgpu_connector,
			int display_idx,
			bool is_connected,
			struct amdgpu_encoder *amdgpu_encoder);
int amdgpu_dm_encoder_init(struct drm_device *dev,
			struct amdgpu_encoder *amdgpu_encoder,
			int display_idx,
			struct amdgpu_crtc *amdgpu_crtc);


void amdgpu_dm_fill_surface_address(struct drm_crtc *crtc,
			struct plane_addr_flip_info *info,
			struct amdgpu_framebuffer *afb,
			struct drm_framebuffer *old_fb);

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc);
void amdgpu_dm_connector_destroy(struct drm_connector *connector);
void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder);

bool amdgpu_dm_mode_reset(struct drm_crtc *crtc);

bool amdgpu_dm_mode_set(
	struct drm_crtc *crtc,
	struct drm_display_mode *mode,
	int x,
	int y,
	struct drm_framebuffer *old_fb);

void dm_add_display_info(
	struct drm_display_info *disp_info,
	struct amdgpu_display_manager *dm,
	uint32_t display_index);

#endif		/* __AMDGPU_DM_TYPES_H__ */
