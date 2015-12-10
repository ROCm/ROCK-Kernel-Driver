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

#include "dc_services.h"

#include "bandwidth_calcs.h"

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

enum bw_defines {
	def_ok,
	def_na,
	def_notok,
	def_display_write_back420_chroma,
	def_display_write_back420_luma,
	def_graphics,
	def_xl_pattern_solid,
	def_xl_pattern_light_horizontal,
	def_xl_pattern_checker,
	def_notok_color,
	def_na_color,
	def_vb_black,
	def_vb_white,
	def_high_no_nbp_state_change_color,
	def_high_no_nbp_state_change,
	def_high_color,
	def_mid_color,
	def_low_color,
	def_high,
	def_mid,
	def_low,
	def_exceeded_allowed_maximum_sclk,
	def_exceeded_allowed_maximum_bw,
	def_exceeded_allowed_page_close_open,
	def_exceeded_allowed_outstanding_pte_req_queue_size,
	def_linear,
	def_underlay444,
	def_underlay422,
	def_underlay420_chroma,
	def_underlay420_luma,
	def_any_lines,
	def_auto,
	def_manual,
	def_portrait,
	def_invalid_linear_or_stereo_mode,
	def_invalid_rotation_or_bpp_or_stereo,
	def_vsr_more_than_vtaps,
	def_vsr_more_than_4,
	def_ceil_htaps_div_4_more_or_eq_hsr,
	def_hsr_more_than_htaps,
	def_hsr_more_than_4,
	def_none,
	def_blended,
	def_landscape
};

static void calculate_bandwidth(const struct bw_calcs_input_dceip *dceip,
	const struct bw_calcs_input_vbios *vbios,
	const struct bw_calcs_input_mode_data_internal *mode_data,
	struct bw_results_internal *results)
{
	const struct bw_fixed pixels_per_chunk = int_to_fixed(512);
	const struct bw_fixed max_chunks_non_fbc_mode = int_to_fixed(16);
	const uint32_t high = 2;
	const uint32_t mid = 1;
	const uint32_t low = 0;

	uint32_t i, j, k;
	uint64_t remainder;
	struct bw_fixed yclk[3];
	struct bw_fixed sclk[3];
	bool d0_underlay_enable;
	bool d1_underlay_enable;
	enum bw_defines v_filter_init_mode[maximum_number_of_surfaces];
	enum bw_defines tiling_mode[maximum_number_of_surfaces];
	enum bw_stereo_mode stereo_mode[maximum_number_of_surfaces];
	enum bw_defines surface_type[maximum_number_of_surfaces];
	enum bw_defines voltage;
	enum bw_defines mode_background_color;
	enum bw_defines mode_font_color;
	enum bw_defines mode_pattern;
	enum bw_defines sclk_message;
	enum bw_defines yclk_message;
	enum bw_defines pipe_check;
	enum bw_defines hsr_check;
	enum bw_defines vsr_check;
	enum bw_defines lb_size_check;
	enum bw_defines fbc_check;
	enum bw_defines rotation_check;
	enum bw_defines mode_check;
	uint32_t y_clk_level;
	uint32_t sclk_level;
	yclk[high] = vbios->high_yclk_mhz;
	yclk[mid] = vbios->high_yclk_mhz;
	yclk[low] = vbios->low_yclk_mhz;
	sclk[high] = vbios->high_sclk_mhz;
	sclk[mid] = vbios->mid_sclk_mhz;
	sclk[low] = vbios->low_sclk_mhz;
	if (mode_data->d0_underlay_mode == ul_none) {
		d0_underlay_enable = false;
	} else {
		d0_underlay_enable = true;
	}
	if (mode_data->d1_underlay_mode == ul_none) {
		d1_underlay_enable = false;
	} else {
		d1_underlay_enable = true;
	}
	results->number_of_underlay_surfaces = int_to_fixed(
		d0_underlay_enable + d1_underlay_enable);
	if (mode_data->underlay_surface_type == yuv_420) {
		surface_type[0] = def_underlay420_luma;
		surface_type[2] = def_underlay420_luma;
		results->bytes_per_pixel[0] = int_to_fixed(1);
		results->bytes_per_pixel[2] = int_to_fixed(1);
		surface_type[1] = def_underlay420_chroma;
		surface_type[3] = def_underlay420_chroma;
		results->bytes_per_pixel[1] = int_to_fixed(2);
		results->bytes_per_pixel[3] = int_to_fixed(2);
		results->lb_size_per_component[0] =
			dceip->underlay420_luma_lb_size_per_component;
		results->lb_size_per_component[1] =
			dceip->underlay420_chroma_lb_size_per_component;
		results->lb_size_per_component[2] =
			dceip->underlay420_luma_lb_size_per_component;
		results->lb_size_per_component[3] =
			dceip->underlay420_chroma_lb_size_per_component;
	} else if (mode_data->underlay_surface_type == yuv_422) {
		surface_type[0] = def_underlay422;
		surface_type[2] = def_underlay422;
		results->bytes_per_pixel[0] = int_to_fixed(2);
		results->bytes_per_pixel[2] = int_to_fixed(2);
		results->lb_size_per_component[0] =
			dceip->underlay422_lb_size_per_component;
		results->lb_size_per_component[2] =
			dceip->underlay422_lb_size_per_component;
	} else {
		surface_type[0] = def_underlay444;
		surface_type[2] = def_underlay444;
		results->bytes_per_pixel[0] = int_to_fixed(4);
		results->bytes_per_pixel[2] = int_to_fixed(4);
		results->lb_size_per_component[0] =
			dceip->lb_size_per_component444;
		results->lb_size_per_component[2] =
			dceip->lb_size_per_component444;
	}
	if (d0_underlay_enable) {
		if (mode_data->underlay_surface_type == yuv_420) {
			results->enable[0] = true;
			results->enable[1] = true;
		} else {
			results->enable[0] = true;
			results->enable[1] = false;
		}
	} else {
		results->enable[0] = false;
		results->enable[1] = false;
	}
	if (d1_underlay_enable) {
		if (mode_data->underlay_surface_type == yuv_420) {
			results->enable[2] = true;
			results->enable[3] = true;
		} else {
			results->enable[2] = true;
			results->enable[3] = false;
		}
	} else {
		results->enable[2] = false;
		results->enable[3] = false;
	}

	results->use_alpha[0] = false;
	results->use_alpha[1] = false;
	results->use_alpha[2] = false;
	results->use_alpha[3] = false;
	results->scatter_gather_enable_for_pipe[0] =
		vbios->scatter_gather_enable;
	results->scatter_gather_enable_for_pipe[1] =
		vbios->scatter_gather_enable;
	results->scatter_gather_enable_for_pipe[2] =
		vbios->scatter_gather_enable;
	results->scatter_gather_enable_for_pipe[3] =
		vbios->scatter_gather_enable;
	results->interlace_mode[0] = mode_data->graphics_interlace_mode;
	results->interlace_mode[1] = mode_data->graphics_interlace_mode;
	results->interlace_mode[2] = mode_data->graphics_interlace_mode;
	results->interlace_mode[3] = mode_data->graphics_interlace_mode;
	results->h_total[0] = mode_data->d0_htotal;
	results->h_total[1] = mode_data->d0_htotal;
	results->h_total[2] = mode_data->d1_htotal;
	results->h_total[3] = mode_data->d1_htotal;
	results->pixel_rate[0] = mode_data->d0_pixel_rate;
	results->pixel_rate[1] = mode_data->d0_pixel_rate;
	results->pixel_rate[2] = mode_data->d1_pixel_rate;
	results->pixel_rate[3] = mode_data->d1_pixel_rate;
	results->src_width[0] = mode_data->underlay_src_width;
	results->src_width[1] = mode_data->underlay_src_width;
	results->src_width[2] = mode_data->underlay_src_width;
	results->src_width[3] = mode_data->underlay_src_width;
	results->src_height[0] = mode_data->underlay_src_height;
	results->src_height[1] = mode_data->underlay_src_height;
	results->src_height[2] = mode_data->underlay_src_height;
	results->src_height[3] = mode_data->underlay_src_height;
	results->pitch_in_pixels[0] = mode_data->underlay_pitch_in_pixels;
	results->pitch_in_pixels[1] = mode_data->underlay_pitch_in_pixels;
	results->pitch_in_pixels[2] = mode_data->underlay_pitch_in_pixels;
	results->pitch_in_pixels[3] = mode_data->underlay_pitch_in_pixels;
	results->scale_ratio[0] = mode_data->d0_underlay_scale_ratio;
	results->scale_ratio[1] = mode_data->d0_underlay_scale_ratio;
	results->scale_ratio[2] = mode_data->d1_underlay_scale_ratio;
	results->scale_ratio[3] = mode_data->d1_underlay_scale_ratio;
	results->h_taps[0] = mode_data->underlay_htaps;
	results->h_taps[1] = mode_data->underlay_htaps;
	results->h_taps[2] = mode_data->underlay_htaps;
	results->h_taps[3] = mode_data->underlay_htaps;
	results->v_taps[0] = mode_data->underlay_vtaps;
	results->v_taps[1] = mode_data->underlay_vtaps;
	results->v_taps[2] = mode_data->underlay_vtaps;
	results->v_taps[3] = mode_data->underlay_vtaps;
	results->rotation_angle[0] = mode_data->underlay_rotation_angle;
	results->rotation_angle[1] = mode_data->underlay_rotation_angle;
	results->rotation_angle[2] = mode_data->underlay_rotation_angle;
	results->rotation_angle[3] = mode_data->underlay_rotation_angle;
	if (mode_data->underlay_tiling_mode == linear) {
		tiling_mode[0] = def_linear;
		tiling_mode[1] = def_linear;
		tiling_mode[2] = def_linear;
		tiling_mode[3] = def_linear;
	} else {
		tiling_mode[0] = def_landscape;
		tiling_mode[1] = def_landscape;
		tiling_mode[2] = def_landscape;
		tiling_mode[3] = def_landscape;
	}
	stereo_mode[0] = mode_data->underlay_stereo_mode;
	stereo_mode[1] = mode_data->underlay_stereo_mode;
	stereo_mode[2] = mode_data->underlay_stereo_mode;
	stereo_mode[3] = mode_data->underlay_stereo_mode;
	results->lb_bpc[0] = mode_data->underlay_lb_bpc;
	results->lb_bpc[1] = mode_data->underlay_lb_bpc;
	results->lb_bpc[2] = mode_data->underlay_lb_bpc;
	results->lb_bpc[3] = mode_data->underlay_lb_bpc;
	results->compression_rate[0] = int_to_fixed(1);
	results->compression_rate[1] = int_to_fixed(1);
	results->compression_rate[2] = int_to_fixed(1);
	results->compression_rate[3] = int_to_fixed(1);
	results->access_one_channel_only[0] = false;
	results->access_one_channel_only[1] = false;
	results->access_one_channel_only[2] = false;
	results->access_one_channel_only[3] = false;
	results->cursor_width_pixels[0] = int_to_fixed(0);
	results->cursor_width_pixels[1] = int_to_fixed(0);
	results->cursor_width_pixels[2] = int_to_fixed(0);
	results->cursor_width_pixels[3] = int_to_fixed(0);
	for (i = 4; i <= maximum_number_of_surfaces - 3; i += 1) {
		if (i < mode_data->number_of_displays + 4) {
			if (i == 4 && mode_data->d0_underlay_mode == ul_only) {
				results->enable[i] = false;
				results->use_alpha[i] = false;
			} else if (i == 4
				&& mode_data->d0_underlay_mode == ul_blend) {
				results->enable[i] = true;
				results->use_alpha[i] = true;
			} else if (i == 4) {
				results->enable[i] = true;
				results->use_alpha[i] = false;
			} else if (i == 5
				&& mode_data->d1_underlay_mode == ul_only) {
				results->enable[i] = false;
				results->use_alpha[i] = false;
			} else if (i == 5
				&& mode_data->d1_underlay_mode == ul_blend) {
				results->enable[i] = true;
				results->use_alpha[i] = true;
			} else {
				results->enable[i] = true;
				results->use_alpha[i] = false;
			}
		} else {
			results->enable[i] = false;
			results->use_alpha[i] = false;
		}
		results->scatter_gather_enable_for_pipe[i] =
			vbios->scatter_gather_enable;
		surface_type[i] = def_graphics;
		results->lb_size_per_component[i] =
			dceip->lb_size_per_component444;
		results->bytes_per_pixel[i] =
			mode_data->graphics_bytes_per_pixel;
		results->interlace_mode[i] = mode_data->graphics_interlace_mode;
		results->h_taps[i] = mode_data->graphics_htaps;
		results->v_taps[i] = mode_data->graphics_vtaps;
		results->rotation_angle[i] = mode_data->graphics_rotation_angle;
		if (mode_data->graphics_tiling_mode == linear) {
			tiling_mode[i] = def_linear;
		} else if (equ(mode_data->graphics_rotation_angle,
			int_to_fixed(0))
			|| equ(mode_data->graphics_rotation_angle,
				int_to_fixed(180))) {
			tiling_mode[i] = def_landscape;
		} else {
			tiling_mode[i] = def_portrait;
		}
		results->lb_bpc[i] = mode_data->graphics_lb_bpc;
		if (i == 4) {
			/* todo: check original d0_underlay_mode comparison, possible bug there*/
			if (mode_data->d0_fbc_enable
				&& (dceip->argb_compression_support
					|| mode_data->d0_underlay_mode
						!= ul_blend)) {
				results->compression_rate[i] =
					vbios->average_compression_rate;
				results->access_one_channel_only[i] =
					mode_data->d0_lpt_enable;
			} else {
				results->compression_rate[i] = int_to_fixed(1);
				results->access_one_channel_only[i] = false;
			}
			results->h_total[i] = mode_data->d0_htotal;
			results->pixel_rate[i] = mode_data->d0_pixel_rate;
			results->src_width[i] =
				mode_data->d0_graphics_src_width;
			results->src_height[i] =
				mode_data->d0_graphics_src_height;
			results->pitch_in_pixels[i] =
				mode_data->d0_graphics_src_width;
			results->scale_ratio[i] =
				mode_data->d0_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d0_graphics_stereo_mode;
		} else if (i == 5) {
			results->compression_rate[i] = int_to_fixed(1);
			results->access_one_channel_only[i] = false;
			results->h_total[i] = mode_data->d1_htotal;
			results->pixel_rate[i] = mode_data->d1_pixel_rate;
			results->src_width[i] =
				mode_data->d1_graphics_src_width;
			results->src_height[i] =
				mode_data->d1_graphics_src_height;
			results->pitch_in_pixels[i] =
				mode_data->d1_graphics_src_width;
			results->scale_ratio[i] =
				mode_data->d1_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d1_graphics_stereo_mode;
		} else {
			results->compression_rate[i] = int_to_fixed(1);
			results->access_one_channel_only[i] = false;
			results->h_total[i] = mode_data->d2_htotal;
			results->pixel_rate[i] = mode_data->d2_pixel_rate;
			results->src_width[i] =
				mode_data->d2_graphics_src_width;
			results->src_height[i] =
				mode_data->d2_graphics_src_height;
			results->pitch_in_pixels[i] =
				mode_data->d2_graphics_src_width;
			results->scale_ratio[i] =
				mode_data->d2_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d2_graphics_stereo_mode;
		}
		results->cursor_width_pixels[i] = vbios->cursor_width;
	}
	results->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 2] =
		false;
	results->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 1] =
		false;
	if (mode_data->d1_display_write_back_dwb_enable == true) {
		results->enable[maximum_number_of_surfaces - 2] = true;
		results->enable[maximum_number_of_surfaces - 1] = true;
	} else {
		results->enable[maximum_number_of_surfaces - 2] = false;
		results->enable[maximum_number_of_surfaces - 1] = false;
	}
	surface_type[maximum_number_of_surfaces - 2] =
		def_display_write_back420_luma;
	surface_type[maximum_number_of_surfaces - 1] =
		def_display_write_back420_chroma;
	results->lb_size_per_component[maximum_number_of_surfaces - 2] =
		dceip->underlay420_luma_lb_size_per_component;
	results->lb_size_per_component[maximum_number_of_surfaces - 1] =
		dceip->underlay420_chroma_lb_size_per_component;
	results->bytes_per_pixel[maximum_number_of_surfaces - 2] = int_to_fixed(
		1);
	results->bytes_per_pixel[maximum_number_of_surfaces - 1] = int_to_fixed(
		2);
	results->interlace_mode[maximum_number_of_surfaces - 2] =
		mode_data->graphics_interlace_mode;
	results->interlace_mode[maximum_number_of_surfaces - 1] =
		mode_data->graphics_interlace_mode;
	results->h_taps[maximum_number_of_surfaces - 2] = int_to_fixed(1);
	results->h_taps[maximum_number_of_surfaces - 1] = int_to_fixed(1);
	results->v_taps[maximum_number_of_surfaces - 2] = int_to_fixed(1);
	results->v_taps[maximum_number_of_surfaces - 1] = int_to_fixed(1);
	results->rotation_angle[maximum_number_of_surfaces - 2] = int_to_fixed(
		0);
	results->rotation_angle[maximum_number_of_surfaces - 1] = int_to_fixed(
		0);
	tiling_mode[maximum_number_of_surfaces - 2] = def_linear;
	tiling_mode[maximum_number_of_surfaces - 1] = def_linear;
	results->lb_bpc[maximum_number_of_surfaces - 2] = int_to_fixed(8);
	results->lb_bpc[maximum_number_of_surfaces - 1] = int_to_fixed(8);
	results->compression_rate[maximum_number_of_surfaces - 2] =
		int_to_fixed(1);
	results->compression_rate[maximum_number_of_surfaces - 1] =
		int_to_fixed(1);
	results->access_one_channel_only[maximum_number_of_surfaces - 2] =
		false;
	results->access_one_channel_only[maximum_number_of_surfaces - 1] =
		false;
	results->h_total[maximum_number_of_surfaces - 2] = mode_data->d1_htotal;
	results->h_total[maximum_number_of_surfaces - 1] = mode_data->d1_htotal;
	results->pixel_rate[maximum_number_of_surfaces - 2] =
		mode_data->d1_pixel_rate;
	results->pixel_rate[maximum_number_of_surfaces - 1] =
		mode_data->d1_pixel_rate;
	results->src_width[maximum_number_of_surfaces - 2] =
		mode_data->d1_graphics_src_width;
	results->src_width[maximum_number_of_surfaces - 1] =
		mode_data->d1_graphics_src_width;
	results->src_height[maximum_number_of_surfaces - 2] =
		mode_data->d1_graphics_src_height;
	results->src_height[maximum_number_of_surfaces - 1] =
		mode_data->d1_graphics_src_height;
	results->pitch_in_pixels[maximum_number_of_surfaces - 2] =
		mode_data->d1_graphics_src_width;
	results->pitch_in_pixels[maximum_number_of_surfaces - 1] =
		mode_data->d1_graphics_src_width;
	results->scale_ratio[maximum_number_of_surfaces - 2] = int_to_fixed(1);
	results->scale_ratio[maximum_number_of_surfaces - 1] = int_to_fixed(1);
	stereo_mode[maximum_number_of_surfaces - 2] = mono;
	stereo_mode[maximum_number_of_surfaces - 1] = mono;
	results->cursor_width_pixels[maximum_number_of_surfaces - 2] =
		int_to_fixed(0);
	results->cursor_width_pixels[maximum_number_of_surfaces - 1] =
		int_to_fixed(0);
	results->use_alpha[maximum_number_of_surfaces - 2] = false;
	results->use_alpha[maximum_number_of_surfaces - 1] = false;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (equ(results->scale_ratio[i], int_to_fixed(1))
				&& surface_type[i] == def_graphics
				&& stereo_mode[i] == mono
				&& results->interlace_mode[i] == false) {
				results->h_taps[i] = int_to_fixed(1);
				results->v_taps[i] = int_to_fixed(1);
			}
			if (surface_type[i] == def_display_write_back420_chroma
				|| surface_type[i] == def_underlay420_chroma) {
				results->pitch_in_pixels_after_surface_type[i] =
					bw_div(results->pitch_in_pixels[i],
						int_to_fixed(2));
				results->src_width_after_surface_type = bw_div(
					results->src_width[i], int_to_fixed(2));
				results->src_height_after_surface_type = bw_div(
					results->src_height[i],
					int_to_fixed(2));
				results->hsr_after_surface_type = bw_div(
					results->scale_ratio[i],
					int_to_fixed(2));
				results->vsr_after_surface_type = bw_div(
					results->scale_ratio[i],
					int_to_fixed(2));
			} else {
				results->pitch_in_pixels_after_surface_type[i] =
					results->pitch_in_pixels[i];
				results->src_width_after_surface_type =
					results->src_width[i];
				results->src_height_after_surface_type =
					results->src_height[i];
				results->hsr_after_surface_type =
					results->scale_ratio[i];
				results->vsr_after_surface_type =
					results->scale_ratio[i];
			}
			if ((equ(results->rotation_angle[i], int_to_fixed(90))
				|| equ(results->rotation_angle[i],
					int_to_fixed(270)))
				&& surface_type[i] != def_graphics) {
				results->src_width_after_rotation =
					results->src_height_after_surface_type;
				results->src_height_after_rotation =
					results->src_width_after_surface_type;
				results->hsr_after_rotation =
					results->vsr_after_surface_type;
				results->vsr_after_rotation =
					results->hsr_after_surface_type;
			} else {
				results->src_width_after_rotation =
					results->src_width_after_surface_type;
				results->src_height_after_rotation =
					results->src_height_after_surface_type;
				results->hsr_after_rotation =
					results->hsr_after_surface_type;
				results->vsr_after_rotation =
					results->vsr_after_surface_type;
			}
			if (stereo_mode[i] == top_bottom) {
				results->source_width_pixels[i] =
					results->src_width_after_rotation;
				results->source_height_pixels = mul(
					int_to_fixed(2),
					results->src_height_after_rotation);
				results->hsr_after_stereo =
					results->hsr_after_rotation;
				results->vsr_after_stereo = mul(
					results->vsr_after_rotation,
					int_to_fixed(1)); //todo: confirm correctness
			} else if (stereo_mode[i] == side_by_side) {
				results->source_width_pixels[i] = mul(
					int_to_fixed(2),
					results->src_width_after_rotation);
				results->source_height_pixels =
					results->src_height_after_rotation;
				results->hsr_after_stereo = mul(
					results->hsr_after_rotation,
					int_to_fixed(1)); //todo: confirm correctness
				results->vsr_after_stereo =
					results->vsr_after_rotation;
			} else {
				results->source_width_pixels[i] =
					results->src_width_after_rotation;
				results->source_height_pixels =
					results->src_height_after_rotation;
				results->hsr_after_stereo =
					results->hsr_after_rotation;
				results->vsr_after_stereo =
					results->vsr_after_rotation;
			}
			results->hsr[i] = results->hsr_after_stereo;
			if (results->interlace_mode[i]) {
				results->vsr[i] = mul(results->vsr_after_stereo,
					int_to_fixed(2));
			} else {
				results->vsr[i] = results->vsr_after_stereo;
			}
			if (mode_data->panning_and_bezel_adjustment != none) {
				results->source_width_rounded_up_to_chunks[i] =
					add(
						bw_floor(
							sub(
								results->source_width_pixels[i],
								int_to_fixed(
									1)),
							int_to_fixed(128)),
						int_to_fixed(256));
			} else {
				results->source_width_rounded_up_to_chunks[i] =
					bw_ceil(results->source_width_pixels[i],
						int_to_fixed(128));
			}
			results->source_height_rounded_up_to_chunks[i] =
				results->source_height_pixels;
		}
	}
	if (geq(dceip->number_of_graphics_pipes,
		int_to_fixed(mode_data->number_of_displays))
		&& geq(dceip->number_of_underlay_pipes,
			results->number_of_underlay_surfaces)
		&& !(dceip->display_write_back_supported == false
			&& mode_data->d1_display_write_back_dwb_enable == true)) {
		pipe_check = def_ok;
	} else {
		pipe_check = def_notok;
	}
	hsr_check = def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (neq(results->hsr[i], int_to_fixed(1))) {
				if (gtn(results->hsr[i], int_to_fixed(4))) {
					hsr_check = def_hsr_more_than_4;
				} else {
					if (gtn(results->hsr[i],
						results->h_taps[i])) {
						hsr_check =
							def_hsr_more_than_htaps;
					} else {
						if (dceip->pre_downscaler_enabled
							== true
							&& gtn(results->hsr[i],
								int_to_fixed(1))
							&& leq(results->hsr[i],
								bw_ceil(
									bw_div(
										results->h_taps[i],
										int_to_fixed(
											4)),
									int_to_fixed(
										1)))) {
							hsr_check =
								def_ceil_htaps_div_4_more_or_eq_hsr;
						}
					}
				}
			}
		}
	}
	vsr_check = def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (neq(results->vsr[i], int_to_fixed(1))) {
				if (gtn(results->vsr[i], int_to_fixed(4))) {
					vsr_check = def_vsr_more_than_4;
				} else {
					if (gtn(results->vsr[i],
						results->v_taps[i])) {
						vsr_check =
							def_vsr_more_than_vtaps;
					}
				}
			}
		}
	}
	lb_size_check = def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if ((dceip->pre_downscaler_enabled
				&& gtn(results->hsr[i], int_to_fixed(1)))) {
				results->source_width_in_lb = bw_div(
					results->source_width_pixels[i],
					results->hsr[i]);
			} else {
				results->source_width_in_lb =
					results->source_width_pixels[i];
			}
			if (equ(results->lb_bpc[i], int_to_fixed(8))) {
				results->lb_line_pitch =
					bw_ceil(
						mul(frc_to_fixed(24011, 3000),
							bw_ceil(
								results->source_width_in_lb,
								int_to_fixed(
									8))),
						int_to_fixed(48));
			} else if (equ(results->lb_bpc[i], int_to_fixed(10))) {
				results->lb_line_pitch =
					bw_ceil(
						mul(frc_to_fixed(30023, 3000),
							bw_ceil(
								results->source_width_in_lb,
								int_to_fixed(
									8))),
						int_to_fixed(48));
			} else
			// case else
			{
				results->lb_line_pitch = bw_ceil(
					mul(results->source_width_in_lb,
						results->lb_bpc[i]),
					int_to_fixed(48));
			}
			results->lb_partitions[i] = bw_floor(
				bw_div(results->lb_size_per_component[i],
					results->lb_line_pitch),
				int_to_fixed(1));
			if ((surface_type[i] != def_graphics
				|| dceip->graphics_lb_nodownscaling_multi_line_prefetching
					== true)) {
				results->lb_partitions_max[i] = int_to_fixed(
					10);
			} else {
				results->lb_partitions_max[i] = int_to_fixed(7);
			}
			results->lb_partitions[i] = bw_min(
				results->lb_partitions_max[i],
				results->lb_partitions[i]);
			if (gtn(add(results->v_taps[i], int_to_fixed(1)),
				results->lb_partitions[i])) {
				lb_size_check = def_notok;
			}
		}
	}
	if (mode_data->d0_fbc_enable
		&& (equ(mode_data->graphics_rotation_angle, int_to_fixed(90))
			|| equ(mode_data->graphics_rotation_angle,
				int_to_fixed(270))
			|| mode_data->d0_graphics_stereo_mode != mono
			|| neq(mode_data->graphics_bytes_per_pixel,
				int_to_fixed(4)))) {
		fbc_check = def_invalid_rotation_or_bpp_or_stereo;
	} else {
		fbc_check = def_ok;
	}
	rotation_check = def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if ((equ(results->rotation_angle[i], int_to_fixed(90))
				|| equ(results->rotation_angle[i],
					int_to_fixed(270)))
				&& (tiling_mode[i] == def_linear
					|| stereo_mode[i] != mono)) {
				rotation_check =
					def_invalid_linear_or_stereo_mode;
			}
		}
	}
	if (pipe_check == def_ok && hsr_check == def_ok && vsr_check == def_ok
		&& lb_size_check == def_ok && fbc_check == def_ok
		&& rotation_check == def_ok) {
		mode_check = def_ok;
	} else {
		mode_check = def_notok;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if ((equ(results->rotation_angle[i], int_to_fixed(90))
				|| equ(results->rotation_angle[i],
					int_to_fixed(270)))) {
				if ((tiling_mode[i] == def_portrait)) {
					results->orthogonal_rotation[i] = false;
				} else {
					results->orthogonal_rotation[i] = true;
				}
			} else {
				if ((tiling_mode[i] == def_portrait)) {
					results->orthogonal_rotation[i] = true;
				} else {
					results->orthogonal_rotation[i] = false;
				}
			}
			if (equ(results->rotation_angle[i], int_to_fixed(90))
				|| equ(results->rotation_angle[i],
					int_to_fixed(270))) {
				results->underlay_maximum_source_efficient_for_tiling =
					dceip->underlay_maximum_height_efficient_for_tiling;
			} else {
				results->underlay_maximum_source_efficient_for_tiling =
					dceip->underlay_maximum_width_efficient_for_tiling;
			}
			if (equ(dceip->de_tiling_buffer, int_to_fixed(0))) {
				if (surface_type[i]
					== def_display_write_back420_luma
					|| surface_type[i]
						== def_display_write_back420_chroma) {
					results->bytes_per_request[i] =
						int_to_fixed(64);
					results->useful_bytes_per_request[i] =
						int_to_fixed(64);
					results->lines_interleaved_in_mem_access[i] =
						int_to_fixed(1);
					results->latency_hiding_lines[i] =
						int_to_fixed(1);
				} else if (tiling_mode[i] == def_linear) {
					results->bytes_per_request[i] =
						int_to_fixed(64);
					results->useful_bytes_per_request[i] =
						int_to_fixed(64);
					results->lines_interleaved_in_mem_access[i] =
						int_to_fixed(2);
					results->latency_hiding_lines[i] =
						int_to_fixed(2);
				} else {
					if (surface_type[i] == def_graphics
						|| (gtn(
							results->source_width_rounded_up_to_chunks[i],
							bw_ceil(
								results->underlay_maximum_source_efficient_for_tiling,
								int_to_fixed(
									256))))) {
						if (equ(
							results->bytes_per_pixel[i],
							int_to_fixed(8))) {
							results->lines_interleaved_in_mem_access[i] =
								int_to_fixed(2);
							results->latency_hiding_lines[i] =
								int_to_fixed(2);
							if (results->orthogonal_rotation[i]) {
								results->bytes_per_request[i] =
									int_to_fixed(
										32);
								results->useful_bytes_per_request[i] =
									int_to_fixed(
										32);
							} else {
								results->bytes_per_request[i] =
									int_to_fixed(
										64);
								results->useful_bytes_per_request[i] =
									int_to_fixed(
										64);
							}
						} else if (equ(
							results->bytes_per_pixel[i],
							int_to_fixed(4))) {
							if (results->orthogonal_rotation[i]) {
								results->lines_interleaved_in_mem_access[i] =
									int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									int_to_fixed(
										2);
								results->bytes_per_request[i] =
									int_to_fixed(
										32);
								results->useful_bytes_per_request[i] =
									int_to_fixed(
										16);
							} else {
								results->lines_interleaved_in_mem_access[i] =
									int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									int_to_fixed(
										2);
								results->bytes_per_request[i] =
									int_to_fixed(
										64);
								results->useful_bytes_per_request[i] =
									int_to_fixed(
										64);
							}
						} else if (equ(
							results->bytes_per_pixel[i],
							int_to_fixed(2))) {
							results->lines_interleaved_in_mem_access[i] =
								int_to_fixed(2);
							results->latency_hiding_lines[i] =
								int_to_fixed(2);
							results->bytes_per_request[i] =
								int_to_fixed(
									32);
							results->useful_bytes_per_request[i] =
								int_to_fixed(
									32);
						} else {
							results->lines_interleaved_in_mem_access[i] =
								int_to_fixed(2);
							results->latency_hiding_lines[i] =
								int_to_fixed(2);
							results->bytes_per_request[i] =
								int_to_fixed(
									32);
							results->useful_bytes_per_request[i] =
								int_to_fixed(
									16);
						}
					} else {
						results->bytes_per_request[i] =
							int_to_fixed(64);
						results->useful_bytes_per_request[i] =
							int_to_fixed(64);
						if (results->orthogonal_rotation[i]) {
							results->lines_interleaved_in_mem_access[i] =
								int_to_fixed(8);
							results->latency_hiding_lines[i] =
								int_to_fixed(4);
						} else {
							if (equ(
								results->bytes_per_pixel[i],
								int_to_fixed(
									4))) {
								results->lines_interleaved_in_mem_access[i] =
									int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									int_to_fixed(
										2);
							} else if (equ(
								results->bytes_per_pixel[i],
								int_to_fixed(
									2))) {
								results->lines_interleaved_in_mem_access[i] =
									int_to_fixed(
										4);
								results->latency_hiding_lines[i] =
									int_to_fixed(
										4);
							} else {
								results->lines_interleaved_in_mem_access[i] =
									int_to_fixed(
										8);
								results->latency_hiding_lines[i] =
									int_to_fixed(
										4);
							}
						}
					}
				}
			} else {
				results->bytes_per_request[i] = int_to_fixed(
					256);
				results->useful_bytes_per_request[i] =
					int_to_fixed(256);
				results->lines_interleaved_in_mem_access[i] =
					int_to_fixed(4);
				results->latency_hiding_lines[i] = int_to_fixed(
					4);
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->v_filter_init[i] =
				bw_floor(
					bw_div(
						(add(
							add(
								add(
									results->vsr[i],
									results->v_taps[i]),
								mul(
									mul(
										frc_to_fixed(
											1,
											2),
										results->vsr[i]),
									int_to_fixed(
										results->interlace_mode[i]))),
							int_to_fixed(1))),
						int_to_fixed(2)),
					int_to_fixed(1));
			if (mode_data->panning_and_bezel_adjustment
				== any_lines) {
				results->v_filter_init[i] = add(
					results->v_filter_init[i],
					int_to_fixed(1));
			}
			if (stereo_mode[i] == top_bottom) {
				v_filter_init_mode[i] = def_manual;
				results->v_filter_init[i] = bw_min(
					results->v_filter_init[i],
					int_to_fixed(4));
			} else {
				v_filter_init_mode[i] = def_auto;
			}
			if (stereo_mode[i] == top_bottom) {
				results->num_lines_at_frame_start =
					int_to_fixed(1);
			} else {
				results->num_lines_at_frame_start =
					int_to_fixed(3);
			}
			if ((gtn(results->vsr[i], int_to_fixed(1))
				&& surface_type[i] == def_graphics)
				|| mode_data->panning_and_bezel_adjustment
					== any_lines) {
				results->line_buffer_prefetch[i] = int_to_fixed(
					0);
			} else if ((((dceip->underlay_downscale_prefetch_enabled
				== true && surface_type[i] != def_graphics)
				|| surface_type[i] == def_graphics)
				&& (gtn(results->lb_partitions[i],
					add(results->v_taps[i],
						bw_ceil(results->vsr[i],
							int_to_fixed(1))))))) {
				results->line_buffer_prefetch[i] = int_to_fixed(
					1);
			} else {
				results->line_buffer_prefetch[i] = int_to_fixed(
					0);
			}
			results->lb_lines_in_per_line_out_in_beginning_of_frame[i] =
				bw_div(
					bw_ceil(results->v_filter_init[i],
						dceip->lines_interleaved_into_lb),
					results->num_lines_at_frame_start);
			if (equ(results->line_buffer_prefetch[i],
				int_to_fixed(1))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_max(int_to_fixed(1),
						results->vsr[i]);
			} else if (leq(results->vsr[i], int_to_fixed(1))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					int_to_fixed(1);
			} else if (leq(results->vsr[i],
				bw_div(int_to_fixed(4), int_to_fixed(3)))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_div(int_to_fixed(4), int_to_fixed(3));
			} else if (leq(results->vsr[i],
				bw_div(int_to_fixed(6), int_to_fixed(4)))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_div(int_to_fixed(6), int_to_fixed(4));
			} else if (leq(results->vsr[i], int_to_fixed(2))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					int_to_fixed(2);
			} else if (leq(results->vsr[i], int_to_fixed(3))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					int_to_fixed(3);
			} else {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					int_to_fixed(4);
			}
			if (equ(results->line_buffer_prefetch[i],
				int_to_fixed(1))
				|| equ(
					results->lb_lines_in_per_line_out_in_middle_of_frame[i],
					int_to_fixed(2))
				|| equ(
					results->lb_lines_in_per_line_out_in_middle_of_frame[i],
					int_to_fixed(4))) {
				results->horizontal_blank_and_chunk_granularity_factor[i] =
					int_to_fixed(1);
			} else {
				results->horizontal_blank_and_chunk_granularity_factor[i] =
					bw_div(results->h_total[i],
						(bw_div(
							(add(
								results->h_total[i],
								bw_div(
									(sub(
										results->source_width_pixels[i],
										dceip->chunk_width)),
									results->hsr[i]))),
							int_to_fixed(2))));
			}
			results->request_bandwidth[i] =
				bw_div(
					mul(
						bw_div(
							mul(
								bw_div(
									mul(
										bw_max(
											results->lb_lines_in_per_line_out_in_beginning_of_frame[i],
											results->lb_lines_in_per_line_out_in_middle_of_frame[i]),
										results->source_width_rounded_up_to_chunks[i]),
									(bw_div(
										results->h_total[i],
										results->pixel_rate[i]))),
								results->bytes_per_pixel[i]),
							results->useful_bytes_per_request[i]),
						results->lines_interleaved_in_mem_access[i]),
					results->latency_hiding_lines[i]);
			results->display_bandwidth[i] = mul(
				results->request_bandwidth[i],
				results->bytes_per_request[i]);
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] == def_display_write_back420_luma) {
				results->data_buffer_size[i] =
					dceip->display_write_back420_luma_mcifwr_buffer_size;
			} else if (surface_type[i]
				== def_display_write_back420_chroma) {
				results->data_buffer_size[i] =
					dceip->display_write_back420_chroma_mcifwr_buffer_size;
			} else if (surface_type[i] == def_underlay420_luma) {
				results->data_buffer_size[i] =
					dceip->underlay_luma_dmif_size;
			} else if (surface_type[i] == def_underlay420_chroma) {
				results->data_buffer_size[i] = bw_div(
					dceip->underlay_chroma_dmif_size,
					int_to_fixed(2));
			} else if (surface_type[i] == def_underlay422
				|| surface_type[i] == def_underlay444) {
				if (results->orthogonal_rotation[i] == false) {
					results->data_buffer_size[i] =
						dceip->underlay_luma_dmif_size;
				} else {
					results->data_buffer_size[i] =
						add(
							dceip->underlay_luma_dmif_size,
							dceip->underlay_chroma_dmif_size);
				}
			} else {
				if (mode_data->number_of_displays == 1
					&& equ(dceip->de_tiling_buffer,
						int_to_fixed(0))) {
					if (mode_data->d0_fbc_enable) {
						results->data_buffer_size[i] =
							mul(
								dceip->max_dmif_buffer_allocated,
								dceip->graphics_dmif_size);
					} else {
						results->data_buffer_size[i] =
							mul(
								mul(
									max_chunks_non_fbc_mode,
									pixels_per_chunk),
								results->bytes_per_pixel[i]);
					}
				} else {
					results->data_buffer_size[i] =
						dceip->graphics_dmif_size;
				}
			}
			if (surface_type[i] == def_display_write_back420_luma
				|| surface_type[i]
					== def_display_write_back420_chroma) {
				results->memory_chunk_size_in_bytes[i] =
					int_to_fixed(1024);
				results->pipe_chunk_size_in_bytes[i] =
					int_to_fixed(1024);
			} else {
				results->memory_chunk_size_in_bytes[i] =
					mul(
						mul(dceip->chunk_width,
							results->lines_interleaved_in_mem_access[i]),
						results->bytes_per_pixel[i]);
				results->pipe_chunk_size_in_bytes[i] =
					mul(
						mul(dceip->chunk_width,
							dceip->lines_interleaved_into_lb),
						results->bytes_per_pixel[i]);
			}
		}
	}
	results->min_dmif_size_in_time = int_to_fixed(9999);
	results->min_mcifwr_size_in_time = int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				if (ltn(
					bw_div(
						bw_div(
							mul(
								results->data_buffer_size[i],
								results->bytes_per_request[i]),
							results->useful_bytes_per_request[i]),
						results->display_bandwidth[i]),
					results->min_dmif_size_in_time)) {
					results->min_dmif_size_in_time =
						bw_div(
							bw_div(
								mul(
									results->data_buffer_size[i],
									results->bytes_per_request[i]),
								results->useful_bytes_per_request[i]),
							results->display_bandwidth[i]);
				}
			} else {
				if (ltn(
					bw_div(
						bw_div(
							mul(
								results->data_buffer_size[i],
								results->bytes_per_request[i]),
							results->useful_bytes_per_request[i]),
						results->display_bandwidth[i]),
					results->min_mcifwr_size_in_time)) {
					results->min_mcifwr_size_in_time =
						bw_div(
							bw_div(
								mul(
									results->data_buffer_size[i],
									results->bytes_per_request[i]),
								results->useful_bytes_per_request[i]),
							results->display_bandwidth[i]);
				}
			}
		}
	}
	results->total_requests_for_dmif_size = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]
			&& surface_type[i] != def_display_write_back420_luma
			&& surface_type[i]
				!= def_display_write_back420_chroma) {
			results->total_requests_for_dmif_size = add(
				results->total_requests_for_dmif_size,
				bw_div(results->data_buffer_size[i],
					results->useful_bytes_per_request[i]));
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma
				&& dceip->limit_excessive_outstanding_dmif_requests
				&& (mode_data->number_of_displays > 1
					|| gtn(
						results->total_requests_for_dmif_size,
						dceip->dmif_request_buffer_size))) {
				results->adjusted_data_buffer_size[i] =
					bw_min(results->data_buffer_size[i],
						bw_ceil(
							mul(
								results->min_dmif_size_in_time,
								results->display_bandwidth[i]),
							results->memory_chunk_size_in_bytes[i]));
			} else {
				results->adjusted_data_buffer_size[i] =
					results->data_buffer_size[i];
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if ((mode_data->number_of_displays == 1
				&& equ(results->number_of_underlay_surfaces,
					int_to_fixed(0)))) {
				results->outstanding_chunk_request_limit[i] =
					int_to_fixed(255);
			} else {
				results->outstanding_chunk_request_limit[i] =
					bw_ceil(
						bw_div(
							results->adjusted_data_buffer_size[i],
							results->pipe_chunk_size_in_bytes[i]),
						int_to_fixed(1));
			}
		}
	}
	if (mode_data->number_of_displays > 1
		|| (neq(mode_data->graphics_rotation_angle, int_to_fixed(0))
			&& neq(mode_data->graphics_rotation_angle,
				int_to_fixed(180)))) {
		results->peak_pte_request_to_eviction_ratio_limiting =
			dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display;
	} else {
		results->peak_pte_request_to_eviction_ratio_limiting =
			dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]
			&& results->scatter_gather_enable_for_pipe[i] == true) {
			if (tiling_mode[i] == def_linear) {
				results->useful_pte_per_pte_request =
					int_to_fixed(8);
				results->scatter_gather_page_width[i] = bw_div(
					int_to_fixed(4096),
					results->bytes_per_pixel[i]);
				results->scatter_gather_page_height[i] =
					int_to_fixed(1);
				results->scatter_gather_pte_request_rows =
					int_to_fixed(1);
				results->scatter_gather_row_height =
					dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode;
			} else if (equ(results->rotation_angle[i],
				int_to_fixed(0))
				|| equ(results->rotation_angle[i],
					int_to_fixed(180))) {
				results->useful_pte_per_pte_request =
					int_to_fixed(8);
				if (equ(results->bytes_per_pixel[i],
					int_to_fixed(4))) {
					results->scatter_gather_page_width[i] =
						int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						int_to_fixed(32);
				} else if (equ(results->bytes_per_pixel[i],
					int_to_fixed(2))) {
					results->scatter_gather_page_width[i] =
						int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						int_to_fixed(32);
				} else {
					results->scatter_gather_page_width[i] =
						int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						int_to_fixed(64);
				}
				results->scatter_gather_pte_request_rows =
					dceip->scatter_gather_pte_request_rows_in_tiling_mode;
				results->scatter_gather_row_height =
					results->scatter_gather_page_height[i];
			} else {
				results->useful_pte_per_pte_request =
					int_to_fixed(1);
				if (equ(results->bytes_per_pixel[i],
					int_to_fixed(4))) {
					results->scatter_gather_page_width[i] =
						int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						int_to_fixed(32);
				} else if (equ(results->bytes_per_pixel[i],
					int_to_fixed(2))) {
					results->scatter_gather_page_width[i] =
						int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						int_to_fixed(64);
				} else
				// case else
				{
					results->scatter_gather_page_width[i] =
						int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						int_to_fixed(64);
				}
				results->scatter_gather_pte_request_rows =
					dceip->scatter_gather_pte_request_rows_in_tiling_mode;
				results->scatter_gather_row_height =
					results->scatter_gather_page_height[i];
			}
			results->pte_request_per_chunk[i] = bw_div(
				bw_div(dceip->chunk_width,
					results->scatter_gather_page_width[i]),
				results->useful_pte_per_pte_request);
			results->scatter_gather_pte_requests_in_row[i] =
				bw_div(
					mul(results->scatter_gather_row_height,
						bw_ceil(
							mul(
								bw_div(
									results->source_width_rounded_up_to_chunks[i],
									dceip->chunk_width),
								results->pte_request_per_chunk[i]),
							int_to_fixed(1))),
					results->scatter_gather_page_height[i]);
			results->scatter_gather_pte_requests_in_vblank = mul(
				results->scatter_gather_pte_request_rows,
				results->scatter_gather_pte_requests_in_row[i]);
			if (equ(
				results->peak_pte_request_to_eviction_ratio_limiting,
				int_to_fixed(0))) {
				results->scatter_gather_pte_request_limit[i] =
					results->scatter_gather_pte_requests_in_vblank;
			} else {
				results->scatter_gather_pte_request_limit[i] =
					bw_max(
						dceip->minimum_outstanding_pte_request_limit,
						bw_min(
							results->scatter_gather_pte_requests_in_vblank,
							bw_ceil(
								mul(
									mul(
										bw_div(
											bw_ceil(
												results->adjusted_data_buffer_size[i],
												results->memory_chunk_size_in_bytes[i]),
											results->memory_chunk_size_in_bytes[i]),
										results->pte_request_per_chunk[i]),
									results->peak_pte_request_to_eviction_ratio_limiting),
								int_to_fixed(
									1))));
			}
		}
	}
	results->inefficient_linear_pitch_in_bytes = mul(
		mul(vbios->number_of_dram_banks,
			vbios->number_of_dram_channels), int_to_fixed(256));
	if (mode_data->underlay_surface_type == yuv_420) {
		results->inefficient_underlay_pitch_in_pixels =
			results->inefficient_linear_pitch_in_bytes;
	} else if (mode_data->underlay_surface_type == yuv_422) {
		results->inefficient_underlay_pitch_in_pixels = bw_div(
			results->inefficient_linear_pitch_in_bytes,
			int_to_fixed(2));
	} else {
		results->inefficient_underlay_pitch_in_pixels = bw_div(
			results->inefficient_linear_pitch_in_bytes,
			int_to_fixed(4));
	}

	div64_u64_rem((uint64_t)mode_data->underlay_pitch_in_pixels.value,
			(uint64_t)results->inefficient_underlay_pitch_in_pixels.value,
			&remainder);

	if (mode_data->underlay_tiling_mode == linear
		&& vbios->scatter_gather_enable == true
		&& remainder == 0) {
		results->minimum_underlay_pitch_padding_recommended_for_efficiency =
			int_to_fixed(256);
	} else {
		results->minimum_underlay_pitch_padding_recommended_for_efficiency =
			int_to_fixed(0);
	}

	results->cursor_total_data = int_to_fixed(0);
	results->cursor_total_request_groups = int_to_fixed(0);
	results->scatter_gather_total_pte_requests = int_to_fixed(0);
	results->scatter_gather_total_pte_request_groups = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->cursor_total_data = add(
				results->cursor_total_data,
				mul(results->cursor_width_pixels[i],
					int_to_fixed(8)));
			results->cursor_total_request_groups = add(
				results->cursor_total_request_groups,
				bw_ceil(
					bw_div(results->cursor_width_pixels[i],
						dceip->cursor_chunk_width),
					int_to_fixed(1)));
			if (results->scatter_gather_enable_for_pipe[i]) {
				results->scatter_gather_total_pte_requests =
					add(
						results->scatter_gather_total_pte_requests,
						results->scatter_gather_pte_request_limit[i]);
				results->scatter_gather_total_pte_request_groups =
					add(
						results->scatter_gather_total_pte_request_groups,
						bw_ceil(
							bw_div(
								results->scatter_gather_pte_request_limit[i],
								bw_ceil(
									results->pte_request_per_chunk[i],
									int_to_fixed(
										1))),
							int_to_fixed(1)));
			}
		}
	}
	results->tile_width_in_pixels = int_to_fixed(8);
	results->dmif_total_number_of_data_request_page_close_open =
		int_to_fixed(0);
	results->mcifwr_total_number_of_data_request_page_close_open =
		int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			uint64_t arg1 = (uint64_t)mul(results->pitch_in_pixels_after_surface_type[i],
						results->bytes_per_pixel[i]).value;

			div64_u64_rem(arg1,
					(uint64_t)results->inefficient_linear_pitch_in_bytes.value,
					&remainder);

			if (results->scatter_gather_enable_for_pipe[i] == true
				&& tiling_mode[i] != def_linear) {
				results->bytes_per_page_close_open =
					mul(
						results->lines_interleaved_in_mem_access[i],
						bw_max(
							mul(
								mul(
									mul(
										results->bytes_per_pixel[i],
										results->tile_width_in_pixels),
									vbios->number_of_dram_banks),
								vbios->number_of_dram_channels),
							mul(
								results->bytes_per_pixel[i],
								results->scatter_gather_page_width[i])));
			} else if (results->scatter_gather_enable_for_pipe[i]
				== true && tiling_mode[i] == def_linear
				&& remainder == 0) {
				results->bytes_per_page_close_open =
					dceip->linear_mode_line_request_alternation_slice;
			} else {
				results->bytes_per_page_close_open =
					results->memory_chunk_size_in_bytes[i];
			}
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				results->dmif_total_number_of_data_request_page_close_open =
					add(
						results->dmif_total_number_of_data_request_page_close_open,
						bw_div(
							bw_ceil(
								results->adjusted_data_buffer_size[i],
								results->memory_chunk_size_in_bytes[i]),
							results->bytes_per_page_close_open));
			} else {
				results->mcifwr_total_number_of_data_request_page_close_open =
					add(
						results->mcifwr_total_number_of_data_request_page_close_open,
						bw_div(
							bw_ceil(
								results->adjusted_data_buffer_size[i],
								results->memory_chunk_size_in_bytes[i]),
							results->bytes_per_page_close_open));
			}
		}
	}
	results->dmif_total_page_close_open_time =
		bw_div(
			mul(
				(add(
					add(
						results->dmif_total_number_of_data_request_page_close_open,
						results->scatter_gather_total_pte_request_groups),
					results->cursor_total_request_groups)),
				vbios->trc), int_to_fixed(1000));
	results->mcifwr_total_page_close_open_time =
		bw_div(
			mul(
				results->mcifwr_total_number_of_data_request_page_close_open,
				vbios->trc), int_to_fixed(1000));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->adjusted_data_buffer_size_in_memory[i] = bw_div(
				mul(results->adjusted_data_buffer_size[i],
					results->bytes_per_request[i]),
				results->useful_bytes_per_request[i]);
		}
	}
	results->total_requests_for_adjusted_dmif_size = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				results->total_requests_for_adjusted_dmif_size =
					add(
						results->total_requests_for_adjusted_dmif_size,
						bw_div(
							results->adjusted_data_buffer_size[i],
							results->useful_bytes_per_request[i]));
			}
		}
	}
	if (equ(dceip->dcfclk_request_generation, int_to_fixed(1))) {
		results->total_dmifmc_urgent_trips = int_to_fixed(1);
	} else {
		results->total_dmifmc_urgent_trips =
			bw_ceil(
				bw_div(
					results->total_requests_for_adjusted_dmif_size,
					(add(dceip->dmif_request_buffer_size,
						mul(
							vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel,
							vbios->number_of_dram_channels)))),
				int_to_fixed(1));
	}
	results->total_dmifmc_urgent_latency = mul(vbios->dmifmc_urgent_latency,
		results->total_dmifmc_urgent_trips);
	results->total_display_reads_required_data = int_to_fixed(0);
	results->total_display_reads_required_dram_access_data = int_to_fixed(
		0);
	results->total_display_writes_required_data = int_to_fixed(0);
	results->total_display_writes_required_dram_access_data = int_to_fixed(
		0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				results->display_reads_required_data =
					results->adjusted_data_buffer_size_in_memory[i];
				results->display_reads_required_dram_access_data =
					mul(
						results->adjusted_data_buffer_size_in_memory[i],
						bw_ceil(
							bw_div(
								vbios->dram_channel_width_in_bits,
								results->bytes_per_request[i]),
							int_to_fixed(1)));
				if (results->access_one_channel_only[i]) {
					results->display_reads_required_dram_access_data =
						mul(
							results->display_reads_required_dram_access_data,
							vbios->number_of_dram_channels);
				}
				results->total_display_reads_required_data =
					add(
						results->total_display_reads_required_data,
						results->display_reads_required_data);
				results->total_display_reads_required_dram_access_data =
					add(
						results->total_display_reads_required_dram_access_data,
						results->display_reads_required_dram_access_data);
			} else {
				results->total_display_writes_required_data =
					add(
						results->total_display_writes_required_data,
						results->adjusted_data_buffer_size_in_memory[i]);
				results->total_display_writes_required_dram_access_data =
					add(
						results->total_display_writes_required_dram_access_data,
						mul(
							results->adjusted_data_buffer_size_in_memory[i],
							bw_ceil(
								bw_div(
									vbios->dram_channel_width_in_bits,
									results->bytes_per_request[i]),
								int_to_fixed(
									1))));
			}
		}
	}
	results->total_display_reads_required_data = add(
		add(results->total_display_reads_required_data,
			results->cursor_total_data),
		mul(results->scatter_gather_total_pte_requests,
			int_to_fixed(64)));
	results->total_display_reads_required_dram_access_data = add(
		add(results->total_display_reads_required_dram_access_data,
			results->cursor_total_data),
		mul(results->scatter_gather_total_pte_requests,
			int_to_fixed(64)));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (gtn(results->v_filter_init[i], int_to_fixed(4))) {
				results->src_pixels_for_first_output_pixel[i] =
					mul(
						results->source_width_rounded_up_to_chunks[i],
						int_to_fixed(4));
			} else {
				if (gtn(results->v_filter_init[i],
					int_to_fixed(2))) {
					results->src_pixels_for_first_output_pixel[i] =
						int_to_fixed(512);
				} else {
					results->src_pixels_for_first_output_pixel[i] =
						int_to_fixed(0);
				}
			}
			results->src_data_for_first_output_pixel[i] =
				bw_div(
					mul(
						mul(
							results->src_pixels_for_first_output_pixel[i],
							results->bytes_per_pixel[i]),
						results->bytes_per_request[i]),
					results->useful_bytes_per_request[i]);
			results->src_pixels_for_last_output_pixel[i] =
				mul(
					results->source_width_rounded_up_to_chunks[i],
					bw_max(
						bw_ceil(
							results->v_filter_init[i],
							dceip->lines_interleaved_into_lb),
						mul(
							results->horizontal_blank_and_chunk_granularity_factor[i],
							bw_ceil(results->vsr[i],
								dceip->lines_interleaved_into_lb))));
			results->src_data_for_last_output_pixel[i] =
				bw_div(
					mul(
						mul(
							mul(
								results->source_width_rounded_up_to_chunks[i],
								bw_max(
									bw_ceil(
										results->v_filter_init[i],
										dceip->lines_interleaved_into_lb),
									results->lines_interleaved_in_mem_access[i])),
							results->bytes_per_pixel[i]),
						results->bytes_per_request[i]),
					results->useful_bytes_per_request[i]);
			results->active_time[i] =
				bw_div(
					bw_div(
						results->source_width_rounded_up_to_chunks[i],
						results->hsr[i]),
					results->pixel_rate[i]);
		}
	}
	for (i = 0; i <= 2; i += 1) {
		for (j = 0; j <= 2; j += 1) {
			results->dmif_burst_time[i][j] =
				bw_max3(
					results->dmif_total_page_close_open_time,
					bw_div(
						results->total_display_reads_required_dram_access_data,
						(mul(
							bw_div(
								mul(yclk[i],
									vbios->dram_channel_width_in_bits),
								int_to_fixed(
									8)),
							vbios->number_of_dram_channels))),
					bw_div(
						results->total_display_reads_required_data,
						(mul(sclk[j],
							vbios->data_return_bus_width))));
			if (mode_data->d1_display_write_back_dwb_enable
				== true) {
				results->mcifwr_burst_time[i][j] =
					bw_max3(
						results->mcifwr_total_page_close_open_time,
						bw_div(
							results->total_display_writes_required_dram_access_data,
							(mul(
								bw_div(
									mul(
										yclk[i],
										vbios->dram_channel_width_in_bits),
									int_to_fixed(
										8)),
								vbios->number_of_dram_channels))),
						bw_div(
							results->total_display_writes_required_data,
							(mul(sclk[j],
								vbios->data_return_bus_width))));
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		for (j = 0; j <= 2; j += 1) {
			for (k = 0; k <= 2; k += 1) {
				if (results->enable[i]) {
					if (surface_type[i]
						!= def_display_write_back420_luma
						&& surface_type[i]
							!= def_display_write_back420_chroma) {
						results->line_source_transfer_time[i][j][k] =
							bw_max(
								mul(
									(add(
										results->total_dmifmc_urgent_latency,
										results->dmif_burst_time[j][k])),
									bw_floor(
										bw_div(
											results->src_data_for_first_output_pixel[i],
											results->adjusted_data_buffer_size_in_memory[i]),
										int_to_fixed(
											1))),
								sub(
									mul(
										(add(
											results->total_dmifmc_urgent_latency,
											results->dmif_burst_time[j][k])),
										bw_floor(
											bw_div(
												results->src_data_for_last_output_pixel[i],
												results->adjusted_data_buffer_size_in_memory[i]),
											int_to_fixed(
												1))),
									results->active_time[i]));
					} else {
						results->line_source_transfer_time[i][j][k] =
							bw_max(
								mul(
									(add(
										vbios->mcifwrmc_urgent_latency,
										results->mcifwr_burst_time[j][k])),
									bw_floor(
										bw_div(
											results->src_data_for_first_output_pixel[i],
											results->adjusted_data_buffer_size_in_memory[i]),
										int_to_fixed(
											1))),
								sub(
									mul(
										(add(
											vbios->mcifwrmc_urgent_latency,
											results->mcifwr_burst_time[j][k])),
										bw_floor(
											bw_div(
												results->src_data_for_last_output_pixel[i],
												results->adjusted_data_buffer_size_in_memory[i]),
											int_to_fixed(
												1))),
									results->active_time[i]));
					}
				}
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (equ(
				dceip->stutter_and_dram_clock_state_change_gated_before_cursor,
				int_to_fixed(0))
				&& gtn(results->cursor_width_pixels[i],
					int_to_fixed(0))) {
				if (ltn(results->vsr[i], int_to_fixed(2))) {
					results->cursor_latency_hiding[i] =
						bw_div(
							bw_div(
								mul(
									(sub(
										dceip->cursor_dcp_buffer_lines,
										int_to_fixed(
											1))),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]);
				} else {
					results->cursor_latency_hiding[i] =
						bw_div(
							bw_div(
								mul(
									(sub(
										dceip->cursor_dcp_buffer_lines,
										int_to_fixed(
											3))),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]);
				}
			} else {
				results->cursor_latency_hiding[i] =
					int_to_fixed(9999);
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (dceip->graphics_lb_nodownscaling_multi_line_prefetching
				== true
				&& (equ(results->vsr[i], int_to_fixed(1))
					|| (leq(results->vsr[i],
						frc_to_fixed(8, 10))
						&& leq(results->v_taps[i],
							int_to_fixed(2))
						&& equ(results->lb_bpc[i],
							int_to_fixed(8))))
				&& surface_type[i] == def_graphics) {
				results->minimum_latency_hiding[i] =
					sub(
						sub(
							add(
								bw_div(
									mul(
										bw_div(
											(bw_div(
												bw_div(
													results->data_buffer_size[i],
													results->bytes_per_pixel[i]),
												results->source_width_pixels[i])),
											results->vsr[i]),
										results->h_total[i]),
									results->pixel_rate[i]),
								results->lb_partitions[i]),
							int_to_fixed(1)),
						results->total_dmifmc_urgent_latency);
			} else {
				results->minimum_latency_hiding[i] =
					sub(
						bw_div(
							mul(
								(add(
									add(
										results->line_buffer_prefetch[i],
										int_to_fixed(
											1)),
									bw_div(
										bw_div(
											bw_div(
												results->data_buffer_size[i],
												results->bytes_per_pixel[i]),
											results->source_width_pixels[i]),
										results->vsr[i]))),
								results->h_total[i]),
							results->pixel_rate[i]),
						results->total_dmifmc_urgent_latency);
			}
			results->minimum_latency_hiding_with_cursor[i] = bw_min(
				results->minimum_latency_hiding[i],
				results->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i += 1) {
		for (j = 0; j <= 2; j += 1) {
			results->blackout_duration_margin[i][j] = int_to_fixed(
				9999);
			results->dispclk_required_for_blackout_duration[i][j] =
				int_to_fixed(0);
			results->dispclk_required_for_blackout_recovery[i][j] =
				int_to_fixed(0);
			for (k = 0; k <= maximum_number_of_surfaces - 1; k +=
				1) {
				if (results->enable[k]
					&& gtn(vbios->blackout_duration,
						int_to_fixed(0))) {
					if (surface_type[k]
						!= def_display_write_back420_luma
						&& surface_type[k]
							!= def_display_write_back420_chroma) {
						results->blackout_duration_margin[i][j] =
							bw_min(
								results->blackout_duration_margin[i][j],
								sub(
									sub(
										sub(
											results->minimum_latency_hiding_with_cursor[k],
											vbios->blackout_duration),
										results->dmif_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_blackout_duration[i][j] =
							bw_max3(
								results->dispclk_required_for_blackout_duration[i][j],
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(sub(
										sub(
											results->minimum_latency_hiding_with_cursor[k],
											vbios->blackout_duration),
										results->dmif_burst_time[i][j]))),
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(add(
										sub(
											sub(
												results->minimum_latency_hiding_with_cursor[k],
												vbios->blackout_duration),
											results->dmif_burst_time[i][j]),
										results->active_time[k]))));
						if (leq(
							vbios->maximum_blackout_recovery_time,
							add(
								mul(
									results->total_dmifmc_urgent_latency,
									int_to_fixed(
										2)),
								results->dmif_burst_time[i][j]))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								int_to_fixed(
									9999);
						} else if (ltn(
							results->adjusted_data_buffer_size[k],
							mul(
								bw_div(
									mul(
										results->display_bandwidth[k],
										results->useful_bytes_per_request[k]),
									results->bytes_per_request[k]),
								(add(
									add(
										vbios->blackout_duration,
										mul(
											results->total_dmifmc_urgent_latency,
											int_to_fixed(
												2))),
									results->dmif_burst_time[i][j]))))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_max(
									results->dispclk_required_for_blackout_recovery[i][j],
									bw_div(
										mul(
											bw_div(
												bw_div(
													(sub(
														mul(
															bw_div(
																mul(
																	results->display_bandwidth[k],
																	results->useful_bytes_per_request[k]),
																results->bytes_per_request[k]),
															(add(
																vbios->blackout_duration,
																vbios->maximum_blackout_recovery_time))),
														results->adjusted_data_buffer_size[k])),
													results->bytes_per_pixel[k]),
												(sub(
													sub(
														vbios->maximum_blackout_recovery_time,
														mul(
															results->total_dmifmc_urgent_latency,
															int_to_fixed(
																2))),
													results->dmif_burst_time[i][j]))),
											results->latency_hiding_lines[k]),
										results->lines_interleaved_in_mem_access[k]));
						}
					} else {
						results->blackout_duration_margin[i][j] =
							bw_min(
								results->blackout_duration_margin[i][j],
								sub(
									sub(
										sub(
											sub(
												results->minimum_latency_hiding_with_cursor[k],
												vbios->blackout_duration),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_blackout_duration[i][j] =
							bw_max3(
								results->dispclk_required_for_blackout_duration[i][j],
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(sub(
										sub(
											sub(
												results->minimum_latency_hiding_with_cursor[k],
												vbios->blackout_duration),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]))),
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(add(
										sub(
											sub(
												sub(
													results->minimum_latency_hiding_with_cursor[k],
													vbios->blackout_duration),
												results->dmif_burst_time[i][j]),
											results->mcifwr_burst_time[i][j]),
										results->active_time[k]))));
						if (ltn(
							vbios->maximum_blackout_recovery_time,
							add(
								add(
									mul(
										vbios->mcifwrmc_urgent_latency,
										int_to_fixed(
											2)),
									results->dmif_burst_time[i][j]),
								results->mcifwr_burst_time[i][j]))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								int_to_fixed(
									9999);
						} else if (ltn(
							results->adjusted_data_buffer_size[k],
							mul(
								bw_div(
									mul(
										results->display_bandwidth[k],
										results->useful_bytes_per_request[k]),
									results->bytes_per_request[k]),
								(add(
									add(
										vbios->blackout_duration,
										mul(
											results->total_dmifmc_urgent_latency,
											int_to_fixed(
												2))),
									results->dmif_burst_time[i][j]))))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_max(
									results->dispclk_required_for_blackout_recovery[i][j],
									bw_div(
										mul(
											bw_div(
												bw_div(
													(sub(
														mul(
															bw_div(
																mul(
																	results->display_bandwidth[k],
																	results->useful_bytes_per_request[k]),
																results->bytes_per_request[k]),
															(add(
																vbios->blackout_duration,
																vbios->maximum_blackout_recovery_time))),
														results->adjusted_data_buffer_size[k])),
													results->bytes_per_pixel[k]),
												(sub(
													vbios->maximum_blackout_recovery_time,
													(add(
														mul(
															results->total_dmifmc_urgent_latency,
															int_to_fixed(
																2)),
														results->dmif_burst_time[i][j]))))),
											results->latency_hiding_lines[k]),
										results->lines_interleaved_in_mem_access[k]));
						}
					}
				}
			}
		}
	}
	if (gtn(results->blackout_duration_margin[high][high], int_to_fixed(0))
		&& ltn(
			results->dispclk_required_for_blackout_duration[high][high],
			vbios->high_voltage_max_dispclk_mhz)) {
		results->cpup_state_change_enable = true;
		if (ltn(
			results->dispclk_required_for_blackout_recovery[high][high],
			vbios->high_voltage_max_dispclk_mhz)) {
			results->cpuc_state_change_enable = true;
		} else {
			results->cpuc_state_change_enable = false;
		}
	} else {
		results->cpup_state_change_enable = false;
		results->cpuc_state_change_enable = false;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (mode_data->number_of_displays <= 1
				|| mode_data->display_synchronization_enabled
					== true) {
				results->maximum_latency_hiding[i] =
					int_to_fixed(450);
			} else {
				results->maximum_latency_hiding[i] =
					add(
						add(
							results->minimum_latency_hiding[i],
							bw_div(
								mul(
									bw_div(
										int_to_fixed(
											1),
										results->vsr[i]),
									results->h_total[i]),
								results->pixel_rate[i])),
						mul(frc_to_fixed(1, 2),
							results->total_dmifmc_urgent_latency));
			}
			results->maximum_latency_hiding_with_cursor[i] = bw_min(
				results->maximum_latency_hiding[i],
				results->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i += 1) {
		for (j = 0; j <= 2; j += 1) {
			results->dram_speed_change_margin[i][j] = int_to_fixed(
				9999);
			results->dispclk_required_for_dram_speed_change[i][j] =
				int_to_fixed(0);
			for (k = 0; k <= maximum_number_of_surfaces - 1; k +=
				1) {
				if (results->enable[k]) {
					if (surface_type[k]
						!= def_display_write_back420_luma
						&& surface_type[k]
							!= def_display_write_back420_chroma) {
						results->dram_speed_change_margin[i][j] =
							bw_min(
								results->dram_speed_change_margin[i][j],
								sub(
									sub(
										sub(
											results->maximum_latency_hiding_with_cursor[k],
											vbios->nbp_state_change_latency),
										results->dmif_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_dram_speed_change[i][j] =
							bw_max3(
								results->dispclk_required_for_dram_speed_change[i][j],
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(sub(
										sub(
											results->maximum_latency_hiding_with_cursor[k],
											vbios->nbp_state_change_latency),
										results->dmif_burst_time[i][j]))),
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(add(
										sub(
											sub(
												results->maximum_latency_hiding_with_cursor[k],
												vbios->nbp_state_change_latency),
											results->dmif_burst_time[i][j]),
										results->active_time[k]))));
					} else {
						results->dram_speed_change_margin[i][j] =
							bw_min(
								results->dram_speed_change_margin[i][j],
								sub(
									sub(
										sub(
											sub(
												results->maximum_latency_hiding_with_cursor[k],
												vbios->nbp_state_change_latency),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_dram_speed_change[i][j] =
							bw_max3(
								results->dispclk_required_for_dram_speed_change[i][j],
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(sub(
										sub(
											sub(
												results->maximum_latency_hiding_with_cursor[k],
												vbios->nbp_state_change_latency),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]))),
								bw_div(
									bw_div(
										mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(add(
										sub(
											sub(
												sub(
													results->maximum_latency_hiding_with_cursor[k],
													vbios->nbp_state_change_latency),
												results->dmif_burst_time[i][j]),
											results->mcifwr_burst_time[i][j]),
										results->active_time[k]))));
					}
				}
			}
		}
	}
	if (gtn(results->dram_speed_change_margin[high][high], int_to_fixed(0))
		&& ltn(
			results->dispclk_required_for_dram_speed_change[high][high],
			vbios->high_voltage_max_dispclk_mhz)) {
		results->nbp_state_change_enable = true;
	} else {
		results->nbp_state_change_enable = false;
	}
	results->min_cursor_memory_interface_buffer_size_in_time = int_to_fixed(
		9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (gtn(results->cursor_width_pixels[i],
				int_to_fixed(0))) {
				results->min_cursor_memory_interface_buffer_size_in_time =
					bw_min(
						results->min_cursor_memory_interface_buffer_size_in_time,
						bw_div(
							mul(
								bw_div(
									bw_div(
										dceip->cursor_memory_interface_buffer_pixels,
										results->cursor_width_pixels[i]),
									results->vsr[i]),
								results->h_total[i]),
							results->pixel_rate[i]));
			}
		}
	}
	results->min_read_buffer_size_in_time = bw_min(
		results->min_cursor_memory_interface_buffer_size_in_time,
		results->min_dmif_size_in_time);
	results->display_reads_time_for_data_transfer = sub(
		results->min_read_buffer_size_in_time,
		results->total_dmifmc_urgent_latency);
	results->display_writes_time_for_data_transfer = sub(
		results->min_mcifwr_size_in_time,
		vbios->mcifwrmc_urgent_latency);
	results->dmif_required_dram_bandwidth = bw_div(
		results->total_display_reads_required_dram_access_data,
		results->display_reads_time_for_data_transfer);
	results->mcifwr_required_dram_bandwidth = bw_div(
		results->total_display_writes_required_dram_access_data,
		results->display_writes_time_for_data_transfer);
	results->required_dmifmc_urgent_latency_for_page_close_open = bw_div(
		(sub(results->min_read_buffer_size_in_time,
			results->dmif_total_page_close_open_time)),
		results->total_dmifmc_urgent_trips);
	results->required_mcifmcwr_urgent_latency = sub(
		results->min_mcifwr_size_in_time,
		results->mcifwr_total_page_close_open_time);
	if (gtn(results->scatter_gather_total_pte_requests,
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		results->required_dram_bandwidth_gbyte_per_second =
			int_to_fixed(9999);
		yclk_message =
			def_exceeded_allowed_outstanding_pte_req_queue_size;
		y_clk_level = high;
		results->dram_bandwidth = mul(
			bw_div(
				mul(vbios->high_yclk_mhz,
					vbios->dram_channel_width_in_bits),
				int_to_fixed(8)),
			vbios->number_of_dram_channels);
	} else if (gtn(vbios->dmifmc_urgent_latency,
		results->required_dmifmc_urgent_latency_for_page_close_open)
		|| gtn(vbios->mcifwrmc_urgent_latency,
			results->required_mcifmcwr_urgent_latency)) {
		results->required_dram_bandwidth_gbyte_per_second =
			int_to_fixed(9999);
		yclk_message = def_exceeded_allowed_page_close_open;
		y_clk_level = high;
		results->dram_bandwidth = mul(
			bw_div(
				mul(vbios->high_yclk_mhz,
					vbios->dram_channel_width_in_bits),
				int_to_fixed(8)),
			vbios->number_of_dram_channels);
	} else {
		results->required_dram_bandwidth_gbyte_per_second = bw_div(
			bw_max(results->dmif_required_dram_bandwidth,
				results->mcifwr_required_dram_bandwidth),
			int_to_fixed(1000));
		if (ltn(
			mul(results->required_dram_bandwidth_gbyte_per_second,
				int_to_fixed(1000)),
			mul(
				bw_div(
					mul(vbios->low_yclk_mhz,
						vbios->dram_channel_width_in_bits),
					int_to_fixed(8)),
				vbios->number_of_dram_channels))
			&& (results->cpup_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[low][high],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[low][high],
						vbios->high_voltage_max_dispclk_mhz)))
			&& (results->cpuc_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[low][high],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[low][high],
						vbios->high_voltage_max_dispclk_mhz)
					&& ltn(
						results->dispclk_required_for_blackout_recovery[low][high],
						vbios->high_voltage_max_dispclk_mhz)))
			&& gtn(results->dram_speed_change_margin[low][high],
				int_to_fixed(0))
			&& ltn(
				results->dispclk_required_for_dram_speed_change[low][high],
				vbios->high_voltage_max_dispclk_mhz)) {
			yclk_message = def_low;
			y_clk_level = low;
			results->dram_bandwidth =
				mul(
					bw_div(
						mul(vbios->low_yclk_mhz,
							vbios->dram_channel_width_in_bits),
						int_to_fixed(8)),
					vbios->number_of_dram_channels);
		} else if (ltn(
			mul(results->required_dram_bandwidth_gbyte_per_second,
				int_to_fixed(1000)),
			mul(
				bw_div(
					mul(vbios->high_yclk_mhz,
						vbios->dram_channel_width_in_bits),
					int_to_fixed(8)),
				vbios->number_of_dram_channels))) {
			yclk_message = def_high;
			y_clk_level = high;
			results->dram_bandwidth =
				mul(
					bw_div(
						mul(vbios->high_yclk_mhz,
							vbios->dram_channel_width_in_bits),
						int_to_fixed(8)),
					vbios->number_of_dram_channels);
		} else {
			yclk_message = def_exceeded_allowed_maximum_bw;
			y_clk_level = high;
			results->dram_bandwidth =
				mul(
					bw_div(
						mul(vbios->high_yclk_mhz,
							vbios->dram_channel_width_in_bits),
						int_to_fixed(8)),
					vbios->number_of_dram_channels);
		}
	}
	results->dmif_required_sclk = bw_div(
		bw_div(results->total_display_reads_required_data,
			results->display_reads_time_for_data_transfer),
		vbios->data_return_bus_width);
	results->mcifwr_required_sclk = bw_div(
		bw_div(results->total_display_writes_required_data,
			results->display_writes_time_for_data_transfer),
		vbios->data_return_bus_width);
	if (gtn(results->scatter_gather_total_pte_requests,
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		results->required_sclk = int_to_fixed(9999);
		sclk_message =
			def_exceeded_allowed_outstanding_pte_req_queue_size;
		sclk_level = high;
	} else if (gtn(vbios->dmifmc_urgent_latency,
		results->required_dmifmc_urgent_latency_for_page_close_open)
		|| gtn(vbios->mcifwrmc_urgent_latency,
			results->required_mcifmcwr_urgent_latency)) {
		results->required_sclk = int_to_fixed(9999);
		sclk_message = def_exceeded_allowed_page_close_open;
		sclk_level = high;
	} else {
		results->required_sclk = bw_max(results->dmif_required_sclk,
			results->mcifwr_required_sclk);
		if (ltn(results->required_sclk, vbios->low_sclk_mhz)
			&& (results->cpup_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[y_clk_level][low],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[y_clk_level][low],
						vbios->high_voltage_max_dispclk_mhz)))
			&& (results->cpuc_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[y_clk_level][low],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[y_clk_level][low],
						vbios->high_voltage_max_dispclk_mhz)
					&& ltn(
						results->dispclk_required_for_blackout_recovery[y_clk_level][low],
						vbios->high_voltage_max_dispclk_mhz)))
			&& (results->nbp_state_change_enable == false
				|| (gtn(
					results->dram_speed_change_margin[y_clk_level][low],
					int_to_fixed(0))
					&& leq(
						results->dispclk_required_for_dram_speed_change[y_clk_level][low],
						vbios->high_voltage_max_dispclk_mhz)))) {
			sclk_message = def_low;
			sclk_level = low;
		} else if (ltn(results->required_sclk, vbios->mid_sclk_mhz)
			&& (results->cpup_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[y_clk_level][mid],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[y_clk_level][mid],
						vbios->high_voltage_max_dispclk_mhz)))
			&& (results->cpuc_state_change_enable == false
				|| (gtn(
					results->blackout_duration_margin[y_clk_level][mid],
					int_to_fixed(0))
					&& ltn(
						results->dispclk_required_for_blackout_duration[y_clk_level][mid],
						vbios->high_voltage_max_dispclk_mhz)
					&& ltn(
						results->dispclk_required_for_blackout_recovery[y_clk_level][mid],
						vbios->high_voltage_max_dispclk_mhz)))
			&& (results->nbp_state_change_enable == false
				|| (gtn(
					results->dram_speed_change_margin[y_clk_level][mid],
					int_to_fixed(0))
					&& leq(
						results->dispclk_required_for_dram_speed_change[y_clk_level][mid],
						vbios->high_voltage_max_dispclk_mhz)))) {
			sclk_message = def_mid;
			sclk_level = mid;
		} else if (ltn(results->required_sclk, vbios->high_sclk_mhz)) {
			sclk_message = def_high;
			sclk_level = high;
		} else {
			sclk_message = def_exceeded_allowed_maximum_sclk;
			sclk_level = high;
		}
	}
	results->downspread_factor = add(
		bw_div(vbios->down_spread_percentage, int_to_fixed(100)),
		int_to_fixed(1));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] == def_graphics) {
				if (equ(results->lb_bpc[i], int_to_fixed(6))) {
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency6_bit_per_component;
				} else if (equ(results->lb_bpc[i],
					int_to_fixed(8))) {
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency8_bit_per_component;
				} else if (equ(results->lb_bpc[i],
					int_to_fixed(10))) {
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency10_bit_per_component;
				} else {
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency12_bit_per_component;
				}
				if (results->use_alpha[i] == true) {
					results->v_scaler_efficiency =
						bw_min(
							results->v_scaler_efficiency,
							dceip->alpha_vscaler_efficiency);
				}
			} else {
				if (equ(results->lb_bpc[i], int_to_fixed(6))) {
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency6_bit_per_component;
				} else if (equ(results->lb_bpc[i],
					int_to_fixed(8))) {
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency8_bit_per_component;
				} else if (equ(results->lb_bpc[i],
					int_to_fixed(10))) {
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency10_bit_per_component;
				} else {
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency12_bit_per_component;
				}
			}
			if (dceip->pre_downscaler_enabled
				&& gtn(results->hsr[i], int_to_fixed(1))) {
				results->scaler_limits_factor =
					bw_max(
						bw_div(results->v_taps[i],
							results->v_scaler_efficiency),
						bw_div(
							results->source_width_rounded_up_to_chunks[i],
							results->h_total[i]));
			} else {
				results->scaler_limits_factor =
					bw_max3(int_to_fixed(1),
						bw_ceil(
							bw_div(results->h_taps[i],
								int_to_fixed(
									4)),
							int_to_fixed(1)),
						mul(results->hsr[i],
							bw_max(
								bw_div(
									results->v_taps[i],
									results->v_scaler_efficiency),
								int_to_fixed(
									1))));
			}
			results->display_pipe_pixel_throughput =
				bw_div(
					bw_div(
						mul(
							bw_max(
								results->lb_lines_in_per_line_out_in_beginning_of_frame[i],
								mul(
									results->lb_lines_in_per_line_out_in_middle_of_frame[i],
									results->horizontal_blank_and_chunk_granularity_factor[i])),
							results->source_width_rounded_up_to_chunks[i]),
						(bw_div(results->h_total[i],
							results->pixel_rate[i]))),
					dceip->lb_write_pixels_per_dispclk);
			results->dispclk_required_without_ramping[i] =
				mul(results->downspread_factor,
					bw_max(
						mul(results->pixel_rate[i],
							results->scaler_limits_factor),
						mul(
							dceip->display_pipe_throughput_factor,
							results->display_pipe_pixel_throughput)));
			results->dispclk_required_with_ramping[i] =
				mul(dceip->dispclk_ramping_factor,
					bw_max(
						mul(results->pixel_rate[i],
							results->scaler_limits_factor),
						results->display_pipe_pixel_throughput));
		}
	}
	results->total_dispclk_required_with_ramping = int_to_fixed(0);
	results->total_dispclk_required_without_ramping = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (ltn(results->total_dispclk_required_with_ramping,
				results->dispclk_required_with_ramping[i])) {
				results->total_dispclk_required_with_ramping =
					results->dispclk_required_with_ramping[i];
			}
			if (ltn(results->total_dispclk_required_without_ramping,
				results->dispclk_required_without_ramping[i])) {
				results->total_dispclk_required_without_ramping =
					results->dispclk_required_without_ramping[i];
			}
		}
	}
	results->total_read_request_bandwidth = int_to_fixed(0);
	results->total_write_request_bandwidth = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				results->total_read_request_bandwidth = add(
					results->total_read_request_bandwidth,
					results->request_bandwidth[i]);
			} else {
				results->total_write_request_bandwidth = add(
					results->total_write_request_bandwidth,
					results->request_bandwidth[i]);
			}
		}
	}
	results->dispclk_required_for_total_read_request_bandwidth = bw_div(
		mul(results->total_read_request_bandwidth,
			dceip->dispclk_per_request), dceip->request_efficiency);
	if (equ(dceip->dcfclk_request_generation, int_to_fixed(0))) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max(results->total_dispclk_required_with_ramping,
				results->dispclk_required_for_total_read_request_bandwidth);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max(results->total_dispclk_required_without_ramping,
				results->dispclk_required_for_total_read_request_bandwidth);
	} else {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			results->total_dispclk_required_with_ramping;
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			results->total_dispclk_required_without_ramping;
	}
	if (results->cpuc_state_change_enable == true) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max3(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[y_clk_level][sclk_level],
				results->dispclk_required_for_blackout_recovery[y_clk_level][sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max3(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[y_clk_level][sclk_level],
				results->dispclk_required_for_blackout_recovery[y_clk_level][sclk_level]);
	}
	if (results->cpup_state_change_enable == true) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[y_clk_level][sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[y_clk_level][sclk_level]);
	}
	if (results->nbp_state_change_enable == true) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_dram_speed_change[y_clk_level][sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_dram_speed_change[y_clk_level][sclk_level]);
	}
	if (ltn(
		results->total_dispclk_required_with_ramping_with_request_bandwidth,
		vbios->high_voltage_max_dispclk_mhz)) {
		results->dispclk =
			results->total_dispclk_required_with_ramping_with_request_bandwidth;
	} else if (ltn(
		results->total_dispclk_required_without_ramping_with_request_bandwidth,
		vbios->high_voltage_max_dispclk_mhz)) {
		results->dispclk = vbios->high_voltage_max_dispclk_mhz;
	} else {
		results->dispclk =
			results->total_dispclk_required_without_ramping_with_request_bandwidth;
	}
	if (pipe_check == def_notok) {
		voltage = def_na;
		mode_background_color = def_na_color;
		mode_font_color = def_vb_white;
	} else if (mode_check == def_notok) {
		voltage = def_notok;
		mode_background_color = def_notok_color;
		mode_font_color = def_vb_black;
	} else if (yclk_message == def_low && sclk_message == def_low
		&& ltn(results->dispclk, vbios->low_voltage_max_dispclk_mhz)) {
		voltage = def_low;
		mode_background_color = def_low_color;
		mode_font_color = def_vb_black;
	} else if (yclk_message == def_low
		&& (sclk_message == def_low || sclk_message == def_mid)
		&& ltn(results->dispclk, vbios->mid_voltage_max_dispclk_mhz)) {
		voltage = def_mid;
		mode_background_color = def_mid_color;
		mode_font_color = def_vb_black;
	} else if ((yclk_message == def_low || yclk_message == def_high)
		&& (sclk_message == def_low || sclk_message == def_mid
			|| sclk_message == def_high)
		&& leq(results->dispclk, vbios->high_voltage_max_dispclk_mhz)) {
		if (results->nbp_state_change_enable == true) {
			voltage = def_high;
			mode_background_color = def_high_color;
			mode_font_color = def_vb_black;
		} else {
			voltage = def_high_no_nbp_state_change;
			mode_background_color =
				def_high_no_nbp_state_change_color;
			mode_font_color = def_vb_black;
		}
	} else {
		voltage = def_notok;
		mode_background_color = def_notok_color;
		mode_font_color = def_vb_black;
	}
	if (mode_background_color == def_na_color
		|| mode_background_color == def_notok_color) {
		mode_pattern = def_xl_pattern_solid;
	} else if (results->cpup_state_change_enable == false) {
		mode_pattern = def_xl_pattern_checker;
	} else if (results->cpuc_state_change_enable == false) {
		mode_pattern = def_xl_pattern_light_horizontal;
	} else {
		mode_pattern = def_xl_pattern_solid;
	}
	results->blackout_recovery_time = int_to_fixed(0);
	for (k = 0; k <= maximum_number_of_surfaces - 1; k += 1) {
		if (results->enable[k]
			&& gtn(vbios->blackout_duration, int_to_fixed(0))
			&& results->cpup_state_change_enable == true) {
			if (surface_type[k] != def_display_write_back420_luma
				&& surface_type[k]
					!= def_display_write_back420_chroma) {
				results->blackout_recovery_time =
					bw_max(results->blackout_recovery_time,
						add(
							mul(
								results->total_dmifmc_urgent_latency,
								int_to_fixed(
									2)),
							results->dmif_burst_time[y_clk_level][sclk_level]));
				if (ltn(results->adjusted_data_buffer_size[k],
					mul(
						bw_div(
							mul(
								results->display_bandwidth[k],
								results->useful_bytes_per_request[k]),
							results->bytes_per_request[k]),
						(add(
							add(
								vbios->blackout_duration,
								mul(
									results->total_dmifmc_urgent_latency,
									int_to_fixed(
										2))),
							results->dmif_burst_time[y_clk_level][sclk_level]))))) {
					results->blackout_recovery_time =
						bw_max(
							results->blackout_recovery_time,
							bw_div(
								(sub(
									add(
										mul(
											bw_div(
												mul(
													results->display_bandwidth[k],
													results->useful_bytes_per_request[k]),
												results->bytes_per_request[k]),
											vbios->blackout_duration),
										bw_div(
											mul(
												mul(
													mul(
														(add(
															mul(
																results->total_dmifmc_urgent_latency,
																int_to_fixed(
																	2)),
															results->dmif_burst_time[y_clk_level][sclk_level])),
														results->dispclk),
													results->bytes_per_pixel[k]),
												results->lines_interleaved_in_mem_access[k]),
											results->latency_hiding_lines[k])),
									results->adjusted_data_buffer_size[k])),
								(sub(
									bw_div(
										mul(
											mul(
												results->dispclk,
												results->bytes_per_pixel[k]),
											results->lines_interleaved_in_mem_access[k]),
										results->latency_hiding_lines[k]),
									bw_div(
										mul(
											results->display_bandwidth[k],
											results->useful_bytes_per_request[k]),
										results->bytes_per_request[k])))));
				}
			} else {
				results->blackout_recovery_time =
					bw_max(results->blackout_recovery_time,
						add(
							mul(
								vbios->mcifwrmc_urgent_latency,
								int_to_fixed(
									2)),
							results->mcifwr_burst_time[y_clk_level][sclk_level]));
				if (ltn(results->adjusted_data_buffer_size[k],
					mul(
						bw_div(
							mul(
								results->display_bandwidth[k],
								results->useful_bytes_per_request[k]),
							results->bytes_per_request[k]),
						(add(
							add(
								vbios->blackout_duration,
								mul(
									vbios->mcifwrmc_urgent_latency,
									int_to_fixed(
										2))),
							results->mcifwr_burst_time[y_clk_level][sclk_level]))))) {
					results->blackout_recovery_time =
						bw_max(
							results->blackout_recovery_time,
							bw_div(
								(sub(
									add(
										mul(
											bw_div(
												mul(
													results->display_bandwidth[k],
													results->useful_bytes_per_request[k]),
												results->bytes_per_request[k]),
											vbios->blackout_duration),
										bw_div(
											mul(
												mul(
													mul(
														(add(
															add(
																mul(
																	vbios->mcifwrmc_urgent_latency,
																	int_to_fixed(
																		2)),
																results->dmif_burst_time[i][j]),
															results->mcifwr_burst_time[y_clk_level][sclk_level])),
														results->dispclk),
													results->bytes_per_pixel[k]),
												results->lines_interleaved_in_mem_access[k]),
											results->latency_hiding_lines[k])),
									results->adjusted_data_buffer_size[k])),
								(sub(
									bw_div(
										mul(
											mul(
												results->dispclk,
												results->bytes_per_pixel[k]),
											results->lines_interleaved_in_mem_access[k]),
										results->latency_hiding_lines[k]),
									bw_div(
										mul(
											results->display_bandwidth[k],
											results->useful_bytes_per_request[k]),
										results->bytes_per_request[k])))));
				}
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (surface_type[i] == def_display_write_back420_luma
				|| surface_type[i]
					== def_display_write_back420_chroma) {
				results->pixels_per_data_fifo_entry[i] =
					int_to_fixed(16);
			} else if (surface_type[i] == def_graphics) {
				results->pixels_per_data_fifo_entry[i] = bw_div(
					int_to_fixed(64),
					results->bytes_per_pixel[i]);
			} else if (results->orthogonal_rotation[i] == false) {
				results->pixels_per_data_fifo_entry[i] =
					int_to_fixed(16);
			} else {
				results->pixels_per_data_fifo_entry[i] = bw_div(
					int_to_fixed(16),
					results->bytes_per_pixel[i]);
			}
		}
	}
	results->min_pixels_per_data_fifo_entry = int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (gtn(results->min_pixels_per_data_fifo_entry,
				results->pixels_per_data_fifo_entry[i])) {
				results->min_pixels_per_data_fifo_entry =
					results->pixels_per_data_fifo_entry[i];
			}
		}
	}
	results->sclk_deep_sleep = bw_max(
		bw_div(mul(results->dispclk, frc_to_fixed(115, 100)),
			results->min_pixels_per_data_fifo_entry),
		results->total_read_request_bandwidth);
	results->chunk_request_time = int_to_fixed(0);
	results->cursor_request_time = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->chunk_request_time =
				add(results->chunk_request_time,
					bw_div(
						bw_div(
							mul(pixels_per_chunk,
								results->bytes_per_pixel[i]),
							results->useful_bytes_per_request[i]),
						bw_min(sclk[sclk_level],
							bw_div(results->dispclk,
								int_to_fixed(
									2)))));
		}
	}
	results->cursor_request_time = (bw_div(results->cursor_total_data,
		(mul(sclk[sclk_level], int_to_fixed(32)))));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->line_source_pixels_transfer_time =
				bw_max(
					bw_div(
						bw_div(
							results->src_pixels_for_first_output_pixel[i],
							dceip->lb_write_pixels_per_dispclk),
						(bw_div(results->dispclk,
							dceip->display_pipe_throughput_factor))),
					sub(
						bw_div(
							bw_div(
								results->src_pixels_for_last_output_pixel[i],
								dceip->lb_write_pixels_per_dispclk),
							(bw_div(results->dispclk,
								dceip->display_pipe_throughput_factor))),
						results->active_time[i]));
			if (surface_type[i] != def_display_write_back420_luma
				&& surface_type[i]
					!= def_display_write_back420_chroma) {
				results->urgent_watermark[i] =
					add(
						add(
							add(
								add(
									add(
										results->total_dmifmc_urgent_latency,
										results->dmif_burst_time[y_clk_level][sclk_level]),
									bw_max(
										results->line_source_pixels_transfer_time,
										results->line_source_transfer_time[i][y_clk_level][sclk_level])),
								vbios->blackout_duration),
							results->chunk_request_time),
						results->cursor_request_time);
				results->stutter_exit_watermark[i] =
					add(
						sub(
							vbios->stutter_self_refresh_exit_latency,
							results->total_dmifmc_urgent_latency),
						results->urgent_watermark[i]);
				results->nbp_state_change_watermark[i] =
					sub(
						add(
							sub(
								vbios->nbp_state_change_latency,
								results->total_dmifmc_urgent_latency),
							results->urgent_watermark[i]),
						vbios->blackout_duration);
			} else {
				results->urgent_watermark[i] =
					add(
						add(
							add(
								add(
									add(
										vbios->mcifwrmc_urgent_latency,
										results->mcifwr_burst_time[y_clk_level][sclk_level]),
									bw_max(
										results->line_source_pixels_transfer_time,
										results->line_source_transfer_time[i][y_clk_level][sclk_level])),
								vbios->blackout_duration),
							results->chunk_request_time),
						results->cursor_request_time);
				results->stutter_exit_watermark[i] =
					int_to_fixed(0);
				results->nbp_state_change_watermark[i] =
					add(
						sub(
							add(
								vbios->nbp_state_change_latency,
								results->dmif_burst_time[y_clk_level][sclk_level]),
							vbios->mcifwrmc_urgent_latency),
						results->urgent_watermark[i]);
			}
		}
	}
	results->stutter_mode_enable = results->cpuc_state_change_enable;
	if (mode_data->number_of_displays > 1) {
		for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
			if (results->enable[i]) {
				if (gtn(results->stutter_exit_watermark[i],
					results->cursor_latency_hiding[i])) {
					results->stutter_mode_enable = false;
				}
			}
		}
	}
	results->dmifdram_access_efficiency =
		bw_min(
			bw_div(
				bw_div(
					results->total_display_reads_required_dram_access_data,
					results->dram_bandwidth),
				results->dmif_total_page_close_open_time),
			int_to_fixed(1));
	if (gtn(results->total_display_writes_required_dram_access_data,
		int_to_fixed(0))) {
		results->mcifwrdram_access_efficiency =
			bw_min(
				bw_div(
					bw_div(
						results->total_display_writes_required_dram_access_data,
						results->dram_bandwidth),
					results->mcifwr_total_page_close_open_time),
				int_to_fixed(1));
	} else {
		results->mcifwrdram_access_efficiency = int_to_fixed(0);
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->average_bandwidth_no_compression[i] =
				bw_div(
					mul(
						mul(
							bw_div(
								mul(
									results->source_width_rounded_up_to_chunks[i],
									results->bytes_per_pixel[i]),
								(bw_div(
									results->h_total[i],
									results->pixel_rate[i]))),
							results->vsr[i]),
						results->bytes_per_request[i]),
					results->useful_bytes_per_request[i]);
			results->average_bandwidth[i] = bw_div(
				results->average_bandwidth_no_compression[i],
				results->compression_rate[i]);
		}
	}
	results->total_average_bandwidth_no_compression = int_to_fixed(0);
	results->total_average_bandwidth = int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->total_average_bandwidth_no_compression = add(
				results->total_average_bandwidth_no_compression,
				results->average_bandwidth_no_compression[i]);
			results->total_average_bandwidth = add(
				results->total_average_bandwidth,
				results->average_bandwidth[i]);
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->stutter_cycle_duration[i] =
				sub(
					mul(
						bw_div(
							bw_div(
								mul(
									bw_div(
										bw_div(
											results->adjusted_data_buffer_size[i],
											results->bytes_per_pixel[i]),
										results->source_width_rounded_up_to_chunks[i]),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]),
						results->compression_rate[i]),
					bw_max(int_to_fixed(0),
						sub(
							results->stutter_exit_watermark[i],
							bw_div(
								mul(
									(add(
										results->line_buffer_prefetch[i],
										int_to_fixed(
											2))),
									results->h_total[i]),
								results->pixel_rate[i]))));
		}
	}
	results->total_stutter_cycle_duration = int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			if (gtn(results->total_stutter_cycle_duration,
				results->stutter_cycle_duration[i])) {
				results->total_stutter_cycle_duration =
					results->stutter_cycle_duration[i];
			}
		}
	}
	results->stutter_burst_time = bw_div(
		mul(results->total_stutter_cycle_duration,
			results->total_average_bandwidth),
		bw_min(
			(mul(results->dram_bandwidth,
				results->dmifdram_access_efficiency)),
			mul(sclk[sclk_level], vbios->data_return_bus_width)));
	results->time_in_self_refresh = sub(
		sub(results->total_stutter_cycle_duration,
			vbios->stutter_self_refresh_exit_latency),
		results->stutter_burst_time);
	if (mode_data->d1_display_write_back_dwb_enable == true) {
		results->stutter_efficiency = int_to_fixed(0);
	} else if (ltn(results->time_in_self_refresh, int_to_fixed(0))) {
		results->stutter_efficiency = int_to_fixed(0);
	} else {
		results->stutter_efficiency = mul(
			bw_div(results->time_in_self_refresh,
				results->total_stutter_cycle_duration),
			int_to_fixed(100));
	}
	results->worst_number_of_trips_to_memory = int_to_fixed(1);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]
			&& results->scatter_gather_enable_for_pipe[i] == true) {
			results->number_of_trips_to_memory_for_getting_apte_row[i] =
				bw_ceil(
					bw_div(
						results->scatter_gather_pte_requests_in_row[i],
						results->scatter_gather_pte_request_limit[i]),
					int_to_fixed(1));
			if (ltn(results->worst_number_of_trips_to_memory,
				results->number_of_trips_to_memory_for_getting_apte_row[i])) {
				results->worst_number_of_trips_to_memory =
					results->number_of_trips_to_memory_for_getting_apte_row[i];
			}
		}
	}
	results->immediate_flip_time = mul(
		results->worst_number_of_trips_to_memory,
		results->total_dmifmc_urgent_latency);
	results->latency_for_non_dmif_clients = add(
		results->total_dmifmc_urgent_latency,
		results->dmif_burst_time[y_clk_level][sclk_level]);
	if (mode_data->d1_display_write_back_dwb_enable == true) {
		results->latency_for_non_mcifwr_clients = add(
			vbios->mcifwrmc_urgent_latency,
			dceip->mcifwr_all_surfaces_burst_time);
	} else {
		results->latency_for_non_mcifwr_clients = int_to_fixed(0);
	}
	results->dmifmc_urgent_latency_supported_in_high_sclk_and_yclk = bw_div(
		(sub(results->min_read_buffer_size_in_time,
			results->dmif_burst_time[high][high])),
		results->total_dmifmc_urgent_trips);
	results->nbp_state_dram_speed_change_margin = int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i += 1) {
		if (results->enable[i]) {
			results->nbp_state_dram_speed_change_margin =
				bw_min(
					results->nbp_state_dram_speed_change_margin,
					sub(
						results->maximum_latency_hiding_with_cursor[i],
						results->nbp_state_change_watermark[i]));
		}
	}
	for (i = 1; i <= 5; i += 1) {
		results->display_reads_time_for_data_transfer_and_urgent_latency =
			sub(results->min_read_buffer_size_in_time,
				mul(results->total_dmifmc_urgent_trips,
					int_to_fixed(i)));
		if (pipe_check == def_ok
			&& (gtn(
				results->display_reads_time_for_data_transfer_and_urgent_latency,
				results->dmif_total_page_close_open_time))) {
			results->dmif_required_sclk_for_urgent_latency[i] =
				bw_div(
					bw_div(
						results->total_display_reads_required_data,
						results->display_reads_time_for_data_transfer_and_urgent_latency),
					vbios->data_return_bus_width);
		} else {
			results->dmif_required_sclk_for_urgent_latency[i] =
				int_to_fixed(0);
		}
	}

}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

void bw_calcs_init(struct bw_calcs_input_dceip *bw_dceip,
	struct bw_calcs_input_vbios *bw_vbios)
{
	struct bw_calcs_input_dceip dceip;
	struct bw_calcs_input_vbios vbios;

	vbios.number_of_dram_channels = int_to_fixed(2);
	vbios.dram_channel_width_in_bits = int_to_fixed(64);
	vbios.number_of_dram_banks = int_to_fixed(8);
	vbios.high_yclk_mhz = int_to_fixed(1600);
	vbios.low_yclk_mhz = frc_to_fixed(66666, 100);
	vbios.low_sclk_mhz = int_to_fixed(200);
	vbios.mid_sclk_mhz = int_to_fixed(300);
	vbios.high_sclk_mhz = frc_to_fixed(62609, 100);
	vbios.low_voltage_max_dispclk_mhz = int_to_fixed(352);
	vbios.mid_voltage_max_dispclk_mhz = int_to_fixed(467);
	vbios.high_voltage_max_dispclk_mhz = int_to_fixed(643);
	vbios.data_return_bus_width = int_to_fixed(32);
	vbios.trc = int_to_fixed(50);
	vbios.dmifmc_urgent_latency = int_to_fixed(4);
	vbios.stutter_self_refresh_exit_latency = frc_to_fixed(153, 10);
	vbios.nbp_state_change_latency = frc_to_fixed(19649, 1000);
	vbios.mcifwrmc_urgent_latency = int_to_fixed(10);
	vbios.scatter_gather_enable = true;
	vbios.down_spread_percentage = frc_to_fixed(5, 10);
	vbios.cursor_width = int_to_fixed(32);
	vbios.average_compression_rate = int_to_fixed(4);
	vbios.number_of_request_slots_gmc_reserves_for_dmif_per_channel =
		int_to_fixed(256);
	vbios.blackout_duration = int_to_fixed(18); /* us */
	vbios.maximum_blackout_recovery_time = int_to_fixed(20);
	dceip.dmif_request_buffer_size = int_to_fixed(768);
	dceip.de_tiling_buffer = int_to_fixed(0);
	dceip.dcfclk_request_generation = int_to_fixed(0);
	dceip.lines_interleaved_into_lb = int_to_fixed(2);
	dceip.chunk_width = int_to_fixed(256);
	dceip.number_of_graphics_pipes = int_to_fixed(3);
	dceip.number_of_underlay_pipes = int_to_fixed(1);
	dceip.display_write_back_supported = false;
	dceip.argb_compression_support = false;
	dceip.underlay_vscaler_efficiency6_bit_per_component = frc_to_fixed(
		35556, 10000);
	dceip.underlay_vscaler_efficiency8_bit_per_component = frc_to_fixed(
		34286, 10000);
	dceip.underlay_vscaler_efficiency10_bit_per_component = frc_to_fixed(32,
		10);
	dceip.underlay_vscaler_efficiency12_bit_per_component = int_to_fixed(3);
	dceip.graphics_vscaler_efficiency6_bit_per_component = frc_to_fixed(35,
		10);
	dceip.graphics_vscaler_efficiency8_bit_per_component = frc_to_fixed(
		34286, 10000);
	dceip.graphics_vscaler_efficiency10_bit_per_component = frc_to_fixed(32,
		10);
	dceip.graphics_vscaler_efficiency12_bit_per_component = int_to_fixed(3);
	dceip.alpha_vscaler_efficiency = int_to_fixed(3);
	dceip.max_dmif_buffer_allocated = int_to_fixed(2);
	dceip.graphics_dmif_size = int_to_fixed(12288);
	dceip.underlay_luma_dmif_size = int_to_fixed(19456);
	dceip.underlay_chroma_dmif_size = int_to_fixed(23552);
	dceip.pre_downscaler_enabled = true;
	dceip.underlay_downscale_prefetch_enabled = true;
	dceip.lb_write_pixels_per_dispclk = int_to_fixed(1);
	dceip.lb_size_per_component444 = int_to_fixed(82176);
	dceip.graphics_lb_nodownscaling_multi_line_prefetching = false;
	dceip.stutter_and_dram_clock_state_change_gated_before_cursor =
		int_to_fixed(0);
	dceip.underlay420_luma_lb_size_per_component = int_to_fixed(82176);
	dceip.underlay420_chroma_lb_size_per_component = int_to_fixed(164352);
	dceip.underlay422_lb_size_per_component = int_to_fixed(82176);
	dceip.cursor_chunk_width = int_to_fixed(64);
	dceip.cursor_dcp_buffer_lines = int_to_fixed(4);
	dceip.cursor_memory_interface_buffer_pixels = int_to_fixed(64);
	dceip.underlay_maximum_width_efficient_for_tiling = int_to_fixed(1920);
	dceip.underlay_maximum_height_efficient_for_tiling = int_to_fixed(1080);
	dceip.peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
		frc_to_fixed(3, 10);
	dceip.peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
		int_to_fixed(25);
	dceip.minimum_outstanding_pte_request_limit = int_to_fixed(2);
	dceip.maximum_total_outstanding_pte_requests_allowed_by_saw =
		int_to_fixed(128);
	dceip.limit_excessive_outstanding_dmif_requests = true;
	dceip.linear_mode_line_request_alternation_slice = int_to_fixed(64);
	dceip.scatter_gather_lines_of_pte_prefetching_in_linear_mode =
		int_to_fixed(32);
	dceip.display_write_back420_luma_mcifwr_buffer_size = int_to_fixed(
		12288);
	dceip.display_write_back420_chroma_mcifwr_buffer_size = int_to_fixed(
		8192);
	dceip.request_efficiency = frc_to_fixed(8, 10);
	dceip.dispclk_per_request = int_to_fixed(2);
	dceip.dispclk_ramping_factor = frc_to_fixed(11, 10);
	dceip.display_pipe_throughput_factor = frc_to_fixed(105, 100);
	dceip.scatter_gather_pte_request_rows_in_tiling_mode = int_to_fixed(2);
	dceip.mcifwr_all_surfaces_burst_time = int_to_fixed(0); /* todo: this is a bug*/

	*bw_dceip = dceip;
	*bw_vbios = vbios;
}

/**
 * Compare calculated (required) clocks against the clocks available at
 * maximum voltage (max Performance Level).
 */
static bool is_display_configuration_supported(
	const struct bw_calcs_input_vbios *vbios,
	const struct bw_calcs_output *calcs_output)
{
	uint32_t int_max_clk;

	int_max_clk = fixed_to_int(vbios->high_voltage_max_dispclk_mhz);
	int_max_clk *= 1000; /* MHz to kHz */
	if (calcs_output->dispclk_khz > int_max_clk)
		return false;

	int_max_clk = fixed_to_int(vbios->high_sclk_mhz);
	int_max_clk *= 1000; /* MHz to kHz */
	if (calcs_output->required_sclk > int_max_clk)
		return false;

	return true;
}

/**
 * Return:
 *	true -	Display(s) configuration supported.
 *		In this case 'calcs_output' contains data for HW programming
 *	false - Display(s) configuration not supported (not enough bandwidth).
 */

bool bw_calcs(struct dc_context *ctx, const struct bw_calcs_input_dceip *dceip,
	const struct bw_calcs_input_vbios *vbios,
	const struct bw_calcs_input_mode_data *mode_data,
	struct bw_calcs_output *calcs_output)
{
	struct bw_results_internal *bw_results_internal = dc_service_alloc(
				ctx, sizeof(struct bw_results_internal));
	struct bw_calcs_input_mode_data_internal *bw_data_internal =
		dc_service_alloc(
			ctx, sizeof(struct bw_calcs_input_mode_data_internal));
	switch (mode_data->number_of_displays) {
	case (3):
		bw_data_internal->d2_htotal = int_to_fixed(
			mode_data->displays_data[2].h_total);
		bw_data_internal->d2_pixel_rate =
			mode_data->displays_data[2].pixel_rate;
		bw_data_internal->d2_graphics_src_width = int_to_fixed(
			mode_data->displays_data[2].graphics_src_width);
		bw_data_internal->d2_graphics_src_height = int_to_fixed(
			mode_data->displays_data[2].graphics_src_height);
		bw_data_internal->d2_graphics_scale_ratio =
			mode_data->displays_data[2].graphics_scale_ratio;
		bw_data_internal->d2_graphics_stereo_mode =
			mode_data->displays_data[2].graphics_stereo_mode;
	case (2):
		bw_data_internal->d1_display_write_back_dwb_enable = false;
		bw_data_internal->d1_underlay_mode = ul_none;
		bw_data_internal->d1_underlay_scale_ratio = int_to_fixed(0);
		bw_data_internal->d1_htotal = int_to_fixed(
			mode_data->displays_data[1].h_total);
		bw_data_internal->d1_pixel_rate =
			mode_data->displays_data[1].pixel_rate;
		bw_data_internal->d1_graphics_src_width = int_to_fixed(
			mode_data->displays_data[1].graphics_src_width);
		bw_data_internal->d1_graphics_src_height = int_to_fixed(
			mode_data->displays_data[1].graphics_src_height);
		bw_data_internal->d1_graphics_scale_ratio =
			mode_data->displays_data[1].graphics_scale_ratio;
		bw_data_internal->d1_graphics_stereo_mode =
			mode_data->displays_data[1].graphics_stereo_mode;

	case (1):
		bw_data_internal->d0_fbc_enable =
			mode_data->displays_data[0].fbc_enable;
		bw_data_internal->d0_lpt_enable =
			mode_data->displays_data[0].lpt_enable;
		bw_data_internal->d0_underlay_mode =
			mode_data->displays_data[0].underlay_mode;
		bw_data_internal->d0_underlay_scale_ratio = int_to_fixed(0);
		bw_data_internal->d0_htotal = int_to_fixed(
			mode_data->displays_data[0].h_total);
		bw_data_internal->d0_pixel_rate =
			mode_data->displays_data[0].pixel_rate;
		bw_data_internal->d0_graphics_src_width = int_to_fixed(
			mode_data->displays_data[0].graphics_src_width);
		bw_data_internal->d0_graphics_src_height = int_to_fixed(
			mode_data->displays_data[0].graphics_src_height);
		bw_data_internal->d0_graphics_scale_ratio =
			mode_data->displays_data[0].graphics_scale_ratio;
		bw_data_internal->d0_graphics_stereo_mode =
			mode_data->displays_data[0].graphics_stereo_mode;

	default:
		/* data for all displays */
		bw_data_internal->number_of_displays =
			mode_data->number_of_displays;
		bw_data_internal->graphics_rotation_angle = int_to_fixed(
			mode_data->displays_data[0].graphics_rotation_angle);
		bw_data_internal->underlay_rotation_angle = int_to_fixed(
			mode_data->displays_data[0].underlay_rotation_angle);
		bw_data_internal->underlay_surface_type =
			mode_data->displays_data[0].underlay_surface_type;
		bw_data_internal->panning_and_bezel_adjustment =
			mode_data->displays_data[0].panning_and_bezel_adjustment;
		bw_data_internal->graphics_tiling_mode =
			mode_data->displays_data[0].graphics_tiling_mode;
		bw_data_internal->graphics_interlace_mode =
			mode_data->displays_data[0].graphics_interlace_mode;
		bw_data_internal->graphics_bytes_per_pixel = int_to_fixed(
			mode_data->displays_data[0].graphics_bytes_per_pixel);
		bw_data_internal->graphics_htaps = int_to_fixed(
			mode_data->displays_data[0].graphics_h_taps);
		bw_data_internal->graphics_vtaps = int_to_fixed(
			mode_data->displays_data[0].graphics_v_taps);
		bw_data_internal->graphics_lb_bpc = int_to_fixed(
			mode_data->displays_data[0].graphics_lb_bpc);
		bw_data_internal->underlay_lb_bpc = int_to_fixed(
			mode_data->displays_data[0].underlay_lb_bpc);
		bw_data_internal->underlay_tiling_mode =
			mode_data->displays_data[0].underlay_tiling_mode;
		bw_data_internal->underlay_htaps = int_to_fixed(
			mode_data->displays_data[0].underlay_h_taps);
		bw_data_internal->underlay_vtaps = int_to_fixed(
			mode_data->displays_data[0].underlay_v_taps);
		bw_data_internal->underlay_src_width = int_to_fixed(
			mode_data->displays_data[0].underlay_src_width);
		bw_data_internal->underlay_src_height = int_to_fixed(
			mode_data->displays_data[0].underlay_src_height);
		bw_data_internal->underlay_pitch_in_pixels = int_to_fixed(
			mode_data->displays_data[0].underlay_pitch_in_pixels);
		bw_data_internal->underlay_stereo_mode =
			mode_data->displays_data[0].underlay_stereo_mode;
		bw_data_internal->display_synchronization_enabled =
			mode_data->display_synchronization_enabled;
	}

	if (bw_data_internal->number_of_displays != 0) {
		struct bw_fixed high_sclk = vbios->high_sclk_mhz;
		struct bw_fixed mid_sclk = vbios->mid_sclk_mhz;
		struct bw_fixed low_sclk = vbios->low_sclk_mhz;
		struct bw_fixed high_yclk = vbios->high_yclk_mhz;
		struct bw_fixed low_yclk = vbios->low_yclk_mhz;

		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		/* units: nanosecond, 16bit storage. */
		calcs_output->nbp_state_change_wm_ns[0].b_mark =
			mul(bw_results_internal->nbp_state_change_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->nbp_state_change_wm_ns[1].b_mark =
			mul(bw_results_internal->nbp_state_change_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->nbp_state_change_wm_ns[2].b_mark =
			mul(bw_results_internal->nbp_state_change_watermark[6],
					int_to_fixed(1000)).value >> 24;

		calcs_output->stutter_exit_wm_ns[0].b_mark =
			mul(bw_results_internal->stutter_exit_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->stutter_exit_wm_ns[1].b_mark =
			mul(bw_results_internal->stutter_exit_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->stutter_exit_wm_ns[2].b_mark =
			mul(bw_results_internal->stutter_exit_watermark[6],
					int_to_fixed(1000)).value >> 24;

		calcs_output->urgent_wm_ns[0].b_mark =
			mul(bw_results_internal->urgent_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->urgent_wm_ns[1].b_mark =
			mul(bw_results_internal->urgent_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->urgent_wm_ns[2].b_mark =
			mul(bw_results_internal->urgent_watermark[6],
					int_to_fixed(1000)).value >> 24;

		((struct bw_calcs_input_vbios *)vbios)->low_yclk_mhz = high_yclk;
		((struct bw_calcs_input_vbios *)vbios)->low_sclk_mhz = high_sclk;
		((struct bw_calcs_input_vbios *)vbios)->mid_sclk_mhz = high_sclk;

		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		calcs_output->nbp_state_change_wm_ns[0].a_mark =
			mul(bw_results_internal->nbp_state_change_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->nbp_state_change_wm_ns[1].a_mark =
			mul(bw_results_internal->nbp_state_change_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->nbp_state_change_wm_ns[2].a_mark =
			mul(bw_results_internal->nbp_state_change_watermark[6],
					int_to_fixed(1000)).value >> 24;

		calcs_output->stutter_exit_wm_ns[0].a_mark =
			mul(bw_results_internal->stutter_exit_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->stutter_exit_wm_ns[1].a_mark =
			mul(bw_results_internal->stutter_exit_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->stutter_exit_wm_ns[2].a_mark =
			mul(bw_results_internal->stutter_exit_watermark[6],
					int_to_fixed(1000)).value >> 24;

		calcs_output->urgent_wm_ns[0].a_mark =
			mul(bw_results_internal->urgent_watermark[4],
					int_to_fixed(1000)).value >> 24;
		calcs_output->urgent_wm_ns[1].a_mark =
			mul(bw_results_internal->urgent_watermark[5],
					int_to_fixed(1000)).value >> 24;
		calcs_output->urgent_wm_ns[2].a_mark =
			mul(bw_results_internal->urgent_watermark[6],
					int_to_fixed(1000)).value >> 24;

		calcs_output->nbp_state_change_enable =
			bw_results_internal->nbp_state_change_enable;
		calcs_output->cpuc_state_change_enable =
				bw_results_internal->cpuc_state_change_enable;
		calcs_output->cpup_state_change_enable =
				bw_results_internal->cpup_state_change_enable;
		calcs_output->stutter_mode_enable =
				bw_results_internal->stutter_mode_enable;
		calcs_output->dispclk_khz =
				mul(bw_results_internal->dispclk,
					int_to_fixed(1000)).value >> 24;
		/*TODO:fix formula to unhardcode use levels*/
		calcs_output->required_blackout_duration_us =
			add(bw_results_internal->blackout_duration_margin[2][2],
					vbios->blackout_duration).value >> 24;
		calcs_output->required_sclk =
			mul(bw_results_internal->required_sclk,
					int_to_fixed(1000)).value >> 24;
		calcs_output->required_sclk_deep_sleep =
			mul(bw_results_internal->sclk_deep_sleep,
					int_to_fixed(1000)).value >> 24;
		/*TODO:fix formula to unhardcode use levels*/
		calcs_output->required_yclk =
				mul(high_yclk, int_to_fixed(1000)).value >> 24;

		((struct bw_calcs_input_vbios *)vbios)->low_yclk_mhz = low_yclk;
		((struct bw_calcs_input_vbios *)vbios)->low_sclk_mhz = low_sclk;
		((struct bw_calcs_input_vbios *)vbios)->mid_sclk_mhz = mid_sclk;
	} else {
		calcs_output->nbp_state_change_enable = true;
		calcs_output->cpuc_state_change_enable = true;
		calcs_output->cpup_state_change_enable = true;
		calcs_output->stutter_mode_enable = true;
		calcs_output->dispclk_khz = 0;
		calcs_output->required_sclk = 0;
	}

	dc_service_free(ctx, bw_data_internal);
	dc_service_free(ctx, bw_results_internal);

	return is_display_configuration_supported(vbios, calcs_output);
}
