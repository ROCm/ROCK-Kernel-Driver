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
#include "dm_services.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "opp.h"
#include "transform.h"
#include "video_csc_types.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
#include "dce80/dce80_resource.h"
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
#include "dce100/dce100_resource.h"
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/dce110_resource.h"
#endif

bool dc_construct_resource_pool(struct adapter_service *adapter_serv,
				struct core_dc *dc,
				uint8_t num_virtual_links)
{
	enum dce_version dce_ver = dal_adapter_service_get_dce_version(adapter_serv);

	switch (dce_ver) {
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
		return dce100_construct_resource_pool(
			adapter_serv, num_virtual_links, dc, &dc->res_pool);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dce110_construct_resource_pool(
			adapter_serv, num_virtual_links, dc, &dc->res_pool);
#endif
	default:
		break;
	}

	return false;
}

void unreference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source)
{
	int i;
	for (i = 0; i < res_ctx->pool.clk_src_count; i++) {
		if (res_ctx->pool.clock_sources[i] == clock_source) {
			res_ctx->clock_source_ref_count[i]--;

		if (res_ctx->clock_source_ref_count[i] == 0)
			clock_source->funcs->cs_power_down(clock_source);
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
	return memcmp(timing1, timing2, sizeof(struct dc_crtc_timing)) == 0;
}

static bool is_sharable_clk_src(
	const struct pipe_ctx *pipe_with_clk_src,
	const struct pipe_ctx *pipe)
{
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	enum dce_version dce_ver = dal_adapter_service_get_dce_version(
		pipe->stream->sink->link->adapter_srv);

	/* Currently no clocks are shared for DCE 10 until VBIOS behaviour
	 * is verified for this use case
	 */
	if (dce_ver == DCE_VERSION_10_0 && !dc_is_dp_signal(pipe->signal))
		return false;
#endif

	if (pipe_with_clk_src->clock_source == NULL)
		return false;

	if (dc_is_dp_signal(pipe->signal) &&
		dc_is_dp_signal(pipe_with_clk_src->signal))
		return true;

	if (pipe->signal != pipe_with_clk_src->signal)
		return false;

	if(!is_same_timing(
		&pipe_with_clk_src->stream->public.timing,
		&pipe->stream->public.timing))
		return false;

	return true;
}

struct clock_source *find_used_clk_src_for_sharing(
					struct resource_context *res_ctx,
					struct pipe_ctx *pipe_ctx)
{
	uint8_t i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (is_sharable_clk_src(&res_ctx->pipe_ctx[i], pipe_ctx))
			return res_ctx->pipe_ctx[i].clock_source;
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

	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

static void calculate_viewport(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	const struct rect src = surface->src_rect;
	const struct rect clip = surface->clip_rect;
	const struct rect dst = surface->dst_rect;

	/* offset = src.ofs + (clip.ofs - dst.ofs) * scl_ratio
	 * num_pixels = clip.num_pix * scl_ratio
	 */
	pipe_ctx->scl_data.viewport.x = src.x + (clip.x - dst.x) * src.width / dst.width;
	pipe_ctx->scl_data.viewport.width = clip.width * src.width / dst.width;

	pipe_ctx->scl_data.viewport.y = src.y + (clip.y - dst.y) * src.height / dst.height;
	pipe_ctx->scl_data.viewport.height = clip.height * src.height / dst.height;

	/* Minimum viewport such that 420/422 chroma vp is non 0 */
	if (pipe_ctx->scl_data.viewport.width < 2)
		pipe_ctx->scl_data.viewport.width = 2;
	if (pipe_ctx->scl_data.viewport.height < 2)
		pipe_ctx->scl_data.viewport.height = 2;
}

static void calculate_overscan(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;

	pipe_ctx->scl_data.overscan.left = stream->public.dst.x;
	if (stream->public.src.x < surface->clip_rect.x)
		pipe_ctx->scl_data.overscan.left += (surface->clip_rect.x
			- stream->public.src.x) * stream->public.dst.width
			/ stream->public.src.width;

	pipe_ctx->scl_data.overscan.right = stream->public.timing.h_addressable
		- stream->public.dst.x - stream->public.dst.width;
	if (stream->public.src.x + stream->public.src.width
		> surface->clip_rect.x + surface->clip_rect.width)
		pipe_ctx->scl_data.overscan.right = stream->public.timing.h_addressable -
			dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(
						pipe_ctx->scl_data.viewport.width),
						pipe_ctx->scl_data.ratios.horz)) -
						pipe_ctx->scl_data.overscan.left;

	pipe_ctx->scl_data.overscan.top = stream->public.dst.y;
	if (stream->public.src.y < surface->clip_rect.y)
		pipe_ctx->scl_data.overscan.top += (surface->clip_rect.y
			- stream->public.src.y) * stream->public.dst.height
			/ stream->public.src.height;

	pipe_ctx->scl_data.overscan.bottom = stream->public.timing.v_addressable
		- stream->public.dst.y - stream->public.dst.height;
	if (stream->public.src.y + stream->public.src.height
		> surface->clip_rect.y + surface->clip_rect.height)
		pipe_ctx->scl_data.overscan.bottom = stream->public.timing.v_addressable -
			dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(
						pipe_ctx->scl_data.viewport.height),
						pipe_ctx->scl_data.ratios.vert)) -
						pipe_ctx->scl_data.overscan.top;

	/* TODO: Add timing overscan to finalize overscan calculation*/
}

static void calculate_scaling_ratios(
		const struct dc_surface *surface,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	const uint32_t in_w = stream->public.src.width;
	const uint32_t in_h = stream->public.src.height;
	const uint32_t out_w = stream->public.dst.width;
	const uint32_t out_h = stream->public.dst.height;

	pipe_ctx->scl_data.ratios.horz = dal_fixed31_32_from_fraction(
					surface->src_rect.width,
					surface->dst_rect.width);
	pipe_ctx->scl_data.ratios.vert = dal_fixed31_32_from_fraction(
					surface->src_rect.height,
					surface->dst_rect.height);

	if (surface->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE)
		pipe_ctx->scl_data.ratios.horz.value *= 2;
	else if (surface->stereo_format
					== PLANE_STEREO_FORMAT_TOP_AND_BOTTOM)
		pipe_ctx->scl_data.ratios.vert.value *= 2;

	pipe_ctx->scl_data.ratios.vert.value = div64_s64(pipe_ctx->scl_data.ratios.vert.value * in_h,
			out_h);
	pipe_ctx->scl_data.ratios.horz.value = div64_s64(pipe_ctx->scl_data.ratios.horz.value * in_w,
			out_w);

	pipe_ctx->scl_data.ratios.horz_c = pipe_ctx->scl_data.ratios.horz;
	pipe_ctx->scl_data.ratios.vert_c = pipe_ctx->scl_data.ratios.vert;

	if (pipe_ctx->scl_data.format == PIXEL_FORMAT_420BPP12) {
		pipe_ctx->scl_data.ratios.horz_c.value /= 2;
		pipe_ctx->scl_data.ratios.vert_c.value /= 2;
	}
}

void build_scaling_params(
	const struct dc_surface *surface,
	struct pipe_ctx *pipe_ctx)
{
	/* Important: scaling ratio calculation requires pixel format,
	 * overscan calculation requires scaling ratios and viewport
	 * and lb depth/taps calculation requires overscan. Call sequence
	 * is therefore important */
	pipe_ctx->scl_data.format = convert_pixel_format_to_dalsurface(surface->format);

	calculate_viewport(surface, pipe_ctx);

	calculate_scaling_ratios(surface, pipe_ctx);

	calculate_overscan(surface, pipe_ctx);

	/* Check if scaling is required update taps if not */
	if (dal_fixed31_32_u2d19(pipe_ctx->scl_data.ratios.horz) == 1 << 19)
		pipe_ctx->scl_data.taps.h_taps = 1;
	else
		pipe_ctx->scl_data.taps.h_taps = surface->scaling_quality.h_taps;

	if (dal_fixed31_32_u2d19(pipe_ctx->scl_data.ratios.horz_c) == 1 << 19)
		pipe_ctx->scl_data.taps.h_taps_c = 1;
	else
		pipe_ctx->scl_data.taps.h_taps_c = surface->scaling_quality.h_taps_c;

	if (dal_fixed31_32_u2d19(pipe_ctx->scl_data.ratios.vert) == 1 << 19)
		pipe_ctx->scl_data.taps.v_taps = 1;
	else
		pipe_ctx->scl_data.taps.v_taps = surface->scaling_quality.v_taps;

	if (dal_fixed31_32_u2d19(pipe_ctx->scl_data.ratios.vert_c) == 1 << 19)
		pipe_ctx->scl_data.taps.v_taps_c = 1;
	else
		pipe_ctx->scl_data.taps.v_taps_c = surface->scaling_quality.v_taps_c;

	dal_logger_write(pipe_ctx->stream->ctx->logger,
				LOG_MAJOR_DCP,
				LOG_MINOR_DCP_SCALER,
				"%s: Overscan:\n bot:%d left:%d right:%d "
				"top:%d\nViewport:\nheight:%d width:%d x:%d "
				"y:%d\n dst_rect:\nheight:%d width:%d x:%d "
				"y:%d\n",
				__func__,
				pipe_ctx->scl_data.overscan.bottom,
				pipe_ctx->scl_data.overscan.left,
				pipe_ctx->scl_data.overscan.right,
				pipe_ctx->scl_data.overscan.top,
				pipe_ctx->scl_data.viewport.height,
				pipe_ctx->scl_data.viewport.width,
				pipe_ctx->scl_data.viewport.x,
				pipe_ctx->scl_data.viewport.y,
				surface->dst_rect.height,
				surface->dst_rect.width,
				surface->dst_rect.x,
				surface->dst_rect.y);
}

void build_scaling_params_for_context(
	const struct core_dc *dc,
	struct validate_context *context)
{
	uint8_t i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context->res_ctx.pipe_ctx[i].surface != NULL &&
				context->res_ctx.pipe_ctx[i].stream != NULL)
			build_scaling_params(
				&context->res_ctx.pipe_ctx[i].surface->public,
				&context->res_ctx.pipe_ctx[i]);
	}
}

bool attach_surfaces_to_context(
		struct dc_surface *surfaces[],
		uint8_t surface_count,
		struct dc_target *dc_target,
		struct validate_context *context)
{
	uint8_t i, j, k;
	struct dc_target_status *target_status = NULL;

	if (surface_count > MAX_SURFACE_NUM) {
		dm_error("Surface: can not attach %d surfaces! Maximum is: %d\n",
			surface_count, MAX_SURFACE_NUM);
		return false;
	}

	for (i = 0; i < context->target_count; i++)
		if (&context->targets[i]->public == dc_target) {
			target_status = &context->target_status[i];
			break;
		}
	if (target_status == NULL) {
		dm_error("Existing target not found; failed to attach surfaces\n");
		return false;
	}

	/* retain new surfaces */
	for (i = 0; i < surface_count; i++)
		dc_surface_retain(surfaces[i]);

	/* release existing surfaces*/
	for (i = 0; i < target_status->surface_count; i++) {
		dc_surface_release(target_status->surfaces[i]);
		target_status->surfaces[i] = NULL;
	}

	/* assign new surfaces*/
	for (i = 0; i < surface_count; i++)
		target_status->surfaces[i] = surfaces[i];

	target_status->surface_count = surface_count;

	for (i = 0; i < dc_target->stream_count; i++) {
		k = 0;
		for (j = 0; j < MAX_PIPES; j++) {
			struct core_surface *surface =
					DC_SURFACE_TO_CORE(surfaces[k]);

			if (context->res_ctx.pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(dc_target->streams[i]))
				continue;
			if (k == surface_count) {
				/* this means there are more pipes per stream
				 * than there are planes and makes no sense
				 */
				BREAK_TO_DEBUGGER();
				continue;
			}

			context->res_ctx.pipe_ctx[j].surface = surface;
			k++;
		}
	}

	return true;
}

static uint32_t get_min_vblank_time_us(const struct validate_context *context)
{
	uint8_t i, j;
	uint32_t min_vertical_blank_time = -1;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct dc_stream *stream =
						target->public.streams[j];
			uint32_t vertical_blank_in_pixels = 0;
			uint32_t vertical_blank_time = 0;

			vertical_blank_in_pixels = stream->timing.h_total *
				(stream->timing.v_total
					- stream->timing.v_addressable);
			vertical_blank_time = vertical_blank_in_pixels
				* 1000 / stream->timing.pix_clk_khz;
			if (min_vertical_blank_time > vertical_blank_time)
				min_vertical_blank_time = vertical_blank_time;
		}
	}
	return min_vertical_blank_time;
}

static void fill_display_configs(
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	uint8_t i, j, k;
	uint8_t num_cfgs = 0;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct dm_pp_single_disp_config *cfg =
					&pp_display_cfg->disp_configs[num_cfgs];
			const struct pipe_ctx *pipe_ctx = NULL;

			for (k = 0; k < MAX_PIPES; k++)
				if (stream ==
					context->res_ctx.pipe_ctx[k].stream) {
					pipe_ctx = &context->res_ctx.pipe_ctx[k];
					break;
				}

			num_cfgs++;
			cfg->signal = pipe_ctx->signal;
			cfg->pipe_idx = pipe_ctx->pipe_idx;
			cfg->src_height = stream->public.src.height;
			cfg->src_width = stream->public.src.width;
			cfg->ddi_channel_mapping =
				stream->sink->link->ddi_channel_mapping.raw;
			cfg->transmitter =
				stream->sink->link->link_enc->transmitter;
			cfg->link_settings.lane_count = stream->sink->link->public.cur_link_settings.lane_count;
			cfg->link_settings.link_rate = stream->sink->link->public.cur_link_settings.link_rate;
			cfg->link_settings.link_spread = stream->sink->link->public.cur_link_settings.link_spread;
			cfg->sym_clock = stream->public.timing.pix_clk_khz;
			switch (stream->public.timing.display_color_depth) {
			case COLOR_DEPTH_101010:
				cfg->sym_clock = (cfg->sym_clock * 30) / 24;
				break;
			case COLOR_DEPTH_121212:
				cfg->sym_clock = (cfg->sym_clock * 36) / 24;
				break;
			case COLOR_DEPTH_161616:
				cfg->sym_clock = (cfg->sym_clock * 48) / 24;
				break;
			default:
				break;
			}
			/* TODO: unhardcode*/
			cfg->v_refresh = 60;
		}
	}
	pp_display_cfg->display_count = num_cfgs;
}

void pplib_apply_safe_state(
	const struct core_dc *dc)
{
	dm_pp_apply_safe_state(dc->ctx);
}

void pplib_apply_display_requirements(
	const struct core_dc *dc,
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{

	pp_display_cfg->all_displays_in_sync =
		context->bw_results.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw_results.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw_results.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw_results.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw_results.required_blackout_duration_us;

	pp_display_cfg->min_memory_clock_khz = context->bw_results.required_yclk
		/ MEMORY_TYPE_MULTIPLIER;
	pp_display_cfg->min_engine_clock_khz = context->bw_results.required_sclk;
	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw_results.required_sclk_deep_sleep;

	pp_display_cfg->avail_mclk_switch_time_us =
						get_min_vblank_time_us(context);
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = context->bw_results.dispclk_khz;

	fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->targets[0]->public.streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 1000
							/ timing->pix_clk_khz;
	}

	dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

/* Maximum TMDS single link pixel clock 165MHz */
#define TMDS_MAX_PIXEL_CLOCK_IN_KHZ 165000

static void set_stream_engine_in_use(
		struct resource_context *res_ctx,
		struct stream_encoder *stream_enc)
{
	int i;

	for (i = 0; i < res_ctx->pool.stream_enc_count; i++) {
		if (res_ctx->pool.stream_enc[i] == stream_enc)
			res_ctx->is_stream_enc_acquired[i] = true;
	}
}

/* TODO: release audio object */
static void set_audio_in_use(
		struct resource_context *res_ctx,
		struct audio *audio)
{
	int i;
	for (i = 0; i < res_ctx->pool.audio_count; i++) {
		if (res_ctx->pool.audios[i] == audio) {
			res_ctx->is_audio_acquired[i] = true;
		}
	}
}

static int8_t acquire_first_free_pipe(
		struct resource_context *res_ctx,
		struct core_stream *stream)
{
	uint8_t i;
	for (i = 0; i < res_ctx->pool.pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream) {
			struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[i];

			pipe_ctx->tg = res_ctx->pool.timing_generators[i];
			pipe_ctx->mi = res_ctx->pool.mis[i];
			pipe_ctx->ipp = res_ctx->pool.ipps[i];
			pipe_ctx->xfm = res_ctx->pool.transforms[i];
			pipe_ctx->opp = res_ctx->pool.opps[i];
			pipe_ctx->dis_clk = res_ctx->pool.display_clock;
			pipe_ctx->pipe_idx = i;

			pipe_ctx->stream = stream;
			return i;
		}
	}
	return -1;
}

static struct stream_encoder *find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		struct core_link *link)
{
	uint8_t i;
	int8_t j = -1;
	const struct dc_sink *sink = NULL;

	for (i = 0; i < res_ctx->pool.stream_enc_count; i++) {
		if (!res_ctx->is_stream_enc_acquired[i] &&
					res_ctx->pool.stream_enc[i]) {
			/* Store first available for MST second display
			 * in daisy chain use case */
			j = i;
			if (res_ctx->pool.stream_enc[i]->id ==
					link->link_enc->preferred_engine)
				return res_ctx->pool.stream_enc[i];
		}
	}

	/*
	 * below can happen in cases when stream encoder is acquired:
	 * 1) for second MST display in chain, so preferred engine already
	 * acquired;
	 * 2) for another link, which preferred engine already acquired by any
	 * MST configuration.
	 *
	 * If signal is of DP type and preferred engine not found, return last available
	 *
	 * TODO - This is just a patch up and a generic solution is
	 * required for non DP connectors.
	 */

	sink = link->public.local_sink ? link->public.local_sink : link->public.remote_sinks[0];

	if (sink && j >= 0 &&  dc_is_dp_signal(sink->sink_signal))
		return res_ctx->pool.stream_enc[j];

	return NULL;
}

static struct audio *find_first_free_audio(struct resource_context *res_ctx)
{
	int i;
	for (i = 0; i < res_ctx->pool.audio_count; i++) {
		if (res_ctx->is_audio_acquired[i] == false) {
			return res_ctx->pool.audios[i];
		}
	}

	return 0;
}

static bool check_timing_change(struct core_stream *cur_stream,
		struct core_stream *new_stream)
{
	if (cur_stream == NULL)
		return true;

	/* If sink pointer changed, it means this is a hotplug, we should do
	 * full hw setting.
	 */
	if (cur_stream->sink != new_stream->sink)
		return true;

	return !is_same_timing(
					&cur_stream->public.timing,
					&new_stream->public.timing);
}

static void set_stream_signal(struct pipe_ctx *pipe_ctx)
{
	struct dc_sink *dc_sink =
		(struct dc_sink *) pipe_ctx->stream->public.sink;

	/* For asic supports dual link DVI, we should adjust signal type
	 * based on timing pixel clock. If pixel clock more than 165Mhz,
	 * signal is dual link, otherwise, single link.
	 */
	if (dc_sink->sink_signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
			dc_sink->sink_signal == SIGNAL_TYPE_DVI_DUAL_LINK) {
		if (pipe_ctx->stream->public.timing.pix_clk_khz >
						TMDS_MAX_PIXEL_CLOCK_IN_KHZ)
			dc_sink->sink_signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		else
			dc_sink->sink_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}

	pipe_ctx->signal = dc_sink->sink_signal;

	/* Down-grade pipe_ctx signal instead of sink singal from HDMI to DVI
	 * here based on audio info in stream. This allows DC to handle stream
	 * with or without audio on a HDMI connector.
	 *
	 * On a HDMI to DVI passive dongle, audio info is not available from the
	 * EDID and the signal is down-graded in pipe ctx.
	 */
	if (pipe_ctx->signal == SIGNAL_TYPE_HDMI_TYPE_A &&
			pipe_ctx->stream->public.audio_info.mode_count == 0)
		pipe_ctx->signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
}

enum dc_status map_resources(
		const struct core_dc *dc,
		struct validate_context *context)
{
	uint8_t i, j, k;

	/* mark resources used for targets that are already active */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (!context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);

			for (k = 0; k < MAX_PIPES; k++) {
				struct pipe_ctx *pipe_ctx =
					&context->res_ctx.pipe_ctx[k];

				if (dc->current_context.res_ctx.pipe_ctx[k].stream
					!= stream)
					continue;

				*pipe_ctx =
					dc->current_context.res_ctx.pipe_ctx[k];
				pipe_ctx->flags.timing_changed = false;
				pipe_ctx->flags.unchanged = true;

				set_stream_engine_in_use(
					&context->res_ctx,
					pipe_ctx->stream_enc);

				reference_clock_source(
					&context->res_ctx,
					pipe_ctx->clock_source);

				set_audio_in_use(&context->res_ctx,
					pipe_ctx->audio);
			}
		}
	}

	/* acquire new resources */
	for (i = 0; i < context->target_count; i++) {
		struct core_target *target = context->targets[i];

		if (context->target_flags[i].unchanged)
			continue;

		for (j = 0; j < target->public.stream_count; j++) {
			struct pipe_ctx *pipe_ctx = NULL;
			struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct core_stream *curr_stream;

			int8_t pipe_idx = acquire_first_free_pipe(
						&context->res_ctx, stream);
			if (pipe_idx < 0)
				return DC_NO_CONTROLLER_RESOURCE;

			pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];
			set_stream_signal(pipe_ctx);

			curr_stream =
				dc->current_context.res_ctx.pipe_ctx[pipe_idx].stream;
			context->res_ctx.pipe_ctx[pipe_idx].flags.timing_changed =
				check_timing_change(curr_stream, stream);

			pipe_ctx->stream_enc =
				find_first_free_match_stream_enc_for_link(
					&context->res_ctx,
					stream->sink->link);

			if (!pipe_ctx->stream_enc)
				return DC_NO_STREAM_ENG_RESOURCE;

			set_stream_engine_in_use(
					&context->res_ctx,
					pipe_ctx->stream_enc);

			/* TODO: Add check if ASIC support and EDID audio */
			if (!stream->sink->converter_disable_audio &&
						dc_is_audio_capable_signal(
						pipe_ctx->signal)) {
				pipe_ctx->audio = find_first_free_audio(
						&context->res_ctx);

				/*
				 * Audio assigned in order first come first get.
				 * There are asics which has number of audio
				 * resources less then number of pipes
				 */
				if (pipe_ctx->audio)
					set_audio_in_use(
						&context->res_ctx,
						pipe_ctx->audio);
			}
		}
	}

	return DC_OK;
}

static enum ds_color_space build_default_color_space(
		struct pipe_ctx *pipe_ctx)
{
	enum ds_color_space color_space =
			DS_COLOR_SPACE_SRGB_FULLRANGE;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->public.timing;

	switch (pipe_ctx->signal) {
	/* TODO: implement other signal color space setting */
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
	{
		uint32_t pix_clk_khz;

		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422 &&
			timing->pixel_encoding == PIXEL_ENCODING_YCBCR444) {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 &&
				timing->timing_standard ==
						TIMING_STANDARD_CEA861)
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;

			pix_clk_khz = timing->pix_clk_khz / 10;
			if (timing->h_addressable == 640 &&
				timing->v_addressable == 480 &&
				(pix_clk_khz == 2520 || pix_clk_khz == 2517))
				color_space = DS_COLOR_SPACE_SRGB_FULLRANGE;
		} else {
			if (timing->timing_standard ==
					TIMING_STANDARD_CEA770 ||
					timing->timing_standard ==
					TIMING_STANDARD_CEA861) {

				color_space =
					(timing->pix_clk_khz > PIXEL_CLOCK) ?
						DS_COLOR_SPACE_YCBCR709 :
						DS_COLOR_SPACE_YCBCR601;
			}
		}
		break;
	}
	default:
		switch (timing->pixel_encoding) {
		case PIXEL_ENCODING_YCBCR422:
		case PIXEL_ENCODING_YCBCR444:
			if (timing->pix_clk_khz > PIXEL_CLOCK)
				color_space = DS_COLOR_SPACE_YCBCR709;
			else
				color_space = DS_COLOR_SPACE_YCBCR601;
			break;
		default:
			break;
		}
		break;
	}
	return color_space;
}

static void translate_info_frame(const struct hw_info_frame *hw_info_frame,
	struct encoder_info_frame *encoder_info_frame)
{
	memset(
		encoder_info_frame, 0, sizeof(struct encoder_info_frame));

	/* For gamut we recalc checksum */
	if (hw_info_frame->gamut_packet.valid) {
		uint8_t chk_sum = 0;
		uint8_t *ptr;
		uint8_t i;

		memmove(
						&encoder_info_frame->gamut,
						&hw_info_frame->gamut_packet,
						sizeof(struct hw_info_packet));

		/*start of the Gamut data. */
		ptr = &encoder_info_frame->gamut.sb[3];

		for (i = 0; i <= encoder_info_frame->gamut.sb[1]; i++)
			chk_sum += ptr[i];

		encoder_info_frame->gamut.sb[2] = (uint8_t) (0x100 - chk_sum);
	}

	if (hw_info_frame->avi_info_packet.valid) {
		memmove(
						&encoder_info_frame->avi,
						&hw_info_frame->avi_info_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vendor_info_packet.valid) {
		memmove(
						&encoder_info_frame->vendor,
						&hw_info_frame->vendor_info_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->spd_packet.valid) {
		memmove(
						&encoder_info_frame->spd,
						&hw_info_frame->spd_packet,
						sizeof(struct hw_info_packet));
	}

	if (hw_info_frame->vsc_packet.valid) {
		memmove(
						&encoder_info_frame->vsc,
						&hw_info_frame->vsc_packet,
						sizeof(struct hw_info_packet));
	}
}

static void set_avi_info_frame(
	struct hw_info_packet *info_packet,
		struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	enum ds_color_space color_space = DS_COLOR_SPACE_UNKNOWN;
	struct info_frame info_frame = { {0} };
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum dc_aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	bool itc = false;
	uint8_t cn0_cn1 = 0;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;

	if (info_packet == NULL)
		return;

	color_space = build_default_color_space(pipe_ctx);

	/* Initialize header */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.
			info_frame_type = INFO_FRAME_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.version =
			INFO_FRAME_VERSION_2;
	info_frame.avi_info_packet.info_packet_hdmi.bits.header.length =
			INFO_FRAME_SIZE_AVI;

	/* IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	* according to HDMI 2.0 spec (Section 10.1)
	* Add "case PixelEncoding_YCbCr420:    pixelEncoding = 3; break;"
	* when YCbCr 4:2:0 is supported by DAL hardware. */

	switch (stream->public.timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		pixel_encoding = 1;
		break;

	case PIXEL_ENCODING_YCBCR444:
		pixel_encoding = 2;
		break;

	case PIXEL_ENCODING_RGB:
	default:
		pixel_encoding = 0;
	}

	/* Y0_Y1_Y2 : The pixel encoding */
	/* H14b AVI InfoFrame has extension on Y-field from 2 bits to 3 bits */
	info_frame.avi_info_packet.info_packet_hdmi.bits.Y0_Y1_Y2 =
		pixel_encoding;

	/* A0 = 1 Active Format Information valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.A0 =
		ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	info_frame.avi_info_packet.info_packet_hdmi.bits.B0_B1 =
		BAR_INFO_BOTH_VALID;

	info_frame.avi_info_packet.info_packet_hdmi.bits.SC0_SC1 =
			PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */
	/* TODO: un-hardcode scan type */
	scan_type = SCANNING_TYPE_UNDERSCAN;
	info_frame.avi_info_packet.info_packet_hdmi.bits.S0_S1 = scan_type;

	/* C0, C1 : Colorimetry */
	if (color_space == DS_COLOR_SPACE_YCBCR709)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU709;
	else if (color_space == DS_COLOR_SPACE_YCBCR601)
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_ITU601;
	else
		info_frame.avi_info_packet.info_packet_hdmi.bits.C0_C1 =
				COLORIMETRY_NO_DATA;

	/* TODO: un-hardcode aspect ratio */
	aspect = stream->public.timing.aspect_ratio;

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	case ASPECT_RATIO_64_27:
	case ASPECT_RATIO_256_135:
	default:
		info_frame.avi_info_packet.info_packet_hdmi.bits.M0_M1 = 0;
	}

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.R0_R3 =
			ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	/* TODO: un-hardcode cn0_cn1 and itc */
	cn0_cn1 = 0;
	itc = false;

	if (itc) {
		info_frame.avi_info_packet.info_packet_hdmi.bits.ITC = 1;
		info_frame.avi_info_packet.info_packet_hdmi.bits.CN0_CN1 =
			cn0_cn1;
	}

	/* TODO: un-hardcode q0_q1 */
	if (color_space == DS_COLOR_SPACE_SRGB_FULLRANGE)
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_FULL_RANGE;
	else if (color_space == DS_COLOR_SPACE_SRGB_LIMITEDRANGE)
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_LIMITED_RANGE;
	else
		info_frame.avi_info_packet.info_packet_hdmi.bits.Q0_Q1 =
						RGB_QUANTIZATION_DEFAULT_RANGE;

	/* TODO : We should handle YCC quantization,
	 * but we do not have matrix calculation */
	info_frame.avi_info_packet.info_packet_hdmi.bits.YQ0_YQ1 =
					YYC_QUANTIZATION_LIMITED_RANGE;

	info_frame.avi_info_packet.info_packet_hdmi.bits.VIC0_VIC7 =
					stream->public.timing.vic;

	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	info_frame.avi_info_packet.info_packet_hdmi.bits.PR0_PR3 = 0;

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_top =
			stream->public.timing.v_border_top;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_bottom =
		(stream->public.timing.v_border_top
			- stream->public.timing.v_border_bottom + 1);
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_left =
			stream->public.timing.h_border_left;
	info_frame.avi_info_packet.info_packet_hdmi.bits.bar_right =
		(stream->public.timing.h_total
			- stream->public.timing.h_border_right + 1);

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum =
		&info_frame.
		avi_info_packet.info_packet_hdmi.packet_raw_data.sb[0];
	*check_sum = INFO_FRAME_AVI + INFO_FRAME_SIZE_AVI
			+ INFO_FRAME_VERSION_2;

	for (byte_index = 1; byte_index <= INFO_FRAME_SIZE_AVI; byte_index++)
		*check_sum += info_frame.avi_info_packet.info_packet_hdmi.
				packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb0;
	info_packet->hb1 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb1;
	info_packet->hb2 =
		info_frame.avi_info_packet.info_packet_hdmi.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(info_packet->sb); byte_index++)
		info_packet->sb[byte_index] = info_frame.avi_info_packet.
		info_packet_hdmi.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

static void set_vendor_info_packet(struct core_stream *stream,
		struct hw_info_packet *info_packet)
{
	uint32_t length = 0;
	bool hdmi_vic_mode = false;
	uint8_t checksum = 0;
	uint32_t i = 0;
	enum dc_timing_3d_format format;

	ASSERT_CRITICAL(stream != NULL);
	ASSERT_CRITICAL(info_packet != NULL);

	format = stream->public.timing.timing_3d_format;

	/* Can be different depending on packet content */
	length = 5;

	if (stream->public.timing.hdmi_vic != 0
			&& stream->public.timing.h_total >= 3840
			&& stream->public.timing.v_total >= 2160)
		hdmi_vic_mode = true;

	/* According to HDMI 1.4a CTS, VSIF should be sent
	 * for both 3D stereo and HDMI VIC modes.
	 * For all other modes, there is no VSIF sent.  */

	if (format == TIMING_3D_FORMAT_NONE && !hdmi_vic_mode)
		return;

	/* 24bit IEEE Registration identifier (0x000c03). LSB first. */
	info_packet->sb[1] = 0x03;
	info_packet->sb[2] = 0x0C;
	info_packet->sb[3] = 0x00;

	/*PB4: 5 lower bytes = 0 (reserved). 3 higher bits = HDMI_Video_Format.
	 * The value for HDMI_Video_Format are:
	 * 0x0 (0b000) - No additional HDMI video format is presented in this
	 * packet
	 * 0x1 (0b001) - Extended resolution format present. 1 byte of HDMI_VIC
	 * parameter follows
	 * 0x2 (0b010) - 3D format indication present. 3D_Structure and
	 * potentially 3D_Ext_Data follows
	 * 0x3..0x7 (0b011..0b111) - reserved for future use */
	if (format != TIMING_3D_FORMAT_NONE)
		info_packet->sb[4] = (2 << 5);
	else if (hdmi_vic_mode)
		info_packet->sb[4] = (1 << 5);

	/* PB5: If PB4 claims 3D timing (HDMI_Video_Format = 0x2):
	 * 4 lower bites = 0 (reserved). 4 higher bits = 3D_Structure.
	 * The value for 3D_Structure are:
	 * 0x0 - Frame Packing
	 * 0x1 - Field Alternative
	 * 0x2 - Line Alternative
	 * 0x3 - Side-by-Side (full)
	 * 0x4 - L + depth
	 * 0x5 - L + depth + graphics + graphics-depth
	 * 0x6 - Top-and-Bottom
	 * 0x7 - Reserved for future use
	 * 0x8 - Side-by-Side (Half)
	 * 0x9..0xE - Reserved for future use
	 * 0xF - Not used */
	switch (format) {
	case TIMING_3D_FORMAT_HW_FRAME_PACKING:
	case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		info_packet->sb[5] = (0x0 << 4);
		break;

	case TIMING_3D_FORMAT_SIDE_BY_SIDE:
	case TIMING_3D_FORMAT_SBS_SW_PACKED:
		info_packet->sb[5] = (0x8 << 4);
		length = 6;
		break;

	case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
	case TIMING_3D_FORMAT_TB_SW_PACKED:
		info_packet->sb[5] = (0x6 << 4);
		break;

	default:
		break;
	}

	/*PB5: If PB4 is set to 0x1 (extended resolution format)
	 * fill PB5 with the correct HDMI VIC code */
	if (hdmi_vic_mode)
		info_packet->sb[5] = stream->public.timing.hdmi_vic;

	/* Header */
	info_packet->hb0 = 0x81; /* VSIF packet type. */
	info_packet->hb1 = 0x01; /* Version */

	/* 4 lower bits = Length, 4 higher bits = 0 (reserved) */
	info_packet->hb2 = (uint8_t) (length);

	/* Calculate checksum */
	checksum = 0;
	checksum += info_packet->hb0;
	checksum += info_packet->hb1;
	checksum += info_packet->hb2;

	for (i = 1; i <= length; i++)
		checksum += info_packet->sb[i];

	info_packet->sb[0] = (uint8_t) (0x100 - checksum);

	info_packet->valid = true;
}

void val_ctx_destruct(struct validate_context *context)
{
	int i, j;

	for (i = 0; i < context->target_count; i++) {
		for (j = 0; j < context->target_status[i].surface_count; j++)
			dc_surface_release(
				context->target_status[i].surfaces[j]);

		context->target_status[i].surface_count = 0;
		dc_target_release(&context->targets[i]->public);
	}
}

/*
 * Copy src_ctx into dst_ctx and retain all surfaces and targets referenced
 * by the src_ctx
 */
void val_ctx_copy_construct(
		const struct validate_context *src_ctx,
		struct validate_context *dst_ctx)
{
	int i, j;

	*dst_ctx = *src_ctx;

	for (i = 0; i < dst_ctx->target_count; i++) {
		dc_target_retain(&dst_ctx->targets[i]->public);
		for (j = 0; j < dst_ctx->target_status[i].surface_count; j++)
			dc_surface_retain(
				dst_ctx->target_status[i].surfaces[j]);
	}
}

void build_info_frame(struct pipe_ctx *pipe_ctx)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct hw_info_frame info_frame = { { 0 } };

	/* default all packets to invalid */
	info_frame.avi_info_packet.valid = false;
	info_frame.gamut_packet.valid = false;
	info_frame.vendor_info_packet.valid = false;
	info_frame.spd_packet.valid = false;
	info_frame.vsc_packet.valid = false;

	signal = pipe_ctx->stream->sink->public.sink_signal;

	/* HDMi and DP have different info packets*/
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		set_avi_info_frame(
			&info_frame.avi_info_packet, pipe_ctx);
		set_vendor_info_packet(
			pipe_ctx->stream, &info_frame.vendor_info_packet);
	}

	translate_info_frame(&info_frame,
			&pipe_ctx->encoder_info_frame);
}
