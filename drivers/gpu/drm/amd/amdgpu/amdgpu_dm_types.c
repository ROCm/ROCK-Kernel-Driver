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

#include "dal_services_types.h"

#include <linux/types.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_pm.h"
// We need to #undef FRAME_SIZE and DEPRECATED because they conflict
// with ptrace-abi.h's #define's of them.
#undef FRAME_SIZE
#undef DEPRECATED

#include "amdgpu_dm_types.h"

#include "include/dal_interface.h"
#include "include/timing_service_types.h"
#include "include/set_mode_interface.h"
#include "include/mode_query_interface.h"
#include "include/dcs_types.h"
#include "include/mode_manager_interface.h"
#include "include/mode_manager_types.h"

/*#include "amdgpu_buffer.h"*/

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.reset = NULL,
	.destroy = amdgpu_dm_encoder_destroy,
};

static void init_dal_topology(
	struct amdgpu_display_manager *dm,
	struct topology *tp,
	uint32_t current_display_index)
{
	DRM_DEBUG_KMS("current_display_index: %d\n", current_display_index);

/* TODO: the following code is used for clone mode, uncomment in case needed
	uint32_t current_display_index;
	uint32_t connected_displays_vector =
		dal_get_connected_targets_vector(dm->dal);
	uint32_t total_connected_displays = 0;

	for (current_display_index = 0;
		connected_displays_vector != 0;
		connected_displays_vector >>= 1,
		++current_display_index) {
		if ((connected_displays_vector & 1) == 1) {
			tp->display_index[total_connected_displays] =
				current_display_index;
			++total_connected_displays;
		}
	} */

	tp->display_index[0] = current_display_index;
	tp->disp_path_num = 1;
}


static enum pixel_format convert_to_dal_pixel_format(uint32_t drm_pf)
{
	switch (drm_pf) {
	case DRM_FORMAT_RGB888:
		return PIXEL_FORMAT_INDEX8;
	case DRM_FORMAT_RGB565:
		return PIXEL_FORMAT_RGB565;
	case DRM_FORMAT_ARGB8888:
		return PIXEL_FORMAT_ARGB8888;
	case DRM_FORMAT_ARGB2101010:
		return PIXEL_FORMAT_ARGB2101010;
	default:
		return PIXEL_FORMAT_ARGB8888;
	}
}

/* TODO: Use DAL define if available */
#define DAL_MODE_FLAG_VIDEO_OPTIMIZED (1 << 0)

/*TODO define max plane nums*/
#define AMDGPU_PFLIP_IRQ_SRC_NUM  6


static int dm_set_cursor(struct drm_crtc *crtc, struct drm_gem_object *obj,
		uint32_t width, uint32_t height)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_bo *robj;
	struct cursor_attributes attributes;
	struct amdgpu_device *adev = crtc->dev->dev_private;
	uint64_t gpu_addr;
	int ret;

	if ((width > amdgpu_crtc->max_cursor_width) ||
	    (height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	robj = gem_to_amdgpu_bo(obj);
	ret = amdgpu_bo_reserve(robj, false);
	if (unlikely(ret != 0))
		return ret;
	ret = amdgpu_bo_pin_restricted(robj, AMDGPU_GEM_DOMAIN_VRAM,
				       0, 0, &gpu_addr);
	amdgpu_bo_unreserve(robj);
	if (ret)
		return ret;

	amdgpu_crtc->cursor_width = width;
	amdgpu_crtc->cursor_height = height;

	attributes.address.high_part = upper_32_bits(gpu_addr);
	attributes.address.low_part = lower_32_bits(gpu_addr);
	attributes.width = width-1;
	attributes.height = height-1;
	attributes.x_hot = 0;
	attributes.y_hot = 0;
	attributes.color_format = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle = 0;
	attributes.attribute_flags.value = 0;

	dal_set_cursor_attributes(adev->dm.dal, amdgpu_crtc->crtc_id, &attributes);

	return 0;
}


static int dm_crtc_cursor_set(struct drm_crtc *crtc,
				    struct drm_file *file_priv,
				    uint32_t handle,
				    uint32_t width,
				    uint32_t height)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_gem_object *obj;
	struct amdgpu_bo *robj;

	struct amdgpu_device *adev = crtc->dev->dev_private;
	int ret;
	struct cursor_position position;

	if (!handle) {
		/* turn off cursor */
		position.enable = false;
		position.x = 0;
		position.y = 0;
		position.hot_spot_enable = false;
		dal_set_cursor_position(adev->dm.dal, amdgpu_crtc->crtc_id, &position);

		obj = NULL;
		goto unpin;
	}

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, amdgpu_crtc->crtc_id);
		return -ENOENT;
	}

	ret = dm_set_cursor(crtc, obj, width, height);

	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

unpin:
	if (amdgpu_crtc->cursor_bo) {
		robj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);
		ret = amdgpu_bo_reserve(robj, false);
		if (likely(ret == 0)) {
			amdgpu_bo_unpin(robj);
			amdgpu_bo_unreserve(robj);
		}
		drm_gem_object_unreference_unlocked(amdgpu_crtc->cursor_bo);
	}

	amdgpu_crtc->cursor_bo = obj;
	return 0;

}

static int dm_crtc_cursor_move(struct drm_crtc *crtc,
				     int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	int xorigin = 0, yorigin = 0;
	struct cursor_position position;

	/* avivo cursor are offset into the total surface */
	x += crtc->x;
	y += crtc->y;
	DRM_DEBUG("x %d y %d c->x %d c->y %d\n", x, y, crtc->x, crtc->y);

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}

	position.enable = true;
	position.x = x;
	position.y = y;

	position.hot_spot_enable = true;
	position.x_origin = xorigin;
	position.y_origin = yorigin;

	dal_set_cursor_position(adev->dm.dal, amdgpu_crtc->crtc_id, &position);

	return 0;
}

static int dm_crtc_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int ret = 0;

	if (amdgpu_crtc->cursor_bo) {
		ret = dm_set_cursor(crtc, amdgpu_crtc->cursor_bo,
			amdgpu_crtc->cursor_width, amdgpu_crtc->cursor_height);
	}

	return ret;
}

static void fill_plane_attributes(
			struct amdgpu_device* adev,
			struct plane_config *pl_config,
			struct drm_crtc *crtc) {

	uint64_t tiling_flags;
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	int r;
	struct amdgpu_framebuffer *amdgpu_fb;

	amdgpu_fb = to_amdgpu_framebuffer(crtc->primary->fb);
	obj = amdgpu_fb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0)){
		DRM_ERROR("Unable to reserve buffer\n");
		return;
	}

	amdgpu_bo_get_tiling_flags(rbo, &tiling_flags);
	amdgpu_bo_unreserve(rbo);

	switch (amdgpu_fb->base.pixel_format) {
	case DRM_FORMAT_C8:
		pl_config->config.format =
			SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		pl_config->config.format =
			SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		pl_config->config.format =
			SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n",
				amdgpu_fb->base.bits_per_pixel);
		return;
	}

	pl_config->config.tiling_info.value = 0;

       if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_2D_TILED_THIN1) {
               unsigned bankw, bankh, mtaspect, tile_split, num_banks;

               bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
               bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
               mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
               tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
               num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);


		/* XXX fix me for VI */
		pl_config->config.tiling_info.grph.NUM_BANKS = num_banks;
		pl_config->config.tiling_info.grph.ARRAY_MODE = ARRAY_2D_TILED_THIN1;
		pl_config->config.tiling_info.grph.TILE_SPLIT = tile_split;
		pl_config->config.tiling_info.grph.BANK_WIDTH = bankw;
		pl_config->config.tiling_info.grph.BANK_HEIGHT = bankh;
		pl_config->config.tiling_info.grph.TILE_ASPECT = mtaspect;
		pl_config->config.tiling_info.grph.TILE_MODE = ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_1D_TILED_THIN1) {
		pl_config->config.tiling_info.grph.ARRAY_MODE = ARRAY_1D_TILED_THIN1;
	}

	pl_config->config.tiling_info.grph.PIPE_CONFIG =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	pl_config->config.plane_size.grph.surface_size.x = 0;
	pl_config->config.plane_size.grph.surface_size.y = 0;
	pl_config->config.plane_size.grph.surface_size.width = amdgpu_fb->base.width;
	pl_config->config.plane_size.grph.surface_size.height = amdgpu_fb->base.height;
	pl_config->config.plane_size.grph.surface_pitch =
			amdgpu_fb->base.pitches[0] / (amdgpu_fb->base.bits_per_pixel / 8);

	/* TODO ACHTUNG ACHTUNG - NICHT SCHIESSEN
	 * Correctly program src, dst, and clip */
	pl_config->attributes.dst_rect.width  = crtc->mode.hdisplay;
	pl_config->attributes.dst_rect.height = crtc->mode.vdisplay;
	pl_config->attributes.dst_rect.x = 0;
	pl_config->attributes.dst_rect.y = 0;
	pl_config->attributes.clip_rect = pl_config->attributes.dst_rect;
	pl_config->attributes.src_rect = pl_config->attributes.dst_rect;

	pl_config->mp_scaling_data.viewport.x = crtc->x;
	pl_config->mp_scaling_data.viewport.y = crtc->y;
	pl_config->mp_scaling_data.viewport.width = crtc->mode.hdisplay;
	pl_config->mp_scaling_data.viewport.height = crtc->mode.vdisplay;

	pl_config->config.rotation = ROTATION_ANGLE_0;
	pl_config->config.layer_index = LAYER_INDEX_PRIMARY;
	pl_config->config.enabled = 1;
	pl_config->mask.bits.SURFACE_CONFIG_IS_VALID = 1;

}

/* this function will be called in the future from other files as well */
void amdgpu_dm_fill_surface_address(struct drm_crtc *crtc,
			struct plane_addr_flip_info *info,
			struct amdgpu_framebuffer *afb,
			struct drm_framebuffer *old_fb)
{
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	uint64_t fb_location;
	struct amdgpu_device *adev = crtc->dev->dev_private;
	int r;

	info->address_info.address.type = PLN_ADDR_TYPE_GRAPHICS;
	//DD-ToDo
	//	info->flip_immediate = amdgpu_pflip_vsync ? false : true;

	info->address_info.layer_index = LAYER_INDEX_PRIMARY;
	info->address_info.flags.bits.ENABLE = 1;

	/*Get fb location*/
	/* no fb bound */
	if (!crtc->primary->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return ;
	}

	DRM_DEBUG_KMS("Pin new framebuffer: %p\n", afb);
	obj = afb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return;

	r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, &fb_location);

	amdgpu_bo_unreserve(rbo);

	if (unlikely(r != 0)) {
		DRM_ERROR("Failed to pin framebuffer\n");
		return ;
	}

	info->address_info.address.grph.addr.low_part =
		lower_32_bits(fb_location);
	info->address_info.address.grph.addr.high_part =
		upper_32_bits(fb_location);

	dal_update_plane_addresses(adev->dm.dal, 1, info);

	/* unpin the old FB if surface change*/
	if (old_fb && old_fb != crtc->primary->fb)	{
		struct amdgpu_framebuffer *afb;
		/*struct amdgpu_bo *rbo;
		int r;*/

		afb = to_amdgpu_framebuffer(old_fb);
		DRM_DEBUG_KMS("Unpin old framebuffer: %p\n", afb);
		rbo = gem_to_amdgpu_bo(afb->obj);
		r = amdgpu_bo_reserve(rbo, false);
		if (unlikely(r)) {
			DRM_ERROR("failed to reserve rbo before unpin\n");
			return;
		} else {
			amdgpu_bo_unpin(rbo);
			amdgpu_bo_unreserve(rbo);
		}
	}
}


/**
 * drm_crtc_set_mode - set a mode
 * @crtc: CRTC to program
 * @mode: mode to use
 * @x: width of mode
 * @y: height of mode
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Try to set @mode on @crtc.  Give @crtc and its associated connectors a chance
 * to fixup or reject the mode prior to trying to set it.
 *
 * RETURNS:
 * True if the mode was set successfully, or false otherwise.
 */
bool amdgpu_dm_mode_set(
	struct drm_crtc *crtc,
	struct drm_display_mode *mode,
	int x,
	int y,
	struct drm_framebuffer *old_fb)
{
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_display_manager *dm =
		&((struct amdgpu_device *)crtc->dev->dev_private)->dm;
	struct drm_device *dev = crtc->dev;
	struct drm_display_mode *adjusted_mode, saved_mode, saved_hwmode;
	int saved_x, saved_y;
	bool ret = true;
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct plane_config pl_config = { { { 0 } } };
	struct plane_addr_flip_info addr_flip_info = { 0 };
	struct amdgpu_framebuffer *afb = NULL;
	int num_planes = 1;
	struct mode_query *mq;
	const struct path_mode_set *pms;

	DRM_DEBUG_KMS("amdgpu_dm_mode_set called\n");

	if (!adev->dm.dal)
		return false;

	adjusted_mode = drm_mode_duplicate(dev, mode);
	if (!adjusted_mode)
		return false;

	/* for now, no support for atomic mode_set, thus old_fb is not used */
	afb = to_amdgpu_framebuffer(crtc->primary->fb);

	saved_hwmode = crtc->hwmode;
	saved_mode = crtc->mode;
	saved_x = crtc->x;
	saved_y = crtc->y;

	/* Update crtc values up front so the driver can rely on them for mode
	* setting.
	*/
	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;

	DRM_DEBUG_KMS("[CRTC: %d, DISPLAY_IDX: %d]\n",
		      crtc->base.id, acrtc->crtc_id);

	/* Currently no support for atomic mode set */
	{
		struct render_mode rm;
		struct refresh_rate rf = { 0 };
		struct topology tp;
		init_dal_topology(dm, &tp, acrtc->crtc_id);
		rm.view.width = mode->hdisplay;
		rm.view.height = mode->vdisplay;
		rm.pixel_format =
			convert_to_dal_pixel_format(crtc->primary->fb->pixel_format);
		rf.field_rate = drm_mode_vrefresh(mode);
		rf.VIDEO_OPTIMIZED_RATE = (mode->private_flags &
			DAL_MODE_FLAG_VIDEO_OPTIMIZED) != 0;
		rf.INTERLACED = (mode->flags & DRM_MODE_FLAG_INTERLACE) != 0;

		mq = dal_get_mode_query(
			adev->dm.dal,
			&tp,
			dm->mode_query_option);
		if (!mq)
			return false;

		if (!dal_mode_query_select_render_mode(mq, &rm)) {
			dal_mode_query_destroy(&mq);
			DRM_ERROR("dal_mode_query_select_render_mode failed\n");
			return -1;
		}

		if (!dal_mode_query_select_refresh_rate(mq, &rf)) {
			dal_mode_query_destroy(&mq);
			DRM_ERROR("dal_mode_query_select_refresh_rate failed\n");
			return false;
		}
		pms = dal_mode_query_get_current_path_mode_set(mq);

		if (!pms) {
			dal_mode_query_destroy(&mq);
			DRM_ERROR("dal_mode_query_get_current_path_mode_set failed\n");
			return false;
		}
	}


	dal_set_blanking(adev->dm.dal, acrtc->crtc_id, true);
	/* the actual mode set call */
	ret = dal_set_path_mode(adev->dm.dal, pms);

	dal_mode_query_destroy(&mq);

	/* Surface programming */
	pl_config.display_index = acrtc->crtc_id;
	addr_flip_info.display_index = acrtc->crtc_id;

	fill_plane_attributes(adev, &pl_config, crtc);

	dal_setup_plane_configurations(adev->dm.dal, num_planes, &pl_config);

	/*programs the surface addr and flip control*/
	amdgpu_dm_fill_surface_address(crtc, &addr_flip_info, afb, old_fb);

	dal_set_blanking(adev->dm.dal, acrtc->crtc_id, false);
	/* Turn vblank on after reset */
	drm_crtc_vblank_on(crtc);

	if (ret) {
		/* Store real post-adjustment hardware mode. */
		crtc->hwmode = *adjusted_mode;
		crtc->enabled = true;

		/* Calculate and store various constants which
		 * are later needed by vblank and swap-completion
		 * timestamping. They are derived from true hwmode.
		 */
		drm_calc_timestamping_constants(crtc, &crtc->hwmode);
	}

	drm_mode_destroy(dev, adjusted_mode);
	if (!ret) {
		crtc->hwmode = saved_hwmode;
		crtc->mode = saved_mode;
		crtc->x = saved_x;
		crtc->y = saved_y;
	}

	return true;
}

bool amdgpu_dm_mode_reset(struct drm_crtc *crtc)
{
	bool ret;
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	uint32_t display_index = acrtc->crtc_id;

	/* Turn vblank off before reset */
	drm_crtc_vblank_off(crtc);

	/* When we will get called from drm asking to reset mode
	 * when fb is null, it will lead us reset mode unnecessarily.
	 * So change the sequence, we won't do the actual reset mode call
	 * when display is connected, as that's harmful for glitchless mode
	 * change (when we only reprogram pipe front end). */
	if ((dal_get_connected_targets_vector(adev->dm.dal)
			& (1 << display_index)) &&
		adev->dm.fake_display_index == INVALID_DISPLAY_INDEX) {

		/*
		 * Blank the display, as buffer will be invalidated.
		 * For the else case it would be done as part of dal reset mode
		 * sequence.
		 */
		mutex_lock(&adev->dm.dal_mutex);
		dal_set_blanking(adev->dm.dal, acrtc->crtc_id, true);
		mutex_unlock(&adev->dm.dal_mutex);

		ret = true;
		DRM_DEBUG_KMS(
			"Skip reset mode for disp_index %d\n",
			display_index);
	} else {
		mutex_lock(&adev->dm.dal_mutex);
		ret = dal_reset_path_mode(adev->dm.dal, 1, &display_index);
		mutex_unlock(&adev->dm.dal_mutex);
		DRM_DEBUG_KMS(
			"Do reset mode for disp_index %d\n",
			display_index);
	}

	/* unpin the FB */
	if (crtc->primary->fb)  {
		struct amdgpu_framebuffer *afb;
		struct amdgpu_bo *rbo;
		int r;

		afb = to_amdgpu_framebuffer(crtc->primary->fb);
		DRM_DEBUG_KMS("Unpin old framebuffer: %p\n", afb);
		rbo = gem_to_amdgpu_bo(afb->obj);
		r = amdgpu_bo_reserve(rbo, false);
		if (unlikely(r))
			DRM_ERROR("failed to reserve rbo before unpin\n");
		else {
			amdgpu_bo_unpin(rbo);
			amdgpu_bo_unreserve(rbo);
		}
	}

	if (ret)
		crtc->enabled = false;

	return ret;
}

/**
 * amdgpu_dm_set_config - set a new config from userspace
 * @crtc: CRTC to setup
 * @crtc_info: user provided configuration
 * @new_mode: new mode to set
 * @connector_set: set of connectors for the new config
 * @fb: new framebuffer
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Setup a new configuration, provided by the user in @crtc_info, and enable
 * it.
 *
 * RETURNS:
 * Zero on success
 */
int amdgpu_dm_set_config(struct drm_mode_set *set)
{
	/* TODO:
	 * Save + restore mode + fb info
	 * Call dm_set_mode to do the folloring:
	 * 	Fill Modes
	 * 	Set Mode
	 * 	SetPlaneConfig
	 * 	UpdatePlaneAddress
	 * 	FillPlaneAttributes
	 */

	struct drm_device *dev;
	struct amdgpu_device *adev;
	struct drm_crtc *save_crtcs, *crtc;
	struct drm_encoder *save_encoders, *encoder, *new_encoder;
	bool mode_changed = false; /* if true do a full mode set */
	bool fb_changed = false; /* if true and !mode_changed just do a flip */
				 /* if true and mode_changed do reset_mode */
	struct drm_connector *save_connectors, *connector;
	const struct drm_connector_helper_funcs *connector_funcs;
	struct amdgpu_crtc *acrtc = NULL;
	int count = 0, fail = 0;
	struct drm_mode_set save_set;
	int ret = 0;
	int i;

	DRM_DEBUG_KMS("\n");
	DRM_DEBUG_KMS("--- DM set_config called ---\n");

	BUG_ON(!set);
	BUG_ON(!set->crtc);
	BUG_ON(!set->crtc->helper_private);

	/* Enforce sane interface api - has been abused by the fb helper. */
	BUG_ON(!set->mode && set->fb);
	BUG_ON(set->fb && set->num_connectors == 0);

	if (set->num_connectors > 1) {
		DRM_ERROR("Trying to set %zu connectors, but code only assumes max of one\n",
				set->num_connectors);
		return -EINVAL;
	}

	dev = set->crtc->dev;
	adev = dev->dev_private;

	if (!set->mode)
		set->fb = NULL;

	/* Allocate space for the backup of all (non-pointer) crtc, encoder and
	 * connector data. */
	save_crtcs = kzalloc(dev->mode_config.num_crtc *
			     sizeof(struct drm_crtc), GFP_KERNEL);
	if (!save_crtcs)
		return -ENOMEM;

	save_encoders = kzalloc(dev->mode_config.num_encoder *
				sizeof(struct drm_encoder), GFP_KERNEL);
	if (!save_encoders) {
		kfree(save_crtcs);
		return -ENOMEM;
	}

	save_connectors = kzalloc(dev->mode_config.num_connector *
				sizeof(struct drm_connector), GFP_KERNEL);
	if (!save_connectors) {
		kfree(save_encoders);
		kfree(save_crtcs);
		return -ENOMEM;
	}

	/* Copy data. Note that driver private data is not affected.
	 * Should anything bad happen only the expected state is
	 * restored, not the drivers personal bookkeeping.
	 */
	count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		save_crtcs[count++] = *crtc;
	}

	count = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		save_encoders[count++] = *encoder;
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		save_connectors[count++] = *connector;
	}


	if (set->fb) {
		DRM_DEBUG_KMS(
			"[CRTC:%d] [FB:%d] #connectors=%d (x y) (%i %i)\n",
				set->crtc->base.id, set->fb->base.id,
				(int)set->num_connectors, set->x, set->y);
	} else {
		/*TODO: Move mode reset to crtc->disable instead, and
		call drm_helper_disable_unused_functions here?*/

		DRM_DEBUG_KMS("[CRTC:%d] [NOFB]\n", set->crtc->base.id);

		DRM_DEBUG_KMS("Setting connector DPMS state to off\n");
		for (i = 0; i < set->num_connectors; i++) {
			DRM_DEBUG_KMS(
				"\t[CONNECTOR:%d] set DPMS off\n",
				set->connectors[i]->base.id);

			set->connectors[i]->funcs->dpms(
				set->connectors[i], DRM_MODE_DPMS_OFF);

		}

		if (!amdgpu_dm_mode_reset(set->crtc)) {
			DRM_ERROR("### Failed to reset mode on [CRTC:%d] ###\n",
					set->crtc->base.id);
			ret = -EINVAL;
			goto fail;
		}
		DRM_DEBUG_KMS("=== Early exit dm_set_config ===\n");
		return 0;
	}

	save_set.crtc = set->crtc;
	save_set.mode = &set->crtc->mode;
	save_set.x = set->crtc->x;
	save_set.y = set->crtc->y;
	save_set.fb = set->crtc->primary->fb;

	/* We should be able to check here if the fb has the same properties
	 * and then just flip_or_move it */
	if (set->crtc->primary->fb != set->fb) {
		DRM_DEBUG_KMS("Old FB: %p, New FB[%d]: %p",
				set->crtc->primary->fb,
				set->fb->base.id,
				set->fb);
		/* If we have no fb then treat it as a full mode set */
		if (set->crtc->primary->fb == NULL) {
			DRM_DEBUG_KMS("crtc has no fb, full mode set\n");
			mode_changed = true;
		} else if (set->fb == NULL) {
			mode_changed = true;
		} else if (set->fb->depth != set->crtc->primary->fb->depth) {
			mode_changed = true;
		} else if (set->fb->bits_per_pixel !=
			   set->crtc->primary->fb->bits_per_pixel) {
			mode_changed = true;
		} else {
			fb_changed = true;
			DRM_DEBUG_KMS("fbs do not match, set fb_changed to true\n");
		}
	} else {
		DRM_DEBUG_KMS("FB hasn't changed since last set\n");
	}

	if (set->x != set->crtc->x || set->y != set->crtc->y) {
		fb_changed = true;
		DRM_DEBUG_KMS("Viewport Changed. Original: (%d,%d), New: (%d,%d)\n",
						set->x,
						set->y,
						set->crtc->x,
						set->crtc->y);
	}

	if (set->mode && !drm_mode_equal(set->mode, &set->crtc->mode)) {
		DRM_DEBUG_KMS("modes are different, set mode_changed=true\n");
		drm_mode_debug_printmodeline(&set->crtc->mode);
		drm_mode_debug_printmodeline(set->mode);
		mode_changed = true;
	}

	/* traverse and find the appropriate connector,
	use its encoder and crtc */
	count = 0;
	fail = 1;
	acrtc = to_amdgpu_crtc(set->crtc);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		/* matching the display index */
		if (to_amdgpu_connector(connector)->connector_id ==
			acrtc->crtc_id) {
			fail = 0;
			break;
		}
	}

	if (fail) {
		ret = -EINVAL;
		DRM_ERROR("Couldn't find a matching connector\n");
		goto fail;
	}

	/* Get best encoder for connector found above
	 * TODO: Might need to traverse entire connector list at some point
	 */
	connector_funcs = connector->helper_private;
	new_encoder = connector->encoder;

	/* NOTE: We're assuming a max of one connector per set, so no need to
	 * loop through connectors in set to find the correct one */
	if (set->connectors[0] == connector) {
		new_encoder = connector_funcs->best_encoder(connector);
		/* if we can't get an encoder for a connector
		   we are setting now - then fail */
		if (new_encoder == NULL)
			/* don't break so fail path works correct */
			fail = 1;
		if (connector->dpms != DRM_MODE_DPMS_ON) {
			DRM_DEBUG_KMS("connector dpms not on, full mode switch\n");
			mode_changed = true;
		}
	}

	if (new_encoder != connector->encoder) {
		DRM_DEBUG_KMS("encoder changed, full mode switch\n");
		mode_changed = true;
		/* If the encoder is reused for another connector, then
		 * the appropriate crtc will be set later.
		 */
		if (connector->encoder)
			connector->encoder->crtc = NULL;
		connector->encoder = new_encoder;
	}


	if (fail) {
		ret = -EINVAL;
		DRM_ERROR("Couldn't find an encoder\n");
		goto fail;
	}

	if (connector->encoder->crtc != set->crtc) {
		mode_changed = true;
		connector->encoder->crtc = set->crtc;

		DRM_DEBUG_KMS("New CRTC being used. Full mode set: [CONNECTOR:%d] to [CRTC:%d]\n",
			connector->base.id,
			set->crtc->base.id);
	} else {
		DRM_DEBUG_KMS("Crtc didn't change: [CONNECTOR:%d] on [CRTC:%d]\n",
			connector->base.id,
			set->crtc->base.id);
	}

	if (mode_changed) {
		struct amdgpu_device *adev;

		DRM_DEBUG_KMS("Attempting to set mode from userspace. Mode:\n");

		drm_mode_debug_printmodeline(set->mode);

		set->crtc->primary->fb = set->fb;

		adev = set->crtc->dev->dev_private;

		mutex_lock(&adev->dm.dal_mutex);
		if (!amdgpu_dm_mode_set(
				set->crtc,
				set->mode,
				set->x,
				set->y,
				save_set.fb)) {
			DRM_ERROR(
					"failed to set mode on [CRTC:%d, DISPLAY_IDX: %d]\n",
					set->crtc->base.id,
					acrtc->crtc_id);
			set->crtc->primary->fb = save_set.fb;
			ret = -EINVAL;
			mutex_unlock(&adev->dm.dal_mutex);
			goto fail;
		}

		mutex_unlock(&adev->dm.dal_mutex);

		DRM_DEBUG_KMS("Setting connector DPMS state to on\n");
		for (i = 0; i < set->num_connectors; i++) {
			DRM_DEBUG_KMS(
					"\t[CONNECTOR:%d] set DPMS on\n",
					set->connectors[i]->base.id);

			 set->connectors[i]->funcs->dpms(
					set->connectors[i], DRM_MODE_DPMS_ON);

		}

		/* Re-set the cursor attributes after a successful set mode */
		dm_crtc_cursor_reset(set->crtc);

	} else if (fb_changed) { /* no mode change just surface change. */
		struct plane_addr_flip_info addr_flip_info = { 0 };
		struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(set->fb);
		struct amdgpu_device *adev = dev->dev_private;
		struct plane_config pl_config = { { { 0 } } };

		DRM_DEBUG_KMS("FB Changed, update address\n");
		set->crtc->primary->fb = set->fb;
		set->crtc->x = set->x;
		set->crtc->y = set->y;

		/* program plane config */
		pl_config.display_index = acrtc->crtc_id;

		/* Blank display before programming surface */
		dal_set_blanking(adev->dm.dal, acrtc->crtc_id, true);
		fill_plane_attributes(adev, &pl_config, set->crtc);
		dal_setup_plane_configurations(adev->dm.dal, 1, &pl_config);
		/* Program the surface addr and flip control */
		addr_flip_info.display_index = acrtc->crtc_id;
		amdgpu_dm_fill_surface_address(set->crtc, &addr_flip_info,
					       afb, save_set.fb);
		dal_set_blanking(adev->dm.dal, acrtc->crtc_id, false);
	}
	DRM_DEBUG_KMS("=== Finished dm_set_config ===\n");

	kfree(save_connectors);
	kfree(save_encoders);
	kfree(save_crtcs);

	/* adjust pm to dpms */
	amdgpu_pm_compute_clocks(adev);

	return 0;

fail:
	/* Restore all previous data. */
	DRM_ERROR("### Failed set_config. Attempting to restore previous data ###\n");
	count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		*crtc = save_crtcs[count++];
	}

	count = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		*encoder = save_encoders[count++];
	}

	count = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		*connector = save_connectors[count++];
	}

	/* Try to restore the config */
	if (mode_changed && save_set.crtc->primary->fb &&
		!amdgpu_dm_mode_set(
			save_set.crtc,
			save_set.mode,
			save_set.x,
			save_set.y,
			save_set.fb))
		DRM_ERROR("failed to restore config after modeset failure\n");

	kfree(save_connectors);
	kfree(save_encoders);
	kfree(save_crtcs);

	DRM_DEBUG_KMS("=== Finished dm_set_config ===\n");
	return ret;
}

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *dm_crtc = to_amdgpu_crtc(crtc);

	drm_crtc_cleanup(crtc);
	destroy_workqueue(dm_crtc->pflip_queue);
	kfree(crtc);
}

static void amdgpu_dm_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t start, uint32_t size)
{
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	uint32_t display_index = acrtc->crtc_id;
	int end = (start + size > 256) ? 256 : start + size;
	int i;
	struct raw_gamma_ramp *gamma;

	gamma = kzalloc(sizeof(struct raw_gamma_ramp), GFP_KERNEL);

	for (i = start; i < end; i++) {
		gamma->rgb_256[i].red = red[i];
		gamma->rgb_256[i].green = green[i];
		gamma->rgb_256[i].blue = blue[i];
	}

	gamma->size = sizeof(gamma->rgb_256);
	gamma->type = GAMMA_RAMP_TYPE_RGB256;

	dal_set_gamma(adev->dm.dal, display_index, gamma);
	kfree(gamma);
}

/* Implemented only the options currently availible for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
/*	.save = NULL,
	.restore = NULL,
	.reset = NULL,*/
	.cursor_set = dm_crtc_cursor_set,
	.cursor_move = dm_crtc_cursor_move,
	.destroy = amdgpu_dm_crtc_destroy,
	.gamma_set = amdgpu_dm_crtc_gamma_set,
	.set_config = amdgpu_dm_set_config,
	.page_flip = amdgpu_crtc_page_flip /* this function is common for
				all implementations of DCE code (original and
				DAL) */

	/*.set_property = NULL*/
};


/*
 * dm_add_display_info
 *
 * @brief:
 * Update required display info from mode
 *
 * @param
 * disp_info: [out] display information
 * mode: [in] mode containing display information
 *
 * @return
 * void
 */
void dm_add_display_info(
		struct drm_display_info *disp_info,
		struct amdgpu_display_manager *dm,
		uint32_t display_index)
{
}

static inline bool compare_mode_query_info_and_mode_timing(
	const struct render_mode *rm,
	const struct refresh_rate *rr,
	const struct mode_info *mi)
{
	if (rm->view.height == mi->pixel_height &&
		rm->view.width == mi->pixel_width &&
		rr->field_rate == mi->field_rate &&
		rr->INTERLACED == mi->flags.INTERLACE &&
		rr->VIDEO_OPTIMIZED_RATE == mi->flags.VIDEO_OPTIMIZED_RATE)
		return true;
	else
		return false;
}

static inline void fill_drm_mode_info(
	struct drm_display_mode *drm_mode,
	const struct mode_timing *mode_timing,
	const struct render_mode *rm,
	const struct refresh_rate *rr)
{
	drm_mode->hsync_start = mode_timing->mode_info.pixel_width +
		mode_timing->crtc_timing.h_front_porch;
	drm_mode->hsync_end = mode_timing->mode_info.pixel_width +
		mode_timing->crtc_timing.h_front_porch +
		mode_timing->crtc_timing.h_sync_width;
	drm_mode->htotal = mode_timing->crtc_timing.h_total;
	drm_mode->hdisplay = rm->view.width;
	drm_mode->vsync_start = mode_timing->mode_info.pixel_height +
		mode_timing->crtc_timing.v_front_porch;
	drm_mode->vsync_end = mode_timing->mode_info.pixel_height +
		mode_timing->crtc_timing.v_front_porch +
		mode_timing->crtc_timing.v_sync_width;
	drm_mode->vtotal = mode_timing->crtc_timing.v_total;
	drm_mode->vdisplay = rm->view.height;

	drm_mode->clock = mode_timing->crtc_timing.pix_clk_khz;
	drm_mode->vrefresh = rr->field_rate;
	if (mode_timing->crtc_timing.flags.HSYNC_POSITIVE_POLARITY)
		drm_mode->flags |= DRM_MODE_FLAG_PHSYNC;
	if (mode_timing->crtc_timing.flags.VSYNC_POSITIVE_POLARITY)
		drm_mode->flags |= DRM_MODE_FLAG_PVSYNC;
	if (mode_timing->crtc_timing.flags.INTERLACE)
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	if (mode_timing->mode_info.flags.PREFERRED)
		drm_mode->type |= DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(drm_mode);
}

/**
 * amdgpu_display_manager_add_mode -
 *      add mode for the connector
 * @connector: drm connector
 * @mode: the mode
 *
 * Add the specified modes to the connector's mode list.
 *
 * Return 0 on success.
 */
static int dm_add_mode(
	struct drm_connector *connector,
	const struct mode_timing *mt,
	const struct render_mode *rm,
	const struct refresh_rate *rr)
{
	struct drm_display_mode *drm_mode;

	if (!mt)
		return -1;

	drm_mode = drm_mode_create(connector->dev);

	if (!drm_mode)
		return -1;

	fill_drm_mode_info(drm_mode, mt, rm, rr);

	list_add(&drm_mode->head, &connector->modes);

	return 0;
}

static void add_to_mq_helper(void *what, const struct path_mode *pm)
{
	dal_mode_query_pin_path_mode(what, pm);
}

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	struct amdgpu_connector *aconnector =
		to_amdgpu_connector(connector);
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	bool connected;
	uint32_t display_index = aconnector->connector_id;

	if (!adev->dm.dal)
		return 0;

	connected = (dal_get_connected_targets_vector(adev->dm.dal)
			& (1 << display_index));

	return (connected ? connector_status_connected :
			connector_status_disconnected);
}
static void amdgpu_dm_crtc_disable(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("NOT IMPLEMENTED\n");
}

static void amdgpu_dm_encoder_disable(struct drm_encoder *encoder)
{
	DRM_DEBUG_KMS("NOT IMPLEMENTED\n");
}

/**
 * amdgpu_display_manager_fill_modes - get complete set of
 * display timing modes per drm_connector
 *
 * @connector: DRM device connector
 * @maxX: max width for modes
 * @maxY: max height for modes
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Query connector and try to detect modes on it. Received
 * modes are assumed to be filtered and validated and supported
 * by the connector assuming set_mode for that connector will
 * come immediately after this function call.
 *
 * Therefore all these modes will be put into the normal modes
 * list.
 *
 * Intended to be used either at bootup time or when major configuration
 * changes have occurred.
 *
 *
 * RETURNS:
 * Number of modes found on @connector.
 */
int amdgpu_display_manager_fill_modes(struct drm_connector *connector,
				      uint32_t maxX, uint32_t maxY)
{
	struct amdgpu_connector *aconnector =
		to_amdgpu_connector(connector);
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_display_mode *mode, *t;
	unsigned int non_filtered_modes_num = 0;
	struct mode_query *mq;
	struct topology tp;

	if (!adev->dm.dal)
		return 0;

	DRM_DEBUG_KMS("[CONNECTOR:%d, DISPLAY_IDX: %d]\n",
		connector->connector_type_id, aconnector->connector_id);

	/* clean all the previous modes on this connector */
	list_for_each_entry_safe(mode, t, &connector->modes, head) {
		list_del(&mode->head);
		drm_mode_debug_printmodeline(mode);
		DRM_DEBUG_KMS("Not using %s mode %d\n",
				mode->name, mode->status);
		drm_mode_destroy(dev, mode);
	}


	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG_KMS("[CONNECTOR:%d] disconnected\n",
			connector->connector_type_id);
		drm_mode_connector_update_edid_property(connector, NULL);
		goto prune;
	}

	/* get the mode list from DAL, iterate over it and add
	the modes to drm connector mode list */

	/* the critical assumtion here is that the returned list is
	clean: no duplicates, all the modes are valid, ordered, and
	can be actually set on the hardware */

	init_dal_topology(&adev->dm, &tp, aconnector->connector_id);
	mq = dal_get_mode_query(adev->dm.dal, &tp, adev->dm.mode_query_option);

	if (!mq)
		goto prune;

	dal_pin_active_path_modes(
		adev->dm.dal,
		mq,
		aconnector->connector_id,
		add_to_mq_helper);

	if (!dal_mode_query_select_first(mq))
		goto prune;

	do {
		const struct render_mode *rm =
			dal_mode_query_get_current_render_mode(mq);

		if (rm->pixel_format != PIXEL_FORMAT_ARGB8888)
			continue;

		do {
			const struct refresh_rate *rr =
				dal_mode_query_get_current_refresh_rate(mq);
			const struct path_mode_set *pms =
				dal_mode_query_get_current_path_mode_set(mq);
			const struct path_mode *pm =
				dal_pms_get_path_mode_for_display_index(
					pms,
					aconnector->connector_id);

			const struct mode_timing *mt = pm->mode_timing;

			if (mt->mode_info.pixel_height > maxY ||
				mt->mode_info.pixel_width > maxX ||
				mt->mode_info.flags.INTERLACE)
				continue;

			if (dm_add_mode(connector, mt, rm, rr) == 0)
				++non_filtered_modes_num;

			if (adev->dm.fake_display_index ==
				aconnector->connector_id)
				break;

		} while (dal_mode_query_select_next_refresh_rate(mq));

		if (adev->dm.fake_display_index == aconnector->connector_id)
			break;

	} while (dal_mode_query_select_next_render_mode(mq));

	dal_mode_query_destroy(&mq);

prune:
	DRM_DEBUG_KMS("[CONNECTOR:%d] probed modes :\n",
		      connector->connector_type_id);

	list_for_each_entry(mode, &connector->modes, head) {
		drm_mode_set_crtcinfo(mode, 0);
		drm_mode_debug_printmodeline(mode);
	}

	return non_filtered_modes_num;
}

static int amdgpu_dm_connector_set_property(struct drm_connector *connector,
					  struct drm_property *property,
					  uint64_t val)
{
	DRM_ERROR("NOT IMPLEMENTED\n");
	return 0;
}

void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	/*drm_sysfs_connector_remove(connector);*/
	drm_connector_cleanup(connector);
	kfree(connector);
}

static void amdgpu_dm_connector_force(struct drm_connector *connector)
{
	DRM_ERROR("NOT IMPLEMENTED\n");
}

static inline enum dal_power_state to_dal_power_state(int mode)
{
	switch (mode) {
	case DRM_MODE_DPMS_ON:
		return DAL_POWER_STATE_ON;
	case DRM_MODE_DPMS_OFF:
		return DAL_POWER_STATE_OFF;
	case DRM_MODE_DPMS_STANDBY:
		return DAL_POWER_STATE_STANDBY;
	case DRM_MODE_DPMS_SUSPEND:
		return DAL_POWER_STATE_SUSPEND;
	default:
		/*
		 * if unknown dpms mode passed for any reason display would be
		 * disabled, with log notification
		 */
		DRM_ERROR("Invalid DPMS mode requested\n");
		return DAL_POWER_STATE_OFF;
	}
}


static void amdgpu_dm_connector_dpms(struct drm_connector *connector, int mode)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	uint32_t display_index = aconnector->connector_id;
	enum dal_power_state ps = to_dal_power_state(mode);

	if (mode == connector->dpms)
		return;

	dal_set_display_dpms(adev->dm.dal, display_index, ps);

	connector->dpms = mode;

	/* adjust pm to dpms */
	amdgpu_pm_compute_clocks(adev);
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.dpms = amdgpu_dm_connector_dpms,
/*	.save = NULL,
	.restore = NULL,
	.reset = NULL,*/
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = amdgpu_display_manager_fill_modes,
	.set_property = amdgpu_dm_connector_set_property,
	.destroy = amdgpu_dm_connector_destroy,
	.force = amdgpu_dm_connector_force
};

static void dm_crtc_helper_dpms(struct drm_crtc *crtc, int mode)
{

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		drm_crtc_vblank_on(crtc);
		break;
	case DRM_MODE_DPMS_OFF:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	default:
		drm_crtc_vblank_off(crtc);
		break;
	}

}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = amdgpu_dm_crtc_disable,
	.dpms = dm_crtc_helper_dpms,
	.load_lut = NULL
};

static const struct drm_encoder_helper_funcs dm_encoder_helper_funcs = {
	.disable = amdgpu_dm_encoder_disable,
};


int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct amdgpu_crtc *acrtc,
			int display_idx)
{
	int res = drm_crtc_init(
			dm->ddev,
			&acrtc->base,
			&amdgpu_dm_crtc_funcs);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	acrtc->max_cursor_width = 128;
	acrtc->max_cursor_height = 128;

	acrtc->crtc_id = display_idx;
	acrtc->base.enabled = false;

	dm->adev->mode_info.crtcs[display_idx] = acrtc;
	drm_mode_crtc_set_gamma_size(&acrtc->base, 256);

	acrtc->pflip_queue =
		create_singlethread_workqueue("amdgpu-pageflip-queue");

	return 0;
fail:
	acrtc->crtc_id = -1;
	return res;
}

static struct drm_encoder *best_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("Finding the best encoder\n");

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj) {
			DRM_ERROR("Couldn't find a matching encoder for our connector\n");
			return NULL;
		}
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	DRM_ERROR("No encoder id\n");
	return NULL;
}


static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	.best_encoder = best_encoder
};

static int to_drm_connector_type(enum signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
		return DRM_MODE_CONNECTOR_DVID;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

int amdgpu_dm_connector_init(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	int display_idx,
	bool is_connected,
	struct amdgpu_encoder *aencoder)
{
	int res, connector_type;
	enum signal_type st = SIGNAL_TYPE_HDMI_TYPE_A;

	DRM_DEBUG_KMS("amdgpu_dm_connector_init\n");

	if (dm->dal != NULL)
		st = dal_get_display_signal(dm->dal, display_idx);
	connector_type = to_drm_connector_type(st);

	res = drm_connector_init(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type);

	if (res) {
		DRM_ERROR("connector_init failed\n");
		aconnector->connector_id = -1;
		return res;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	aconnector->connector_id = display_idx;
	aconnector->base.interlace_allowed = true;
	aconnector->base.doublescan_allowed = true;
	aconnector->hpd.hpd = display_idx; /* maps to 'enum amdgpu_hpd_id' */

	if (is_connected)
		aconnector->base.status = connector_status_connected;
	else
		aconnector->base.status = connector_status_disconnected;

	/*configure suport HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported*/
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	/* TODO: Don't do this manually anymore
	aconnector->base.encoder = &aencoder->base;
	*/

	drm_mode_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	/*drm_sysfs_connector_add(&dm_connector->base);*/

	/* TODO: this switch should be updated during hotplug/unplug*/
	if (dm->dal != NULL && is_connected) {
		DRM_DEBUG_KMS("Connector is connected\n");
		drm_mode_connector_update_edid_property(
			&aconnector->base,
			(struct edid *)
			dal_get_display_edid(dm->dal, display_idx, NULL));
	}

	drm_connector_register(&aconnector->base);

	return 0;
}

int amdgpu_dm_encoder_init(
	struct drm_device *dev,
	struct amdgpu_encoder *aencoder,
	int display_idx,
	struct amdgpu_crtc *acrtc)
{
	int res = drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS);

	aencoder->base.possible_crtcs = 1 << display_idx;
	aencoder->base.crtc = &acrtc->base;

	if (!res)
		aencoder->encoder_id = display_idx;

	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &dm_encoder_helper_funcs);

	return res;
}
