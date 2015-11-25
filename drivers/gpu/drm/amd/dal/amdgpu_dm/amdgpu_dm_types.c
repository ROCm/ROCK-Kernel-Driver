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
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include "amdgpu.h"
#include "amdgpu_pm.h"
// We need to #undef FRAME_SIZE and DEPRECATED because they conflict
// with ptrace-abi.h's #define's of them.
#undef FRAME_SIZE
#undef DEPRECATED

#include "mode_query_interface.h"
#include "dcs_types.h"
#include "mode_manager_types.h"

/*#include "amdgpu_buffer.h"*/

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#include "dc.h"

#include "amdgpu_dm_types.h"
#include "amdgpu_dm_mst_types.h"

struct dm_connector_state {
	struct drm_connector_state base;

	enum amdgpu_rmx_type scaling;
	uint8_t underscan_vborder;
	uint8_t underscan_hborder;
	bool underscan_enable;
};

#define to_dm_connector_state(x)\
	container_of((x), struct dm_connector_state, base)

#define AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET 1

void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static void dm_set_cursor(
	struct amdgpu_crtc *amdgpu_crtc,
	uint64_t gpu_addr,
	uint32_t width,
	uint32_t height)
{
	struct dc_cursor_attributes attributes;
	amdgpu_crtc->cursor_width = width;
	amdgpu_crtc->cursor_height = height;

	attributes.address.high_part = upper_32_bits(gpu_addr);
	attributes.address.low_part  = lower_32_bits(gpu_addr);
	attributes.width             = width-1;
	attributes.height            = height-1;
	attributes.x_hot             = 0;
	attributes.y_hot             = 0;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	if (!dc_target_set_cursor_attributes(
				amdgpu_crtc->target,
				&attributes)) {
		DRM_ERROR("DC failed to set cursor attributes\n");
	}
}

static int dm_crtc_unpin_cursor_bo_old(
	struct amdgpu_crtc *amdgpu_crtc)
{
	struct amdgpu_bo *robj;
	int ret = 0;

	if (NULL != amdgpu_crtc && NULL != amdgpu_crtc->cursor_bo) {
		robj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);

		ret  = amdgpu_bo_reserve(robj, false);

		if (likely(ret == 0)) {
			amdgpu_bo_unpin(robj);
			amdgpu_bo_unreserve(robj);
		}
	} else {
		DRM_ERROR("dm_crtc_unpin_cursor_ob_old bo %x, leaked %p\n",
				ret,
				amdgpu_crtc->cursor_bo);
	}

	drm_gem_object_unreference_unlocked(amdgpu_crtc->cursor_bo);
	amdgpu_crtc->cursor_bo = NULL;

	return ret;
}

static int dm_crtc_pin_cursor_bo_new(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	struct amdgpu_bo **ret_obj,
	uint64_t *gpu_addr)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_bo *robj;
	struct drm_gem_object *obj;
	int ret = EINVAL;

	if (NULL != crtc) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);

		if (!obj) {
			DRM_ERROR(
				"Cannot find cursor object %x for crtc %d\n",
				handle,
				amdgpu_crtc->crtc_id);
			goto release;
		}
		robj = gem_to_amdgpu_bo(obj);

		ret  = amdgpu_bo_reserve(robj, false);

		if (unlikely(ret != 0)) {
			drm_gem_object_unreference_unlocked(obj);
		DRM_ERROR("dm_crtc_pin_cursor_bo_new ret %x, handle %x\n",
				 ret, handle);
			goto release;
		}

		ret = amdgpu_bo_pin(robj, AMDGPU_GEM_DOMAIN_VRAM, NULL);

		if (ret == 0) {
			*gpu_addr = amdgpu_bo_gpu_offset(robj);
			*ret_obj  = robj;
		}
		amdgpu_bo_unreserve(robj);
		if (ret)
			drm_gem_object_unreference_unlocked(obj);

	}
release:

	return ret;
}

static int dm_crtc_cursor_set(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	uint32_t width,
	uint32_t height)
{
	struct amdgpu_bo *new_cursor_bo;
	uint64_t gpu_addr;
	struct dc_cursor_position position;

	int ret;

	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	ret		= EINVAL;
	new_cursor_bo	= NULL;
	gpu_addr	= 0;

	DRM_DEBUG_KMS(
	"%s: crtc_id=%d with handle %d and size %d to %d, bo_object %p\n",
		__func__,
		amdgpu_crtc->crtc_id,
		handle,
		width,
		height,
		amdgpu_crtc->cursor_bo);

	if (!handle) {
		/* turn off cursor */
		position.enable = false;
		position.x = 0;
		position.y = 0;
		position.hot_spot_enable = false;

		if (amdgpu_crtc->target) {
			/*set cursor visible false*/
			dc_target_set_cursor_position(
				amdgpu_crtc->target,
				&position);
		}
		/*unpin old cursor buffer and update cache*/
		ret = dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);
		goto release;

	}

	if ((width > amdgpu_crtc->max_cursor_width) ||
		(height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR(
			"%s: bad cursor width or height %d x %d\n",
			__func__,
			width,
			height);
		goto release;
	}
	/*try to pin new cursor bo*/
	ret = dm_crtc_pin_cursor_bo_new(crtc, file_priv, handle,
			&new_cursor_bo, &gpu_addr);
	/*if map not successful then return an error*/
	if (ret)
		goto release;

	/*program new cursor bo to hardware*/
	dm_set_cursor(amdgpu_crtc, gpu_addr, width, height);

	/*un map old, not used anymore cursor bo ,
	 * return memory and mapping back */
	dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);

	/*assign new cursor bo to our internal cache*/
	amdgpu_crtc->cursor_bo = &new_cursor_bo->gem_base;

release:
	return ret;

}

static int dm_crtc_cursor_move(struct drm_crtc *crtc,
				     int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int xorigin = 0, yorigin = 0;
	struct dc_cursor_position position;

	/* avivo cursor are offset into the total surface */
	x += crtc->primary->state->src_x >> 16;
	y += crtc->primary->state->src_y >> 16;

	/*
	 * TODO: for cursor debugging unguard the following
	 */
#if 0
	DRM_DEBUG_KMS(
		"%s: x %d y %d c->x %d c->y %d\n",
		__func__,
		x,
		y,
		crtc->x,
		crtc->y);
#endif

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

	if (!dc_target_set_cursor_position(
				amdgpu_crtc->target,
				&position)) {
		DRM_ERROR("DC failed to set cursor position\n");
		return -EINVAL;
	}

#if BUILD_FEATURE_TIMING_SYNC
	{
		struct drm_device *dev = crtc->dev;
		struct amdgpu_device *adev = dev->dev_private;
		struct amdgpu_display_manager *dm = &adev->dm;

		dc_print_sync_report(dm->dc);
	}
#endif
	return 0;
}

static void dm_crtc_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	DRM_DEBUG_KMS(
		"%s: with cursor_bo %p\n",
		__func__,
		amdgpu_crtc->cursor_bo);

	if (amdgpu_crtc->cursor_bo && amdgpu_crtc->target) {
		dm_set_cursor(
		amdgpu_crtc,
		amdgpu_bo_gpu_offset(gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo)),
		amdgpu_crtc->cursor_width,
		amdgpu_crtc->cursor_height);
	}
}
static bool fill_rects_from_plane_state(
	const struct drm_plane_state *state,
	struct dc_surface *surface)
{
	surface->src_rect.x = state->src_x >> 16;
	surface->src_rect.y = state->src_y >> 16;
	/*we ignore for now mantissa and do not to deal with floating pixels :(*/
	surface->src_rect.width = state->src_w >> 16;

	if (surface->src_rect.width == 0)
		return false;

	surface->src_rect.height = state->src_h >> 16;
	if (surface->src_rect.height == 0)
		return false;

	surface->dst_rect.x = state->crtc_x;
	surface->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return false;

	surface->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return false;

	surface->dst_rect.height = state->crtc_h;

	surface->clip_rect = surface->dst_rect;

	switch (state->rotation) {
	case DRM_ROTATE_0:
		surface->rotation = ROTATION_ANGLE_0;
		break;
	case DRM_ROTATE_90:
		surface->rotation = ROTATION_ANGLE_90;
		break;
	case DRM_ROTATE_180:
		surface->rotation = ROTATION_ANGLE_180;
		break;
	case DRM_ROTATE_270:
		surface->rotation = ROTATION_ANGLE_270;
		break;
	default:
		surface->rotation = ROTATION_ANGLE_0;
		break;
	}

	return true;
}
static bool get_fb_info(
	const struct amdgpu_framebuffer *amdgpu_fb,
	uint64_t *tiling_flags,
	uint64_t *fb_location)
{
	struct amdgpu_bo *rbo = gem_to_amdgpu_bo(amdgpu_fb->obj);
	int r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0)){
		DRM_ERROR("Unable to reserve buffer\n");
		return false;
	}


	if (fb_location)
		*fb_location = amdgpu_bo_gpu_offset(rbo);

	if (tiling_flags)
		amdgpu_bo_get_tiling_flags(rbo, tiling_flags);

	amdgpu_bo_unreserve(rbo);

	return true;
}
static void fill_plane_attributes_from_fb(
	struct dc_surface *surface,
	const struct amdgpu_framebuffer *amdgpu_fb)
{
	uint64_t tiling_flags;
	uint64_t fb_location;
	const struct drm_framebuffer *fb = &amdgpu_fb->base;

	get_fb_info(
		amdgpu_fb,
		&tiling_flags,
		&fb_location);

	surface->address.type                = PLN_ADDR_TYPE_GRAPHICS;
	surface->address.grph.addr.low_part  = lower_32_bits(fb_location);
	surface->address.grph.addr.high_part = upper_32_bits(fb_location);

	switch (fb->pixel_format) {
	case DRM_FORMAT_C8:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n", fb->bits_per_pixel);
		return;
	}

	surface->tiling_info.value = 0;

	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_2D_TILED_THIN1)
	{
		unsigned bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);


		/* XXX fix me for VI */
		surface->tiling_info.grph.NUM_BANKS = num_banks;
		surface->tiling_info.grph.ARRAY_MODE =
						ARRAY_2D_TILED_THIN1;
		surface->tiling_info.grph.TILE_SPLIT = tile_split;
		surface->tiling_info.grph.BANK_WIDTH = bankw;
		surface->tiling_info.grph.BANK_HEIGHT = bankh;
		surface->tiling_info.grph.TILE_ASPECT = mtaspect;
		surface->tiling_info.grph.TILE_MODE =
				ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== ARRAY_1D_TILED_THIN1) {
		surface->tiling_info.grph.ARRAY_MODE = ARRAY_1D_TILED_THIN1;
	}

	surface->tiling_info.grph.PIPE_CONFIG =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	surface->plane_size.grph.surface_size.x = 0;
	surface->plane_size.grph.surface_size.y = 0;
	surface->plane_size.grph.surface_size.width = fb->width;
	surface->plane_size.grph.surface_size.height = fb->height;
	surface->plane_size.grph.surface_pitch =
		fb->pitches[0] / (fb->bits_per_pixel / 8);

	surface->enabled = true;
	surface->scaling_quality.h_taps_c = 2;
	surface->scaling_quality.v_taps_c = 2;

/* TODO: unhardcode */
	surface->colorimetry.limited_range = false;
	surface->colorimetry.color_space = SURFACE_COLOR_SPACE_SRGB;
	surface->scaling_quality.h_taps = 4;
	surface->scaling_quality.v_taps = 4;
	surface->stereo_format = PLANE_STEREO_FORMAT_NONE;

}

static void fill_gamma_from_crtc(
	const struct drm_crtc *crtc,
	struct dc_surface *dc_surface)
{
	int i;
	struct gamma_ramp *gamma;
	uint16_t *red, *green, *blue;
	int end = (crtc->gamma_size > NUM_OF_RAW_GAMMA_RAMP_RGB_256) ?
			NUM_OF_RAW_GAMMA_RAMP_RGB_256 : crtc->gamma_size;

	red = crtc->gamma_store;
	green = red + crtc->gamma_size;
	blue = green + crtc->gamma_size;

	gamma = &dc_surface->gamma_correction;

	for (i = 0; i < end; i++) {
		gamma->gamma_ramp_rgb256x3x16.red[i] =
				(unsigned short) red[i];
		gamma->gamma_ramp_rgb256x3x16.green[i] =
				(unsigned short) green[i];
		gamma->gamma_ramp_rgb256x3x16.blue[i] =
				(unsigned short) blue[i];
	}

	gamma->type = GAMMA_RAMP_RBG256X3X16;
	gamma->size = sizeof(gamma->gamma_ramp_rgb256x3x16);
}

static void fill_plane_attributes(
			struct dc_surface *surface,
			const struct drm_crtc *crtc)
{
	const struct amdgpu_framebuffer *amdgpu_fb =
		to_amdgpu_framebuffer(crtc->primary->state->fb);
	fill_rects_from_plane_state(crtc->primary->state, surface);
	fill_plane_attributes_from_fb(
		surface,
		amdgpu_fb);

	/* In case of gamma set, update gamma value */
	if (crtc->mode.private_flags &
			AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET) {
		fill_gamma_from_crtc(crtc, surface);
	}
}

/*****************************************************************************/

struct amdgpu_connector *aconnector_from_drm_crtc_id(
		const struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_connector *aconnector;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_connector(connector);

		/* acrtc->crtc_id means display_index */
		if (aconnector->connector_id != acrtc->crtc_id)
			continue;

		/* Found the connector */
		return aconnector;
	}

	/* If we get here, not found. */
	return NULL;
}

static void calculate_stream_scaling_settings(
		const struct drm_display_mode *mode,
		const struct dc_stream *stream,
		struct dm_connector_state *dm_state)
{
	enum amdgpu_rmx_type rmx_type;

	struct rect src = { 0 }; /* viewport in target space*/
	struct rect dst = { 0 }; /* stream addressable area */

	/* Full screen scaling by default */
	src.width = mode->hdisplay;
	src.height = mode->vdisplay;
	dst.width = stream->timing.h_addressable;
	dst.height = stream->timing.v_addressable;

	rmx_type = dm_state->scaling;
	if (rmx_type == RMX_ASPECT || rmx_type == RMX_OFF) {
		if (src.width * dst.height <
				src.height * dst.width) {
			/* height needs less upscaling/more downscaling */
			dst.width = src.width *
					dst.height / src.height;
		} else {
			/* width needs less upscaling/more downscaling */
			dst.height = src.height *
					dst.width / src.width;
		}
	} else if (rmx_type == RMX_CENTER) {
		dst = src;
	}

	dst.x = (stream->timing.h_addressable - dst.width) / 2;
	dst.y = (stream->timing.v_addressable - dst.height) / 2;

	if (dm_state->underscan_enable) {
		dst.x += dm_state->underscan_hborder / 2;
		dst.y += dm_state->underscan_vborder / 2;
		dst.width -= dm_state->underscan_hborder;
		dst.height -= dm_state->underscan_vborder;
	}

	dc_update_stream(stream, &src, &dst);

	DRM_DEBUG_KMS("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
			dst.x, dst.y, dst.width, dst.height);

}

static void dm_dc_surface_commit(
		struct dc *dc,
		struct drm_crtc *crtc,
		struct dm_connector_state *dm_state)
{
	struct dc_surface *dc_surface;
	const struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct dc_target *dc_target = acrtc->target;

	if (!dc_target) {
		dal_error(
			"%s: Failed to obtain target on crtc (%d)!\n",
			__func__,
			acrtc->crtc_id);
		goto fail;
	}

	dc_surface = dc_create_surface(dc);

	if (!dc_surface) {
		dal_error(
			"%s: Failed to create a surface!\n",
			__func__);
		goto fail;
	}

	calculate_stream_scaling_settings(&crtc->state->mode,
			dc_target->streams[0],
			dm_state);

	/* Surface programming */
	fill_plane_attributes(dc_surface, crtc);
	if (crtc->mode.private_flags &
		AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET) {
		/* reset trigger of gamma */
		crtc->mode.private_flags &=
			~AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET;
	}

	if (false == dc_commit_surfaces_to_target(
			dc,
			&dc_surface,
			1,
			dc_target)) {
		dal_error(
			"%s: Failed to attach surface!\n",
			__func__);
	}

	dc_surface_release(dc_surface);
fail:
	return;
}

static enum dc_color_depth convert_color_depth_from_display_info(
		const struct drm_connector *connector)
{
	uint32_t bpc = connector->display_info.bpc;

	/* Limited color depth to 8bit
	 * TODO: Still need to handle deep color*/
	if (bpc > 8)
		bpc = 8;

	switch (bpc) {
	case 0:
		/* Temporary Work around, DRM don't parse color depth for
		 * EDID revision before 1.4
		 * TODO: Fix edid parsing
		 */
		return COLOR_DEPTH_888;
	case 6:
		return COLOR_DEPTH_666;
	case 8:
		return COLOR_DEPTH_888;
	case 10:
		return COLOR_DEPTH_101010;
	case 12:
		return COLOR_DEPTH_121212;
	case 14:
		return COLOR_DEPTH_141414;
	case 16:
		return COLOR_DEPTH_161616;
	default:
		return COLOR_DEPTH_UNDEFINED;
	}
}

static enum dc_aspect_ratio get_aspect_ratio(
		const struct drm_display_mode *mode_in)
{
	int32_t width = mode_in->crtc_hdisplay * 9;
	int32_t height = mode_in->crtc_vdisplay * 16;
	if ((width - height) < 10 && (width - height) > -10)
		return ASPECT_RATIO_16_9;
	else
		return ASPECT_RATIO_4_3;
}

/*****************************************************************************/

static void dc_timing_from_drm_display_mode(
	struct dc_crtc_timing *timing_out,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector)
{
	memset(timing_out, 0, sizeof(struct dc_crtc_timing));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */
	timing_out->pixel_encoding = PIXEL_ENCODING_RGB;
	timing_out->timing_standard = TIMING_STANDARD_HDMI;
	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
			connector);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;
	timing_out->vic = drm_match_cea_mode(mode_in);

	timing_out->h_addressable = mode_in->crtc_hdisplay;
	timing_out->h_total = mode_in->crtc_htotal;
	timing_out->h_sync_width =
		mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
	timing_out->h_front_porch =
		mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
	timing_out->v_total = mode_in->crtc_vtotal;
	timing_out->v_addressable = mode_in->crtc_vdisplay;
	timing_out->v_front_porch =
		mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
	timing_out->v_sync_width =
		mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
	timing_out->pix_clk_khz = mode_in->crtc_clock;
	timing_out->aspect_ratio = get_aspect_ratio(mode_in);
}

static void fill_audio_info(
	struct audio_info *audio_info,
	const struct drm_connector *drm_connector,
	const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	while (i < AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS &&
		edid_caps->display_name[i]) {
		audio_info->display_name[i] = edid_caps->display_name[i];
		i++;
	}

	if(cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if(drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}

/*TODO: move these defines elsewhere*/
#define DAL_MAX_CONTROLLERS 4

static void copy_crtc_timing_for_drm_display_mode(
		const struct drm_display_mode *src_mode,
		struct drm_display_mode *dst_mode)
{
	dst_mode->crtc_hdisplay = src_mode->crtc_hdisplay;
	dst_mode->crtc_vdisplay = src_mode->crtc_vdisplay;
	dst_mode->crtc_clock = src_mode->crtc_clock;
	dst_mode->crtc_hblank_start = src_mode->crtc_hblank_start;
	dst_mode->crtc_hblank_end = src_mode->crtc_hblank_end;
	dst_mode->crtc_hsync_start=  src_mode->crtc_hsync_start;
	dst_mode->crtc_hsync_end = src_mode->crtc_hsync_end;
	dst_mode->crtc_htotal = src_mode->crtc_htotal;
	dst_mode->crtc_hskew = src_mode->crtc_hskew;
	dst_mode->crtc_vblank_start = src_mode->crtc_vblank_start;;
	dst_mode->crtc_vblank_end = src_mode->crtc_vblank_end;;
	dst_mode->crtc_vsync_start = src_mode->crtc_vsync_start;;
	dst_mode->crtc_vsync_end = src_mode->crtc_vsync_end;;
	dst_mode->crtc_vtotal = src_mode->crtc_vtotal;;
}

static void decide_crtc_timing_for_drm_display_mode(
		struct drm_display_mode *drm_mode,
		const struct drm_display_mode *native_mode,
		bool scale_enabled)
{
	if (scale_enabled) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else if (native_mode->clock == drm_mode->clock &&
			native_mode->htotal == drm_mode->htotal &&
			native_mode->vtotal == drm_mode->vtotal) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}


static struct dc_target *create_target_for_sink(
		const struct amdgpu_connector *aconnector,
		struct drm_display_mode *drm_mode)
{
	struct drm_display_mode *preferred_mode = NULL;
	const struct drm_connector *drm_connector;
	struct dm_connector_state *dm_state;
	struct dc_target *target = NULL;
	struct dc_stream *stream;
	struct drm_display_mode mode = *drm_mode;
	bool native_mode_found = false;

	if (NULL == aconnector) {
		DRM_ERROR("aconnector is NULL!\n");
		goto drm_connector_null;
	}

	drm_connector = &aconnector->base;
	dm_state = to_dm_connector_state(drm_connector->state);
	stream = dc_create_stream_for_sink(aconnector->dc_sink);

	if (NULL == stream) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	list_for_each_entry(preferred_mode, &aconnector->base.modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&aconnector->base.modes,
				struct drm_display_mode,
				head);

	decide_crtc_timing_for_drm_display_mode(
			&mode, preferred_mode,
			dm_state->scaling != RMX_OFF);

	dc_timing_from_drm_display_mode(&stream->timing,
			&mode, &aconnector->base);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		aconnector->dc_sink);

	target = dc_create_target_for_streams(&stream, 1);
	dc_stream_release(stream);

	if (NULL == target) {
		DRM_ERROR("Failed to create target with streams!\n");
		goto target_create_fail;
	}

drm_connector_null:
target_create_fail:
stream_create_fail:
	return target;
}

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static void amdgpu_dm_atomic_crtc_gamma_set(
		struct drm_crtc *crtc,
		u16 *red,
		u16 *green,
		u16 *blue,
		uint32_t start,
		uint32_t size)
{
	struct drm_device *dev = crtc->dev;
	struct drm_property *prop = dev->mode_config.prop_crtc_id;

	crtc->mode.private_flags |= AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET;

	drm_atomic_helper_crtc_set_property(crtc, prop, 0);
}

static int dm_crtc_funcs_atomic_set_property(
	struct drm_crtc *crtc,
	struct drm_crtc_state *state,
	struct drm_property *property,
	uint64_t val)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *new_crtc;
	int i;

	for_each_crtc_in_state(state->state, new_crtc, new_crtc_state, i) {
		if (new_crtc == crtc) {
			struct drm_plane_state *plane_state;

			new_crtc_state->planes_changed = true;

			/*
			 * Bit of magic done here. We need to ensure
			 * that planes get update after mode is set.
			 * So, we need to add primary plane to state,
			 * and this way atomic_update would be called
			 * for it
			 */
			plane_state =
				drm_atomic_get_plane_state(
					state->state,
					crtc->primary);

			if (!plane_state)
				return -EINVAL;
		}
	}

	return 0;
}

/* Implemented only the options currently availible for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.cursor_set = dm_crtc_cursor_set,
	.cursor_move = dm_crtc_cursor_move,
	.destroy = amdgpu_dm_crtc_destroy,
	.gamma_set = amdgpu_dm_atomic_crtc_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.atomic_set_property = dm_crtc_funcs_atomic_set_property
};

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_connector *aconnector =
			to_amdgpu_connector(connector);

	/*
	 * TODO: check whether we should lock here for mst_mgr.lock
	 */
	/* set root connector to disconnected */
	if (aconnector->is_mst_connector) {
		if (!aconnector->mst_mgr.mst_state)
			drm_dp_mst_topology_mgr_set_mst(
				&aconnector->mst_mgr,
				true);

		return connector_status_disconnected;
	}

	connected = (NULL != aconnector->dc_sink);
	return (connected ? connector_status_connected :
			connector_status_disconnected);
}

int amdgpu_dm_connector_atomic_set_property(
	struct drm_connector *connector,
	struct drm_connector_state *state,
	struct drm_property *property,
	uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_connector_state *dm_old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *dm_new_state =
		to_dm_connector_state(state);

	if (property == dev->mode_config.scaling_mode_property) {
		struct drm_crtc_state *new_crtc_state;
		struct drm_crtc *crtc;
		int i;
		enum amdgpu_rmx_type rmx_type;

		switch (val) {
		case DRM_MODE_SCALE_CENTER:
			rmx_type = RMX_CENTER;
			break;
		case DRM_MODE_SCALE_ASPECT:
			rmx_type = RMX_ASPECT;
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			rmx_type = RMX_FULL;
			break;
		case DRM_MODE_SCALE_NONE:
		default:
			rmx_type = RMX_OFF;
			break;
		}

		if (dm_old_state->scaling == rmx_type)
			return 0;

		dm_new_state->scaling = rmx_type;

		for_each_crtc_in_state(state->state, crtc, new_crtc_state, i) {
			if (crtc == state->crtc) {
				struct drm_plane_state *plane_state;

				new_crtc_state->mode_changed = true;

				/*
				 * Bit of magic done here. We need to ensure
				 * that planes get update after mode is set.
				 * So, we need to add primary plane to state,
				 * and this way atomic_update would be called
				 * for it
				 */
				plane_state =
					drm_atomic_get_plane_state(
						state->state,
						crtc->primary);

				if (!plane_state)
					return -EINVAL;
			}
		}

		return 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		dm_new_state->underscan_hborder = val;
		return 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		dm_new_state->underscan_vborder = val;
		return 0;
	} else if (property == adev->mode_info.underscan_property) {
		struct drm_crtc_state *new_crtc_state;
		struct drm_crtc *crtc;
		int i;

		dm_new_state->underscan_enable = val;
		for_each_crtc_in_state(state->state, crtc, new_crtc_state, i) {
			if (crtc == state->crtc) {
				struct drm_plane_state *plane_state;

				/*
				 * Bit of magic done here. We need to ensure
				 * that planes get update after mode is set.
				 * So, we need to add primary plane to state,
				 * and this way atomic_update would be called
				 * for it
				 */
				plane_state =
					drm_atomic_get_plane_state(
						state->state,
						crtc->primary);

				if (!plane_state)
					return -EINVAL;
			}
		}

		return 0;
	}

	return -EINVAL;
}

void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	/*drm_sysfs_connector_remove(connector);*/
	drm_connector_cleanup(connector);
	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	kfree(state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state) {
		state->scaling = RMX_OFF;
		state->underscan_enable = false;
		state->underscan_hborder = 0;
		state->underscan_vborder = 0;

		connector->state = &state->base;
		connector->state->connector = connector;
	}
}

struct drm_connector_state *amdgpu_dm_connector_atomic_duplicate_state(
	struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
		kzalloc(sizeof(*new_state), GFP_KERNEL);

	if (new_state) {
		*new_state = *state;

		return &new_state->base;
	}

	return NULL;
}

void amdgpu_dm_connector_atomic_destroy_state(
	struct drm_connector *connector,
	struct drm_connector_state *state)
{
	struct dm_connector_state *dm_state =
		to_dm_connector_state(state);

	__drm_atomic_helper_connector_destroy_state(connector, state);

	kfree(dm_state);
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = drm_atomic_helper_connector_set_property,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = amdgpu_dm_connector_atomic_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property
};

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

static int get_modes(struct drm_connector *connector)
{
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);
	return amdgpu_connector->num_modes;
}

static int mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	const struct dc_sink *dc_sink =
			to_amdgpu_connector(connector)->dc_sink;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct dc_validation_set val_set = { 0 };
	/* TODO: Unhardcode stream count */
	struct dc_stream *streams[1];
	struct dc_target *target;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	if (NULL == dc_sink) {
		DRM_ERROR("dc_sink is NULL!\n");
		goto stream_create_fail;
	}

	streams[0] = dc_create_stream_for_sink(dc_sink);

	if (NULL == streams[0]) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	drm_mode_set_crtcinfo(mode, 0);
	dc_timing_from_drm_display_mode(&streams[0]->timing, mode, connector);

	target = dc_create_target_for_streams(streams, 1);
	val_set.target = target;

	if (NULL == val_set.target) {
		DRM_ERROR("Failed to create target with stream!\n");
		goto target_create_fail;
	}

	val_set.surface_count = 0;
	streams[0]->src.width = mode->hdisplay;
	streams[0]->src.height = mode->vdisplay;
	streams[0]->dst = streams[0]->src;

	if (dc_validate_resources(adev->dm.dc, &val_set, 1))
		result = MODE_OK;

	dc_target_release(target);
target_create_fail:
	dc_stream_release(streams[0]);
stream_create_fail:
	/* TODO: error handling*/
	return result;
}


static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	* If hotplug a second bigger display in FB Con mode, bigger resolution
	* modes will be filtered by drm_mode_validate_size(), and those modes
	* is missing after user start lightdm. So we need to renew modes list.
	* in get_modes call back, not just return the modes count
	*/
	.get_modes = get_modes,
	.mode_valid = mode_valid,
	.best_encoder = best_encoder
};

static void dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static int dm_crtc_helper_atomic_check(
	struct drm_crtc *crtc,
	struct drm_crtc_state *state)
{
	return 0;
}

static bool dm_crtc_helper_mode_fixup(
	struct drm_crtc *crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = dm_crtc_helper_disable,
	.atomic_check = dm_crtc_helper_atomic_check,
	.mode_fixup = dm_crtc_helper_mode_fixup
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

static int dm_encoder_helper_atomic_check(
	struct drm_encoder *encoder,
	struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

static const struct drm_plane_funcs dm_plane_funcs = {
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state
};

static void clear_unrelated_fields(struct drm_plane_state *state)
{
	state->crtc = NULL;
	state->fb = NULL;
	state->state = NULL;
	state->fence = NULL;
}

static bool page_flip_needed(
	const struct drm_plane_state *new_state,
	const struct drm_plane_state *old_state)
{
	struct drm_plane_state old_state_tmp;
	struct drm_plane_state new_state_tmp;

	struct amdgpu_framebuffer *amdgpu_fb_old;
	struct amdgpu_framebuffer *amdgpu_fb_new;

	uint64_t old_tiling_flags;
	uint64_t new_tiling_flags;

	if (!old_state)
		return false;

	if (!old_state->fb)
		return false;

	if (!new_state)
		return false;

	if (!new_state->fb)
		return false;

	old_state_tmp = *old_state;
	new_state_tmp = *new_state;

	if (!new_state->crtc->state->event)
		return false;

	amdgpu_fb_old = to_amdgpu_framebuffer(old_state->fb);
	amdgpu_fb_new = to_amdgpu_framebuffer(new_state->fb);

	if (!get_fb_info(amdgpu_fb_old, &old_tiling_flags, NULL))
		return false;

	if (!get_fb_info(amdgpu_fb_new, &new_tiling_flags, NULL))
		return false;

	if (old_tiling_flags != new_tiling_flags)
		return false;

	clear_unrelated_fields(&old_state_tmp);
	clear_unrelated_fields(&new_state_tmp);

	return memcmp(&old_state_tmp, &new_state_tmp, sizeof(old_state_tmp)) == 0;
}

static int dm_plane_helper_prepare_fb(
	struct drm_plane *plane,
	const struct drm_plane_state *new_state)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	int r;

	if (!new_state->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(new_state->fb);

	DRM_DEBUG_KMS("Pin new framebuffer: %p\n", afb);
	obj = afb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, NULL);

	amdgpu_bo_unreserve(rbo);

	if (unlikely(r != 0)) {
		DRM_ERROR("Failed to pin framebuffer\n");
		return r;
	}

	return 0;
}

static void dm_plane_helper_cleanup_fb(
	struct drm_plane *plane,
	const struct drm_plane_state *old_state)
{
	struct amdgpu_bo *rbo;
	struct amdgpu_framebuffer *afb;
	int r;

	if (!old_state->fb)
		return;

	afb = to_amdgpu_framebuffer(old_state->fb);
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

int dm_create_validation_set_for_target(struct drm_connector *connector,
		struct drm_display_mode *mode, struct dc_validation_set *val_set)
{
	int result = MODE_ERROR;
	const struct dc_sink *dc_sink =
			to_amdgpu_connector(connector)->dc_sink;
	/* TODO: Unhardcode stream count */
	struct dc_stream *streams[1];
	struct dc_target *target;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	if (NULL == dc_sink) {
		DRM_ERROR("dc_sink is NULL!\n");
		return result;
	}

	streams[0] = dc_create_stream_for_sink(dc_sink);

	if (NULL == streams[0]) {
		DRM_ERROR("Failed to create stream for sink!\n");
		return result;
	}

	drm_mode_set_crtcinfo(mode, 0);
	dc_timing_from_drm_display_mode(&streams[0]->timing, mode, connector);

	target = dc_create_target_for_streams(streams, 1);
	val_set->target = target;

	if (NULL == val_set->target) {
		DRM_ERROR("Failed to create target with stream!\n");
		goto fail;
	}

	streams[0]->src.width = mode->hdisplay;
	streams[0]->src.height = mode->vdisplay;
	streams[0]->dst = streams[0]->src;

	return MODE_OK;

fail:
	dc_stream_release(streams[0]);
	return result;

}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = dm_plane_helper_prepare_fb,
	.cleanup_fb = dm_plane_helper_cleanup_fb,
};

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC to implement proper check
 */
static uint32_t rgb_formats[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
};

int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct amdgpu_crtc *acrtc,
			uint32_t link_index)
{
	int res = -ENOMEM;

	struct drm_plane *primary_plane =
		kzalloc(sizeof(*primary_plane), GFP_KERNEL);

	if (!primary_plane)
		goto fail_plane;

	/* this flag is used in legacy code only */
	primary_plane->format_default = true;

	res = drm_universal_plane_init(
		dm->adev->ddev,
		primary_plane,
		0,
		&dm_plane_funcs,
		rgb_formats,
		ARRAY_SIZE(rgb_formats),
		DRM_PLANE_TYPE_PRIMARY, NULL);

	primary_plane->crtc = &acrtc->base;

	drm_plane_helper_add(primary_plane, &dm_plane_helper_funcs);

	res = drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			primary_plane,
			NULL,
			&amdgpu_dm_crtc_funcs, NULL);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	acrtc->max_cursor_width = 128;
	acrtc->max_cursor_height = 128;

	acrtc->crtc_id = link_index;
	acrtc->base.enabled = false;

	dm->adev->mode_info.crtcs[link_index] = acrtc;
	drm_mode_crtc_set_gamma_size(&acrtc->base, 256);

	return 0;
fail:
	kfree(primary_plane);
fail_plane:
	acrtc->crtc_id = -1;
	return res;
}

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
		return DRM_MODE_CONNECTOR_DVID;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
		connector->helper_private;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = helper->best_encoder(connector);

	if (encoder == NULL)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->native_mode.clock = 0;

	if (!list_empty(&connector->probed_modes)) {
		struct drm_display_mode *preferred_mode = NULL;
		list_for_each_entry(preferred_mode,
				&connector->probed_modes,
				head) {
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			amdgpu_encoder->native_mode = *preferred_mode;
		}
			break;
		}

	}
}

static struct drm_display_mode *amdgpu_dm_create_common_mode(
		struct drm_encoder *encoder, char *name,
		int hdisplay, int vdisplay)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;

	mode = drm_mode_duplicate(dev, native_mode);

	if(mode == NULL)
		return NULL;

	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	strncpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

static void amdgpu_dm_connector_add_common_modes(struct drm_encoder *encoder,
					struct drm_connector *connector)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	struct amdgpu_connector *amdgpu_connector =
				to_amdgpu_connector(connector);
	int i;
	int n;
	struct mode_size {
		char name[DRM_DISPLAY_MODE_LEN];
		int w;
		int h;
	}common_modes[] = {
		{  "640x480",  640,  480},
		{  "800x600",  800,  600},
		{ "1024x768", 1024,  768},
		{ "1280x720", 1280,  720},
		{ "1280x800", 1280,  800},
		{"1280x1024", 1280, 1024},
		{ "1440x900", 1440,  900},
		{"1680x1050", 1680, 1050},
		{"1600x1200", 1600, 1200},
		{"1920x1080", 1920, 1080},
		{"1920x1200", 1920, 1200}
	};

	n = sizeof(common_modes) / sizeof(common_modes[0]);

	for (i = 0; i < n; i++) {
		struct drm_display_mode *curmode = NULL;
		bool mode_existed = false;

		if (common_modes[i].w > native_mode->hdisplay ||
			common_modes[i].h > native_mode->vdisplay ||
			(common_modes[i].w == native_mode->hdisplay &&
			common_modes[i].h == native_mode->vdisplay))
				continue;

		list_for_each_entry(curmode, &connector->probed_modes, head) {
			if (common_modes[i].w == curmode->hdisplay &&
				common_modes[i].h == curmode->vdisplay) {
				mode_existed = true;
				break;
			}
		}

		if (mode_existed)
			continue;

		mode = amdgpu_dm_create_common_mode(encoder,
				common_modes[i].name, common_modes[i].w,
				common_modes[i].h);
		drm_mode_probed_add(connector, mode);
		amdgpu_connector->num_modes++;
	}
}

static void amdgpu_dm_connector_ddc_get_modes(
	struct drm_connector *connector,
	struct edid *edid)
{
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	if (edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_connector->num_modes =
				drm_add_edid_modes(connector, edid);

		drm_edid_to_eld(connector, edid);

		amdgpu_dm_get_native_mode(connector);
	} else
		amdgpu_connector->num_modes = 0;
}

int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
			connector->helper_private;
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_connector->edid;

	encoder = helper->best_encoder(connector);

	amdgpu_dm_connector_ddc_get_modes(connector, edid);
	amdgpu_dm_connector_add_common_modes(encoder, connector);
	return amdgpu_connector->num_modes;
}

/* Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector. */
int amdgpu_dm_connector_init(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	uint32_t link_index,
	struct amdgpu_encoder *aencoder)
{
	int res, connector_type;
	struct amdgpu_device *adev = dm->ddev->dev_private;
	struct dc *dc = dm->dc;
	const struct dc_link *link = dc_get_link_at_index(dc, link_index);

	DRM_DEBUG_KMS("%s()\n", __func__);

	connector_type = to_drm_connector_type(link->connector_signal);

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

	aconnector->connector_id = link_index;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = true;
	aconnector->base.doublescan_allowed = true;
	aconnector->hpd.hpd = link_index; /* maps to 'enum amdgpu_hpd_id' */

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

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

	/* TODO: Don't do this manually anymore
	aconnector->base.encoder = &aencoder->base;
	*/

	drm_mode_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	/*drm_sysfs_connector_add(&dm_connector->base);*/

	drm_connector_register(&aconnector->base);

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort)
		amdgpu_dm_initialize_mst_connector(dm, aconnector);

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	/* NOTE: this currently will create backlight device even if a panel
	 * is not connected to the eDP/LVDS connector.
	 *
	 * This is less than ideal but we don't have sink information at this
	 * stage since detection happens after. We can't do detection earlier
	 * since MST detection needs connectors to be created first.
	 */
	if (link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) {
		/* Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen. */
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev)
			dm->backlight_link = link;
	}
#endif

	return 0;
}

int amdgpu_dm_encoder_init(
	struct drm_device *dev,
	struct amdgpu_encoder *aencoder,
	uint32_t link_index,
	struct amdgpu_crtc *acrtc)
{
	int res = drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS,
				   NULL);

	aencoder->base.possible_crtcs = 1 << link_index;

	if (!res)
		aencoder->encoder_id = link_index;
	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &amdgpu_dm_encoder_helper_funcs);

	return res;
}

enum dm_commit_action {
	DM_COMMIT_ACTION_NOTHING,
	DM_COMMIT_ACTION_RESET,
	DM_COMMIT_ACTION_DPMS_ON,
	DM_COMMIT_ACTION_DPMS_OFF,
	DM_COMMIT_ACTION_SET
};

static enum dm_commit_action get_dm_commit_action(struct drm_crtc_state *state)
{
	/* mode changed means either actually mode changed or enabled changed */
	/* active changed means dpms changed */
	if (state->mode_changed) {
		/* if it is got disabled - call reset mode */
		if (!state->enable)
			return DM_COMMIT_ACTION_RESET;

		if (state->active)
			return DM_COMMIT_ACTION_SET;
		else
			return DM_COMMIT_ACTION_RESET;
	} else {
		/* ! mode_changed */

		/* if it is remain disable - skip it */
		if (!state->enable)
			return DM_COMMIT_ACTION_NOTHING;

		if (state->active_changed) {
			if (state->active) {
				return DM_COMMIT_ACTION_DPMS_ON;
			} else {
				return DM_COMMIT_ACTION_DPMS_OFF;
			}
		} else {
			/* ! active_changed */
			return DM_COMMIT_ACTION_NOTHING;
		}
	}
}

static void manage_dm_interrupts(
	struct amdgpu_device *adev,
	struct amdgpu_crtc *acrtc,
	bool enable)
{
	if (enable) {
		drm_crtc_vblank_on(&acrtc->base);
		amdgpu_irq_get(
			adev,
			&adev->pageflip_irq,
			amdgpu_crtc_idx_to_irq_type(
				adev,
				acrtc->crtc_id));
	} else {
		unsigned long flags;
		amdgpu_irq_put(
			adev,
			&adev->pageflip_irq,
			amdgpu_crtc_idx_to_irq_type(
				adev,
				acrtc->crtc_id));
		drm_crtc_vblank_off(&acrtc->base);

		/*
		 * should be called here, to guarantee no works left in queue.
		 * As this function sleeps it was bug to call it inside the
		 * amdgpu_dm_flip_cleanup function under locked event_lock
		 */
		if (acrtc->pflip_works) {
			flush_work(&acrtc->pflip_works->flip_work);
			flush_work(&acrtc->pflip_works->unpin_work);
		}

		/*
		 * TODO: once Vitaly's change to adjust locking in
		 * page_flip_work_func is submitted to base driver move
		 * lock and check to amdgpu_dm_flip_cleanup function
		 */

		spin_lock_irqsave(&adev->ddev->event_lock, flags);
		if (acrtc->pflip_status != AMDGPU_FLIP_NONE) {
			/*
			 * this is the case when on reset, last pending pflip
			 * interrupt did not not occur. Clean-up
			 */
			amdgpu_dm_flip_cleanup(adev, acrtc);
		}
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
	}
}

/*
 * Handle headless hotplug workaround
 *
 * In case of headless hotplug, if plugging the same monitor to the same
 * DDI, DRM consider it as mode unchanged. We should check whether the
 * sink pointer changed, and set mode_changed properly to
 * make sure commit is doing everything.
 */
static void handle_headless_hotplug(
		const struct amdgpu_crtc *acrtc,
		struct drm_crtc_state *state,
		struct amdgpu_connector **aconnector)
{
	struct amdgpu_connector *old_connector =
			aconnector_from_drm_crtc_id(&acrtc->base);

	/*
	 * TODO Revisit this. This code is kinda hacky and might break things.
	 */

	if (!old_connector)
		return;

	if (!*aconnector)
		*aconnector = old_connector;

	if (acrtc->target && (*aconnector)->dc_sink) {
		if ((*aconnector)->dc_sink !=
				acrtc->target->streams[0]->sink) {
			state->mode_changed = true;
		}
	}

	if (!acrtc->target) {
		/* In case of headless with DPMS on, when system waked up,
		 * if no monitor connected, target is null and will not create
		 * new target, on that condition, we should check
		 * if any connector is connected, if connected,
		 * it means a hot plug happened after wake up,
		 * mode_changed should be set to true to make sure
		 * commit targets will do everything.
		 */
		state->mode_changed =
			(*aconnector)->base.status ==
					connector_status_connected;
	} else {
		/* In case of headless hotplug, if plug same monitor to same
		 * DDI, DRM consider it as mode unchanged, we should check
		 * sink pointer changed, and set mode changed properly to
		 * make sure commit doing everything.
		 */
		/* check if sink has changed from last commit */
		if ((*aconnector)->dc_sink && (*aconnector)->dc_sink !=
					acrtc->target->streams[0]->sink)
			state->mode_changed = true;
	}
}

int amdgpu_dm_atomic_commit(
	struct drm_device *dev,
	struct drm_atomic_state *state,
	bool async)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	uint32_t i, j;
	int32_t ret;
	uint32_t commit_targets_count = 0;
	uint32_t new_crtcs_count = 0;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;

	struct dc_target *commit_targets[DAL_MAX_CONTROLLERS];
	struct amdgpu_crtc *new_crtcs[DAL_MAX_CONTROLLERS];

	/* In this step all new fb would be pinned */

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(dev, state);

	/*
	 * From this point state become old state really. New state is
	 * initialized to appropriate objects and could be accessed from there
	 */

	/*
	 * there is no fences usage yet in state. We can skip the following line
	 * wait_for_fences(dev, state);
	 */

	/* update changed items */
	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		struct amdgpu_crtc *acrtc;
		struct amdgpu_connector *aconnector = NULL;
		enum dm_commit_action action;
		struct drm_crtc_state *new_state = crtc->state;
		struct drm_connector *connector;
		struct drm_connector_state *old_con_state;

		acrtc = to_amdgpu_crtc(crtc);

		for_each_connector_in_state(state,
				connector, old_con_state, j) {
			if (connector->state->crtc == crtc) {
				aconnector = to_amdgpu_connector(connector);
				break;
			}
		}

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */
		handle_headless_hotplug(acrtc, new_state, &aconnector);
		if (!aconnector) {
			DRM_ERROR("Can't find connector for crtc %d\n",
							acrtc->crtc_id);
			break;
		}

		action = get_dm_commit_action(new_state);

		switch (action) {
		case DM_COMMIT_ACTION_DPMS_ON:
		case DM_COMMIT_ACTION_SET: {
			const struct drm_connector_helper_funcs *connector_funcs;
			struct dc_target *new_target =
				create_target_for_sink(
					aconnector,
					&crtc->state->mode);
			DRM_DEBUG_KMS("Atomic commit: SET.\n");
			if (!new_target) {
				/*
				 * this could happen because of issues with
				 * userspace notifications delivery.
				 * In this case userspace tries to set mode on
				 * display which is disconnect in fact.
				 * dc_sink in NULL in this case on aconnector.
				 * We expect reset mode will come soon.
				 *
				 * This can also happen when unplug is done
				 * during resume sequence ended
				 */
				new_state->planes_changed = false;
				break;
			}

			if (acrtc->target) {
				/*
				 * we evade vblanks and pflips on crtc that
				 * should be changed
				 */
				manage_dm_interrupts(adev, acrtc, false);
				/* this is the update mode case */
				dc_target_release(acrtc->target);
				acrtc->target = NULL;
			}

			/*
			 * this loop saves set mode crtcs
			 * we needed to enable vblanks once all
			 * resources acquired in dc after dc_commit_targets
			 */
			new_crtcs[new_crtcs_count] = acrtc;
			new_crtcs_count++;

			acrtc->target = new_target;
			acrtc->enabled = true;
			acrtc->base.enabled = true;
			connector_funcs = aconnector->base.helper_private;
			aconnector->base.encoder =
				connector_funcs->best_encoder(
					&aconnector->base);
			break;
		}

		case DM_COMMIT_ACTION_NOTHING:
			break;

		case DM_COMMIT_ACTION_DPMS_OFF:
		case DM_COMMIT_ACTION_RESET:
			DRM_DEBUG_KMS("Atomic commit: RESET.\n");
			/* i.e. reset mode */
			if (acrtc->target) {
				manage_dm_interrupts(adev, acrtc, false);

				dc_target_release(acrtc->target);
				acrtc->target = NULL;
				acrtc->enabled = false;
				acrtc->base.enabled = false;
				aconnector->base.encoder = NULL;
			}
			break;
		} /* switch() */
	} /* for_each_crtc_in_state() */

	commit_targets_count = 0;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->target) {
			commit_targets[commit_targets_count] = acrtc->target;
			++commit_targets_count;
		}
	}

	/* DC is optimized not to do anything if 'targets' didn't change. */
	dc_commit_targets(dm->dc, commit_targets, commit_targets_count);

	/* update planes when needed */
	for_each_plane_in_state(state, plane, old_plane_state, i) {
		struct drm_plane_state *plane_state = plane->state;
		struct drm_crtc *crtc = plane_state->crtc;
		struct drm_framebuffer *fb = plane_state->fb;
		struct drm_connector *connector;

		if (fb && crtc && crtc->state->planes_changed) {
			struct dm_connector_state *dm_state = NULL;

			if (page_flip_needed(
				plane_state,
				old_plane_state))
				amdgpu_crtc_page_flip(
					crtc,
					fb,
					crtc->state->event,
					0);
			else {
				list_for_each_entry(connector,
						&dev->mode_config.connector_list, head)	{
					if (connector->state->crtc == crtc) {
						dm_state = to_dm_connector_state(connector->state);
						break;
					}
				}

				dm_dc_surface_commit(
					dm->dc,
					crtc,
					dm_state);
			}
		}
	}

	for (i = 0; i < new_crtcs_count; i++) {
		/*
		 * loop to enable interrupts on newly arrived crtc
		 */
		struct amdgpu_crtc *acrtc = new_crtcs[i];

		manage_dm_interrupts(adev, acrtc, true);
		dm_crtc_cursor_reset(&acrtc->base);

	}

	drm_atomic_helper_wait_for_vblanks(dev, state);

	/* In this state all old framebuffers would be unpinned */

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	return 0;
}

static uint32_t add_val_sets_surface(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_target *target,
	const struct dc_surface *surface)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].target == target)
			break;
		++i;
	}

	val_sets[i].surfaces[val_sets[i].surface_count] = surface;
	val_sets[i].surface_count++;

	return val_sets[i].surface_count;
}

static uint32_t update_in_val_sets_target(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_target *old_target,
	const struct dc_target *new_target)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].target == old_target)
			break;
		++i;
	}

	val_sets[i].target = new_target;

	if (i == set_count) {
		/* nothing found. add new one to the end */
		return set_count + 1;
	}

	return set_count;
}

static uint32_t remove_from_val_sets(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_target *target)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].target == target)
			break;
		++i;
	}

	if (i == set_count) {
		/* nothing found */
		return set_count;
	}

	memmove(
		&val_sets[i],
		&val_sets[i + 1],
		sizeof(struct dc_validation_set *) * (set_count - i - 1));

	return set_count - 1;
}

int amdgpu_dm_atomic_check(struct drm_device *dev,
			struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, j, ret, set_count, new_target_count;
	struct dc_validation_set set[MAX_TARGET_NUM] = {{ 0 }};
	struct dc_target *new_targets[MAX_TARGET_NUM] = { 0 };
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;

	ret = drm_atomic_helper_check(dev, state);

	if (ret) {
		DRM_ERROR("Atomic state validation failed with error :%d !\n",
				ret);
		return ret;
	}

	ret = -EINVAL;

	if (state->num_connector > MAX_TARGET_NUM) {
		DRM_ERROR("Exceeded max targets number !\n");
		return ret;
	}

	/* copy existing configuration */
	new_target_count = 0;
	set_count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->target) {
			set[set_count].target = acrtc->target;
			++set_count;
		}
	}

	/* update changed items */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct amdgpu_crtc *acrtc = NULL;
		struct amdgpu_connector *aconnector = NULL;
		enum dm_commit_action action;
		struct drm_connector *connector;
		struct drm_connector_state *con_state;

		acrtc = to_amdgpu_crtc(crtc);

		for_each_connector_in_state(state, connector, con_state, j) {
			if (con_state->crtc == crtc) {
				aconnector = to_amdgpu_connector(connector);
				break;
			}
		}

		/*TODO:
		handle_headless_hotplug(acrtc, crtc_state, &aconnector);*/

		action = get_dm_commit_action(crtc_state);

		switch (action) {
		case DM_COMMIT_ACTION_DPMS_ON:
		case DM_COMMIT_ACTION_SET: {
			struct drm_display_mode mode = crtc_state->mode;
			struct dc_target *new_target = NULL;

			if (!aconnector) {
				DRM_ERROR("Can't find connector for crtc %d\n",
								acrtc->crtc_id);
				goto connector_not_found;
			}
			new_target =
				create_target_for_sink(
					aconnector,
					&mode);
			new_targets[new_target_count] = new_target;

			set_count = update_in_val_sets_target(
					set,
					set_count,
					acrtc->target,
					new_target);
			new_target_count++;
			break;
		}

		case DM_COMMIT_ACTION_NOTHING:
			break;
		case DM_COMMIT_ACTION_DPMS_OFF:
		case DM_COMMIT_ACTION_RESET:
			/* i.e. reset mode */
			if (acrtc->target) {
				set_count = remove_from_val_sets(
						set,
						set_count,
						acrtc->target);
			}
			break;
		}
	}


	for (i = 0; i < set_count; i++) {
		for_each_plane_in_state(state, plane, plane_state, j) {
			struct drm_plane_state *old_plane_state = plane->state;
			struct drm_framebuffer *fb = plane_state->fb;
			struct amdgpu_crtc *acrtc =
					to_amdgpu_crtc(plane_state->crtc);

			if (!fb || acrtc->target != set[i].target)
				continue;
			if (!plane_state->crtc->state->planes_changed)
				continue;

			if (!page_flip_needed(plane_state, old_plane_state)) {
				struct dc_surface *surface =
					dc_create_surface(dc);

				fill_plane_attributes(
						surface, plane_state->crtc);
				add_val_sets_surface(
					set,
					set_count,
					acrtc->target,
					surface);
			}
		}

	}

	if (set_count == 0 || dc_validate_resources(dc, set, set_count))
		ret = 0;

connector_not_found:
	for (i = 0; i < set_count; i++) {
		for (j = 0; j < set[i].surface_count; j++) {
			dc_surface_release(
				(struct dc_surface *)set[i].surfaces[j]);
		}
	}
	for (i = 0; i < new_target_count; i++)
		dc_target_release(new_targets[i]);

	if (ret != 0)
		DRM_ERROR("Atomic check failed.\n");

	return ret;
}
