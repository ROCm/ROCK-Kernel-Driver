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
#include "dc_services.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "link_encoder_types.h"
#include "stream_encoder_types.h"


void unreference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source)
{
	int i;
	for (i = 0; i < res_ctx->pool.clk_src_count; i++) {
		if (res_ctx->pool.clock_sources[i] == clock_source) {
			res_ctx->clock_source_ref_count[i]--;
		}
	}
}

void reference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source)
{
	int i;
	for (i = 0; i < res_ctx->pool.clk_src_count; i++) {
		if (res_ctx->pool.clock_sources[i] == clock_source) {
			res_ctx->clock_source_ref_count[i]++;
		}
	}
}

bool is_same_timing(
	const struct dc_crtc_timing *timing1,
	const struct dc_crtc_timing *timing2)
{
	return dal_memcmp(timing1, timing2, sizeof(struct dc_crtc_timing)) == 0;
}

static bool is_sharable_clk_src(
	const struct core_stream *stream_with_clk_src,
	const struct core_stream *stream)
{
	enum clock_source_id id = dal_clock_source_get_id(
					stream_with_clk_src->clock_source);

	if (stream_with_clk_src->clock_source == NULL)
		return false;

	if (!dc_is_dp_signal(stream->signal) && id == CLOCK_SOURCE_ID_EXTERNAL)
		return false;

	if(!is_same_timing(
		&stream_with_clk_src->public.timing, &stream->public.timing))
		return false;

	return true;
}

struct clock_source *find_used_clk_src_for_sharing(
					struct validate_context *context,
					struct core_stream *stream)
{
	uint8_t i, j;
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		for (j = 0; j < target->stream_count; j++)
		{
			if (target->streams[j]->clock_source == NULL)
				continue;
			if (is_sharable_clk_src(target->streams[j], stream))
				return target->streams[j]->clock_source;
		}
	}

	return NULL;
}

static enum pixel_format convert_pixel_format_to_dalsurface(
		enum surface_pixel_format surface_pixel_format)
{
	enum pixel_format dal_pixel_format = PIXEL_FORMAT_UNKNOWN;

	switch (surface_pixel_format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		dal_pixel_format = PIXEL_FORMAT_INDEX8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010_XRBIAS;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;


	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP12;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCb:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_YCr:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CbY:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_422_CrY:
		dal_pixel_format = PIXEL_FORMAT_422BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb1555:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CrYCb565:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb4444:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA5551:
		dal_pixel_format = PIXEL_FORMAT_444BPP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb8888:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_ACrYCb2101010:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_444_CbYCrA1010102:
		dal_pixel_format = PIXEL_FORMAT_444BPP32;
		break;
	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

static void calculate_viewport(
		const struct dc_surface *surface,
		struct core_stream *stream)
{
	const struct rect src = surface->src_rect;
	const struct rect clip = surface->clip_rect;
	const struct rect dst = surface->dst_rect;

	/* offset = src.ofs + (clip.ofs - dst.ofs) * scl_ratio
	 * num_pixels = clip.num_pix * scl_ratio
	 */
	stream->viewport.x = src.x + (clip.x - dst.x) * src.width / dst.width;
	stream->viewport.width = clip.width * src.width / dst.width;

	stream->viewport.y = src.y + (clip.y - dst.y) * src.height / dst.height;
	stream->viewport.height = clip.height * src.height / dst.height;

	/* Minimum viewport such that 420/422 chroma vp is non 0 */
	if (stream->viewport.width < 2)
	{
		stream->viewport.width = 2;
	}
	if (stream->viewport.height < 2)
	{
		stream->viewport.height = 2;
	}
}

static void calculate_overscan(
		const struct dc_surface *surface,
		struct core_stream *stream)
{
	stream->overscan.left = stream->public.dst.x;
	if (stream->public.src.x < surface->clip_rect.x)
		stream->overscan.left += (surface->clip_rect.x
			- stream->public.src.x) * stream->public.dst.width
			/ stream->public.src.width;

	stream->overscan.right = stream->public.timing.h_addressable
		- stream->public.dst.x - stream->public.dst.width;
	if (stream->public.src.x + stream->public.src.width
		> surface->clip_rect.x + surface->clip_rect.width)
		stream->overscan.right = stream->public.timing.h_addressable -
			dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(
						stream->viewport.width),
						stream->ratios.horz)) -
						stream->overscan.left;


	stream->overscan.top = stream->public.dst.y;
	if (stream->public.src.y < surface->clip_rect.y)
		stream->overscan.top += (surface->clip_rect.y
			- stream->public.src.y) * stream->public.dst.height
			/ stream->public.src.height;

	stream->overscan.bottom = stream->public.timing.v_addressable
		- stream->public.dst.y - stream->public.dst.height;
	if (stream->public.src.y + stream->public.src.height
		> surface->clip_rect.y + surface->clip_rect.height)
		stream->overscan.bottom = stream->public.timing.v_addressable -
			dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(
						stream->viewport.height),
						stream->ratios.vert)) -
						stream->overscan.top;


	/* TODO: Add timing overscan to finalize overscan calculation*/
}

static void calculate_scaling_ratios(
		const struct dc_surface *surface,
		struct core_stream *stream)
{
	const uint32_t in_w = stream->public.src.width;
	const uint32_t in_h = stream->public.src.height;
	const uint32_t out_w = stream->public.dst.width;
	const uint32_t out_h = stream->public.dst.height;

	stream->ratios.horz = dal_fixed31_32_from_fraction(
					surface->src_rect.width,
					surface->dst_rect.width);
	stream->ratios.vert = dal_fixed31_32_from_fraction(
					surface->src_rect.height,
					surface->dst_rect.height);

	if (surface->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE)
		stream->ratios.horz.value *= 2;
	else if (surface->stereo_format
					== PLANE_STEREO_FORMAT_TOP_AND_BOTTOM)
		stream->ratios.vert.value *= 2;

	stream->ratios.vert.value = div64_u64(stream->ratios.vert.value * in_h,
			out_h);
	stream->ratios.horz.value = div64_u64(stream->ratios.horz.value * in_w ,
			out_w);

	stream->ratios.horz_c = stream->ratios.horz;
	stream->ratios.vert_c = stream->ratios.vert;

	if (stream->format == PIXEL_FORMAT_420BPP12) {
		stream->ratios.horz_c.value /= 2;
		stream->ratios.vert_c.value /= 2;
	} else if (stream->format == PIXEL_FORMAT_422BPP16) {
		stream->ratios.horz_c.value /= 2;
	}
}

/*TODO: per pipe not per stream*/
void build_scaling_params(
	const struct dc_surface *surface,
	struct core_stream *stream)
{
	/* Important: scaling ratio calculation requires pixel format,
	 * overscan calculation requires scaling ratios and viewport
	 * and lb depth/taps calculation requires overscan. Call sequence
	 * is therefore important */
	stream->format = convert_pixel_format_to_dalsurface(surface->format);

	calculate_viewport(surface, stream);

	calculate_scaling_ratios(surface, stream);

	calculate_overscan(surface, stream);

	/* Check if scaling is required update taps if not */
	if (dal_fixed31_32_u2d19(stream->ratios.horz) == 1 << 19)
		stream->taps.h_taps = 1;
	else
		stream->taps.h_taps = surface->scaling_quality.h_taps;

	if (dal_fixed31_32_u2d19(stream->ratios.horz_c) == 1 << 19)
		stream->taps.h_taps_c = 1;
	else
		stream->taps.h_taps_c = surface->scaling_quality.h_taps_c;

	if (dal_fixed31_32_u2d19(stream->ratios.vert) == 1 << 19)
		stream->taps.v_taps = 1;
	else
		stream->taps.v_taps = surface->scaling_quality.v_taps;

	if (dal_fixed31_32_u2d19(stream->ratios.vert_c) == 1 << 19)
		stream->taps.v_taps_c = 1;
	else
		stream->taps.v_taps_c = surface->scaling_quality.v_taps_c;
}

void build_scaling_params_for_context(
	const struct dc *dc,
	struct validate_context *context)
{
	uint8_t i, j, k;
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];
		if (context->target_flags[i].unchanged)
			continue;
		for (j = 0; j < target->status.surface_count; j++) {
			const struct dc_surface *surface = target->status.surfaces[j];
			for (k = 0; k < target->stream_count; k++) {
				struct core_stream *stream = target->streams[k];

				build_scaling_params(surface, stream);
			}
		}
	}
}

bool logical_attach_surfaces_to_target(
		struct dc_surface *surfaces[],
		uint8_t surface_count,
		struct dc_target *dc_target)
{
	uint8_t i;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

	if (target->status.surface_count >= MAX_SURFACE_NUM) {
		dal_error("Surface: this target has too many surfaces!\n");
		return false;
	}

	for (i = 0; i < target->status.surface_count; i++)
		dc_surface_release(target->status.surfaces[i]);

	for (i = 0; i < surface_count; i++) {
		struct core_surface *surface = DC_SURFACE_TO_CORE(surfaces[i]);
		surface->status.dc_target = &target->public;
		target->status.surfaces[i] = surfaces[i];
		dc_surface_retain(target->status.surfaces[i]);
	}
	target->status.surface_count = surface_count;

	return true;
}
