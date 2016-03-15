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

#include "dm_services.h"
#include "bandwidth_calcs.h"

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

static void calculate_bandwidth(
	const struct bw_calcs_dceip *dceip,
	const struct bw_calcs_vbios *vbios,
	const struct bw_calcs_mode_data_internal *mode_data,
	struct bw_calcs_results *results)

{
	const int32_t pixels_per_chunk = 512;
	const int32_t max_chunks_non_fbc_mode = 16;
	const int32_t high = 2;
	const int32_t mid = 1;
	const int32_t low = 0;

	int32_t i, j, k;
	struct bw_fixed yclk[3];
	struct bw_fixed sclk[3];
	bool d0_underlay_enable;
	bool d1_underlay_enable;
	enum bw_defines sclk_message;
	enum bw_defines yclk_message;
	enum bw_defines v_filter_init_mode[maximum_number_of_surfaces];
	enum bw_defines tiling_mode[maximum_number_of_surfaces];
	enum bw_defines stereo_mode[maximum_number_of_surfaces];
	enum bw_defines surface_type[maximum_number_of_surfaces];
	enum bw_defines voltage;
	enum bw_defines pipe_check;
	enum bw_defines hsr_check;
	enum bw_defines vsr_check;
	enum bw_defines lb_size_check;
	enum bw_defines fbc_check;
	enum bw_defines rotation_check;
	enum bw_defines mode_check;

	yclk[low] = vbios->low_yclk;
	yclk[mid] = vbios->high_yclk;
	yclk[high] = vbios->high_yclk;
	sclk[low] = vbios->low_sclk;
	sclk[mid] = vbios->mid_sclk;
	sclk[high] = vbios->high_sclk;
	/* surface assignment:*/
	/* 0: d0 underlay or underlay luma*/
	/* 1: d0 underlay chroma*/
	/* 2: d1 underlay or underlay luma*/
	/* 3: d1 underlay chroma*/
	/* 4: d0 graphics*/
	/* 5: d1 graphics*/
	/* 6: d2 graphics*/
	/* 7: d3 graphics, same mode as d2*/
	/* 8: d4 graphics, same mode as d2*/
	/* 9: d5 graphics, same mode as d2*/
	/* ...*/
	/* maximum_number_of_surfaces-2: d1 display_write_back420 luma*/
	/* maximum_number_of_surfaces-1: d1 display_write_back420 chroma*/
	/* underlay luma and chroma surface parameters from spreadsheet*/
	if (mode_data->d0_underlay_mode == bw_def_none) {
		d0_underlay_enable = 0;
	} else {
		d0_underlay_enable = 1;
	}
	if (mode_data->d1_underlay_mode == bw_def_none) {
		d1_underlay_enable = 0;
	} else {
		d1_underlay_enable = 1;
	}
	results->number_of_underlay_surfaces = d0_underlay_enable
		+ d1_underlay_enable;
	switch (mode_data->underlay_surface_type) {
	case bw_def_420:
		surface_type[0] = bw_def_underlay420_luma;
		surface_type[2] = bw_def_underlay420_luma;
		results->bytes_per_pixel[0] = 1;
		results->bytes_per_pixel[2] = 1;
		surface_type[1] = bw_def_underlay420_chroma;
		surface_type[3] = bw_def_underlay420_chroma;
		results->bytes_per_pixel[1] = 2;
		results->bytes_per_pixel[3] = 2;
		results->lb_size_per_component[0] =
			dceip->underlay420_luma_lb_size_per_component;
		results->lb_size_per_component[1] =
			dceip->underlay420_chroma_lb_size_per_component;
		results->lb_size_per_component[2] =
			dceip->underlay420_luma_lb_size_per_component;
		results->lb_size_per_component[3] =
			dceip->underlay420_chroma_lb_size_per_component;
		break;
	case bw_def_422:
		surface_type[0] = bw_def_underlay422;
		surface_type[2] = bw_def_underlay422;
		results->bytes_per_pixel[0] = 2;
		results->bytes_per_pixel[2] = 2;
		results->lb_size_per_component[0] =
			dceip->underlay422_lb_size_per_component;
		results->lb_size_per_component[2] =
			dceip->underlay422_lb_size_per_component;
		break;
	default:
		surface_type[0] = bw_def_underlay444;
		surface_type[2] = bw_def_underlay444;
		results->bytes_per_pixel[0] = 4;
		results->bytes_per_pixel[2] = 4;
		results->lb_size_per_component[0] =
			dceip->lb_size_per_component444;
		results->lb_size_per_component[2] =
			dceip->lb_size_per_component444;
		break;
	}
	if (d0_underlay_enable) {
		switch (mode_data->underlay_surface_type) {
		case bw_def_420:
			results->enable[0] = 1;
			results->enable[1] = 1;
			break;
		default:
			results->enable[0] = 1;
			results->enable[1] = 0;
			break;
		}
	} else {
		results->enable[0] = 0;
		results->enable[1] = 0;
	}
	if (d1_underlay_enable) {
		switch (mode_data->underlay_surface_type) {
		case bw_def_420:
			results->enable[2] = 1;
			results->enable[3] = 1;
			break;
		default:
			results->enable[2] = 1;
			results->enable[3] = 0;
			break;
		}
	} else {
		results->enable[2] = 0;
		results->enable[3] = 0;
	}
	results->use_alpha[0] = 0;
	results->use_alpha[1] = 0;
	results->use_alpha[2] = 0;
	results->use_alpha[3] = 0;
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
	results->h_total[0] = bw_int_to_fixed(mode_data->d0_htotal);
	results->h_total[1] = bw_int_to_fixed(mode_data->d0_htotal);
	results->h_total[2] = bw_int_to_fixed(mode_data->d1_htotal);
	results->h_total[3] = bw_int_to_fixed(mode_data->d1_htotal);
	results->pixel_rate[0] = mode_data->d0_pixel_rate;
	results->pixel_rate[1] = mode_data->d0_pixel_rate;
	results->pixel_rate[2] = mode_data->d1_pixel_rate;
	results->pixel_rate[3] = mode_data->d1_pixel_rate;
	results->src_width[0] = bw_int_to_fixed(mode_data->underlay_src_width);
	results->src_width[1] = bw_int_to_fixed(mode_data->underlay_src_width);
	results->src_width[2] = bw_int_to_fixed(mode_data->underlay_src_width);
	results->src_width[3] = bw_int_to_fixed(mode_data->underlay_src_width);
	results->src_height[0] = bw_int_to_fixed(
		mode_data->underlay_src_height);
	results->src_height[1] = bw_int_to_fixed(
		mode_data->underlay_src_height);
	results->src_height[2] = bw_int_to_fixed(
		mode_data->underlay_src_height);
	results->src_height[3] = bw_int_to_fixed(
		mode_data->underlay_src_height);
	results->pitch_in_pixels[0] = bw_int_to_fixed(
		mode_data->underlay_pitch_in_pixels);
	results->pitch_in_pixels[1] = bw_int_to_fixed(
		mode_data->underlay_pitch_in_pixels);
	results->pitch_in_pixels[2] = bw_int_to_fixed(
		mode_data->underlay_pitch_in_pixels);
	results->pitch_in_pixels[3] = bw_int_to_fixed(
		mode_data->underlay_pitch_in_pixels);
	results->scale_ratio[0] = mode_data->d0_underlay_scale_ratio;
	results->scale_ratio[1] = mode_data->d0_underlay_scale_ratio;
	results->scale_ratio[2] = mode_data->d1_underlay_scale_ratio;
	results->scale_ratio[3] = mode_data->d1_underlay_scale_ratio;
	results->h_taps[0] = bw_int_to_fixed(mode_data->underlay_htaps);
	results->h_taps[1] = bw_int_to_fixed(mode_data->underlay_htaps);
	results->h_taps[2] = bw_int_to_fixed(mode_data->underlay_htaps);
	results->h_taps[3] = bw_int_to_fixed(mode_data->underlay_htaps);
	results->v_taps[0] = bw_int_to_fixed(mode_data->underlay_vtaps);
	results->v_taps[1] = bw_int_to_fixed(mode_data->underlay_vtaps);
	results->v_taps[2] = bw_int_to_fixed(mode_data->underlay_vtaps);
	results->v_taps[3] = bw_int_to_fixed(mode_data->underlay_vtaps);
	results->rotation_angle[0] = bw_int_to_fixed(
		mode_data->underlay_rotation_angle);
	results->rotation_angle[1] = bw_int_to_fixed(
		mode_data->underlay_rotation_angle);
	results->rotation_angle[2] = bw_int_to_fixed(
		mode_data->underlay_rotation_angle);
	results->rotation_angle[3] = bw_int_to_fixed(
		mode_data->underlay_rotation_angle);
	if (mode_data->underlay_tiling_mode == bw_def_linear) {
		tiling_mode[0] = bw_def_linear;
		tiling_mode[1] = bw_def_linear;
		tiling_mode[2] = bw_def_linear;
		tiling_mode[3] = bw_def_linear;
	} else {
		tiling_mode[0] = bw_def_landscape;
		tiling_mode[1] = bw_def_landscape;
		tiling_mode[2] = bw_def_landscape;
		tiling_mode[3] = bw_def_landscape;
	}
	stereo_mode[0] = mode_data->underlay_stereo_mode;
	stereo_mode[1] = mode_data->underlay_stereo_mode;
	stereo_mode[2] = mode_data->underlay_stereo_mode;
	stereo_mode[3] = mode_data->underlay_stereo_mode;
	results->lb_bpc[0] = mode_data->underlay_lb_bpc;
	results->lb_bpc[1] = mode_data->underlay_lb_bpc;
	results->lb_bpc[2] = mode_data->underlay_lb_bpc;
	results->lb_bpc[3] = mode_data->underlay_lb_bpc;
	results->compression_rate[0] = bw_int_to_fixed(1);
	results->compression_rate[1] = bw_int_to_fixed(1);
	results->compression_rate[2] = bw_int_to_fixed(1);
	results->compression_rate[3] = bw_int_to_fixed(1);
	results->access_one_channel_only[0] = 0;
	results->access_one_channel_only[1] = 0;
	results->access_one_channel_only[2] = 0;
	results->access_one_channel_only[3] = 0;
	results->cursor_width_pixels[0] = bw_int_to_fixed(0);
	results->cursor_width_pixels[1] = bw_int_to_fixed(0);
	results->cursor_width_pixels[2] = bw_int_to_fixed(0);
	results->cursor_width_pixels[3] = bw_int_to_fixed(0);
	/* graphics surface parameters from spreadsheet*/
	for (i = 4; i <= maximum_number_of_surfaces - 3; i++) {
		if (i < mode_data->number_of_displays + 4) {
			if (i == 4
				&& mode_data->d0_underlay_mode
					== bw_def_underlay_only) {
				results->enable[i] = 0;
				results->use_alpha[i] = 0;
			} else if (i == 4
				&& mode_data->d0_underlay_mode
					== bw_def_blend) {
				results->enable[i] = 1;
				results->use_alpha[i] = 1;
			} else if (i == 4) {
				results->enable[i] = 1;
				results->use_alpha[i] = 0;
			} else if (i == 5
				&& mode_data->d1_underlay_mode
					== bw_def_underlay_only) {
				results->enable[i] = 0;
				results->use_alpha[i] = 0;
			} else if (i == 5
				&& mode_data->d1_underlay_mode
					== bw_def_blend) {
				results->enable[i] = 1;
				results->use_alpha[i] = 1;
			} else {
				results->enable[i] = 1;
				results->use_alpha[i] = 0;
			}
		} else {
			results->enable[i] = 0;
			results->use_alpha[i] = 0;
		}
		results->scatter_gather_enable_for_pipe[i] =
			vbios->scatter_gather_enable;
		surface_type[i] = bw_def_graphics;
		results->lb_size_per_component[i] =
			dceip->lb_size_per_component444;
		results->bytes_per_pixel[i] =
			mode_data->graphics_bytes_per_pixel;
		results->interlace_mode[i] = mode_data->graphics_interlace_mode;
		results->h_taps[i] = bw_int_to_fixed(mode_data->graphics_htaps);
		results->v_taps[i] = bw_int_to_fixed(mode_data->graphics_vtaps);
		results->rotation_angle[i] = bw_int_to_fixed(
			mode_data->graphics_rotation_angle);
		if (mode_data->graphics_tiling_mode == bw_def_linear) {
			tiling_mode[i] = bw_def_linear;
		} else if (mode_data->graphics_rotation_angle == 0
			|| mode_data->graphics_rotation_angle == 180) {
			tiling_mode[i] = bw_def_landscape;
		} else {
			tiling_mode[i] = bw_def_portrait;
		}
		results->lb_bpc[i] = mode_data->graphics_lb_bpc;
		if (i == 4) {
			if (mode_data->d0_fbc_enable
				&& (dceip->argb_compression_support
					|| mode_data->d0_underlay_mode
						!= bw_def_blended)) {
				results->compression_rate[i] = bw_int_to_fixed(
					vbios->average_compression_rate);
				results->access_one_channel_only[i] =
					mode_data->d0_lpt_enable;
			} else {
				results->compression_rate[i] = bw_int_to_fixed(
					1);
				results->access_one_channel_only[i] = 0;
			}
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d0_htotal);
			results->pixel_rate[i] = mode_data->d0_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d0_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d0_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d0_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d0_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d0_graphics_stereo_mode;
		} else if (i == 5) {
			results->compression_rate[i] = bw_int_to_fixed(1);
			results->access_one_channel_only[i] = 0;
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d1_htotal);
			results->pixel_rate[i] = mode_data->d1_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d1_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d1_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d1_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d1_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d1_graphics_stereo_mode;
		} else if (i == 6) {
			results->compression_rate[i] = bw_int_to_fixed(1);
			results->access_one_channel_only[i] = 0;
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d2_htotal);
			results->pixel_rate[i] = mode_data->d2_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d2_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d2_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d2_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d2_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d2_graphics_stereo_mode;
		} else if (i == 7) {
			results->compression_rate[i] = bw_int_to_fixed(1);
			results->access_one_channel_only[i] = 0;
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d3_htotal);
			results->pixel_rate[i] = mode_data->d3_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d3_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d3_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d3_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d3_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d3_graphics_stereo_mode;
		} else if (i == 8) {
			results->compression_rate[i] = bw_int_to_fixed(1);
			results->access_one_channel_only[i] = 0;
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d4_htotal);
			results->pixel_rate[i] = mode_data->d4_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d4_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d4_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d4_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d4_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d4_graphics_stereo_mode;
		} else {
			results->compression_rate[i] = bw_int_to_fixed(1);
			results->access_one_channel_only[i] = 0;
			results->h_total[i] = bw_int_to_fixed(
				mode_data->d5_htotal);
			results->pixel_rate[i] = mode_data->d5_pixel_rate;
			results->src_width[i] = bw_int_to_fixed(
				mode_data->d5_graphics_src_width);
			results->src_height[i] = bw_int_to_fixed(
				mode_data->d5_graphics_src_height);
			results->pitch_in_pixels[i] = bw_int_to_fixed(
				mode_data->d5_graphics_src_width);
			results->scale_ratio[i] =
				mode_data->d5_graphics_scale_ratio;
			stereo_mode[i] = mode_data->d5_graphics_stereo_mode;
		}
		results->cursor_width_pixels[i] = bw_int_to_fixed(
			vbios->cursor_width);
	}
	/* display_write_back420*/
	results->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 2] =
		0;
	results->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 1] =
		0;
	if (mode_data->d1_display_write_back_dwb_enable == 1) {
		results->enable[maximum_number_of_surfaces - 2] = 1;
		results->enable[maximum_number_of_surfaces - 1] = 1;
	} else {
		results->enable[maximum_number_of_surfaces - 2] = 0;
		results->enable[maximum_number_of_surfaces - 1] = 0;
	}
	surface_type[maximum_number_of_surfaces - 2] =
		bw_def_display_write_back420_luma;
	surface_type[maximum_number_of_surfaces - 1] =
		bw_def_display_write_back420_chroma;
	results->lb_size_per_component[maximum_number_of_surfaces - 2] =
		dceip->underlay420_luma_lb_size_per_component;
	results->lb_size_per_component[maximum_number_of_surfaces - 1] =
		dceip->underlay420_chroma_lb_size_per_component;
	results->bytes_per_pixel[maximum_number_of_surfaces - 2] = 1;
	results->bytes_per_pixel[maximum_number_of_surfaces - 1] = 2;
	results->interlace_mode[maximum_number_of_surfaces - 2] =
		mode_data->graphics_interlace_mode;
	results->interlace_mode[maximum_number_of_surfaces - 1] =
		mode_data->graphics_interlace_mode;
	results->h_taps[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	results->h_taps[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	results->v_taps[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	results->v_taps[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	results->rotation_angle[maximum_number_of_surfaces - 2] =
		bw_int_to_fixed(0);
	results->rotation_angle[maximum_number_of_surfaces - 1] =
		bw_int_to_fixed(0);
	tiling_mode[maximum_number_of_surfaces - 2] = bw_def_linear;
	tiling_mode[maximum_number_of_surfaces - 1] = bw_def_linear;
	results->lb_bpc[maximum_number_of_surfaces - 2] = 8;
	results->lb_bpc[maximum_number_of_surfaces - 1] = 8;
	results->compression_rate[maximum_number_of_surfaces - 2] =
		bw_int_to_fixed(1);
	results->compression_rate[maximum_number_of_surfaces - 1] =
		bw_int_to_fixed(1);
	results->access_one_channel_only[maximum_number_of_surfaces - 2] = 0;
	results->access_one_channel_only[maximum_number_of_surfaces - 1] = 0;
	results->h_total[maximum_number_of_surfaces - 2] = bw_int_to_fixed(
		mode_data->d1_htotal);
	results->h_total[maximum_number_of_surfaces - 1] = bw_int_to_fixed(
		mode_data->d1_htotal);
	results->pixel_rate[maximum_number_of_surfaces - 2] =
		mode_data->d1_pixel_rate;
	results->pixel_rate[maximum_number_of_surfaces - 1] =
		mode_data->d1_pixel_rate;
	results->src_width[maximum_number_of_surfaces - 2] = bw_int_to_fixed(
		mode_data->d1_graphics_src_width);
	results->src_width[maximum_number_of_surfaces - 1] = bw_int_to_fixed(
		mode_data->d1_graphics_src_width);
	results->src_height[maximum_number_of_surfaces - 2] = bw_int_to_fixed(
		mode_data->d1_graphics_src_height);
	results->src_height[maximum_number_of_surfaces - 1] = bw_int_to_fixed(
		mode_data->d1_graphics_src_height);
	results->pitch_in_pixels[maximum_number_of_surfaces - 2] =
		bw_int_to_fixed(mode_data->d1_graphics_src_width);
	results->pitch_in_pixels[maximum_number_of_surfaces - 1] =
		bw_int_to_fixed(mode_data->d1_graphics_src_width);
	results->scale_ratio[maximum_number_of_surfaces - 2] = bw_int_to_fixed(
		1);
	results->scale_ratio[maximum_number_of_surfaces - 1] = bw_int_to_fixed(
		1);
	stereo_mode[maximum_number_of_surfaces - 2] = bw_def_mono;
	stereo_mode[maximum_number_of_surfaces - 1] = bw_def_mono;
	results->cursor_width_pixels[maximum_number_of_surfaces - 2] =
		bw_int_to_fixed(0);
	results->cursor_width_pixels[maximum_number_of_surfaces - 1] =
		bw_int_to_fixed(0);
	results->use_alpha[maximum_number_of_surfaces - 2] = 0;
	results->use_alpha[maximum_number_of_surfaces - 1] = 0;
	/*mode check calculations:*/
	/* mode within dce ip capabilities*/
	/* fbc*/
	/* hsr*/
	/* vsr*/
	/* lb size*/
	/*effective scaling source and ratios:*/
	/*for graphics, non-stereo, non-interlace surfaces when the size of the source and destination are the same, only one tap is used*/
	/*420 chroma has half the width, height, horizontal and vertical scaling ratios than luma*/
	/*rotating an underlay surface swaps the width, height, horizontal and vertical scaling ratios*/
	/*in top-bottom stereo mode there is 2:1 vertical downscaling for each eye*/
	/*in side-by-side stereo mode there is 2:1 horizontal downscaling for each eye*/
	/*in interlace mode there is 2:1 vertical downscaling for each field*/
	/*in panning or bezel adjustment mode the source width has an extra 128 pixels*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_equ(results->scale_ratio[i], bw_int_to_fixed(1))
				&& surface_type[i] == bw_def_graphics
				&& stereo_mode[i] == bw_def_mono
				&& results->interlace_mode[i] == 0) {
				results->h_taps[i] = bw_int_to_fixed(1);
				results->v_taps[i] = bw_int_to_fixed(1);
			}
			if (surface_type[i]
				== bw_def_display_write_back420_chroma
				|| surface_type[i]
					== bw_def_underlay420_chroma) {
				results->pitch_in_pixels_after_surface_type[i] =
					bw_div(
						results->pitch_in_pixels[i],
						bw_int_to_fixed(2));
				results->src_width_after_surface_type = bw_div(
					results->src_width[i],
					bw_int_to_fixed(2));
				results->src_height_after_surface_type = bw_div(
					results->src_height[i],
					bw_int_to_fixed(2));
				results->hsr_after_surface_type = bw_div(
					results->scale_ratio[i],
					bw_int_to_fixed(2));
				results->vsr_after_surface_type = bw_div(
					results->scale_ratio[i],
					bw_int_to_fixed(2));
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
			if ((bw_equ(
				results->rotation_angle[i],
				bw_int_to_fixed(90))
				|| bw_equ(
					results->rotation_angle[i],
					bw_int_to_fixed(270)))
				&& surface_type[i] != bw_def_graphics) {
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
			switch (stereo_mode[i]) {
			case bw_def_top_bottom:
				results->source_width_pixels[i] =
					results->src_width_after_rotation;
				results->source_height_pixels = bw_mul(
					bw_int_to_fixed(2),
					results->src_height_after_rotation);
				results->hsr_after_stereo =
					results->hsr_after_rotation;
				results->vsr_after_stereo = bw_mul(
					bw_int_to_fixed(1),
					results->vsr_after_rotation);
				break;
			case bw_def_side_by_side:
				results->source_width_pixels[i] = bw_mul(
					bw_int_to_fixed(2),
					results->src_width_after_rotation);
				results->source_height_pixels =
					results->src_height_after_rotation;
				results->hsr_after_stereo = bw_mul(
					bw_int_to_fixed(1),
					results->hsr_after_rotation);
				results->vsr_after_stereo =
					results->vsr_after_rotation;
				break;
			default:
				results->source_width_pixels[i] =
					results->src_width_after_rotation;
				results->source_height_pixels =
					results->src_height_after_rotation;
				results->hsr_after_stereo =
					results->hsr_after_rotation;
				results->vsr_after_stereo =
					results->vsr_after_rotation;
				break;
			}
			results->hsr[i] = results->hsr_after_stereo;
			if (results->interlace_mode[i]) {
				results->vsr[i] = bw_mul(
					results->vsr_after_stereo,
					bw_int_to_fixed(2));
			} else {
				results->vsr[i] = results->vsr_after_stereo;
			}
			if (mode_data->panning_and_bezel_adjustment
				!= bw_def_none) {
				results->source_width_rounded_up_to_chunks[i] =
					bw_add(
						bw_floor2(
							bw_sub(
								results->source_width_pixels[i],
								bw_int_to_fixed(
									1)),
							bw_int_to_fixed(128)),
						bw_int_to_fixed(256));
			} else {
				results->source_width_rounded_up_to_chunks[i] =
					bw_ceil2(
						results->source_width_pixels[i],
						bw_int_to_fixed(128));
			}
			results->source_height_rounded_up_to_chunks[i] =
				results->source_height_pixels;
		}
	}
	/*mode support checks:*/
	/*the number of graphics and underlay pipes is limited by the ip support*/
	/*maximum horizontal and vertical scale ratio is 4, and should not exceed the number of taps*/
	/*for downscaling with the pre-downscaler, the horizontal scale ratio must be more than the ceiling of one quarter of the number of taps*/
	/*the pre-downscaler reduces the line buffer source by the horizontal scale ratio*/
	/*the number of lines in the line buffer has to exceed the number of vertical taps*/
	/*the size of the line in the line buffer is the product of the source width and the bits per component, rounded up to a multiple of 48*/
	/*the size of the line in the line buffer in the case of 10 bit per component is the product of the source width rounded up to multiple of 8 and 30.023438 / 3, rounded up to a multiple of 48*/
	/*the size of the line in the line buffer in the case of 8 bit per component is the product of the source width rounded up to multiple of 8 and 30.023438 / 3, rounded up to a multiple of 48*/
	/*frame buffer compression is not supported with stereo mode, rotation, or non- 888 formats*/
	/*rotation is not supported with linear of stereo modes*/
	if (dceip->number_of_graphics_pipes >= mode_data->number_of_displays
		&& dceip->number_of_underlay_pipes
			>= results->number_of_underlay_surfaces
		&& !(dceip->display_write_back_supported == 0
			&& mode_data->d1_display_write_back_dwb_enable == 1)) {
		pipe_check = bw_def_ok;
	} else {
		pipe_check = bw_def_notok;
	}
	hsr_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_neq(results->hsr[i], bw_int_to_fixed(1))) {
				if (bw_mtn(
					results->hsr[i],
					bw_int_to_fixed(4))) {
					hsr_check = bw_def_hsr_mtn_4;
				} else {
					if (bw_mtn(
						results->hsr[i],
						results->h_taps[i])) {
						hsr_check =
							bw_def_hsr_mtn_h_taps;
					} else {
						if (dceip->pre_downscaler_enabled
							== 1
							&& bw_mtn(
								results->hsr[i],
								bw_int_to_fixed(
									1))
							&& bw_leq(
								results->hsr[i],
								bw_ceil2(
									bw_div(
										results->h_taps[i],
										bw_int_to_fixed(
											4)),
									bw_int_to_fixed(
										1)))) {
							hsr_check =
								bw_def_ceiling__h_taps_div_4___meq_hsr;
						}
					}
				}
			}
		}
	}
	vsr_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_neq(results->vsr[i], bw_int_to_fixed(1))) {
				if (bw_mtn(
					results->vsr[i],
					bw_int_to_fixed(4))) {
					vsr_check = bw_def_vsr_mtn_4;
				} else {
					if (bw_mtn(
						results->vsr[i],
						results->v_taps[i])) {
						vsr_check =
							bw_def_vsr_mtn_v_taps;
					}
				}
			}
		}
	}
	lb_size_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if ((dceip->pre_downscaler_enabled
				&& bw_mtn(results->hsr[i], bw_int_to_fixed(1)))) {
				results->source_width_in_lb = bw_div(
					results->source_width_pixels[i],
					results->hsr[i]);
			} else {
				results->source_width_in_lb =
					results->source_width_pixels[i];
			}
			switch (results->lb_bpc[i]) {
			case 8:
				results->lb_line_pitch =
					bw_ceil2(
						bw_mul(
							bw_div(
								bw_frc_to_fixed(
									2401171875ULL,
									100000000),
								bw_int_to_fixed(
									3)),
							bw_ceil2(
								results->source_width_in_lb,
								bw_int_to_fixed(
									8))),
						bw_int_to_fixed(48));
				break;
			case 10:
				results->lb_line_pitch =
					bw_ceil2(
						bw_mul(
							bw_div(
								bw_frc_to_fixed(
									300234375,
									10000000),
								bw_int_to_fixed(
									3)),
							bw_ceil2(
								results->source_width_in_lb,
								bw_int_to_fixed(
									8))),
						bw_int_to_fixed(48));
				break;
			default:
				results->lb_line_pitch = bw_ceil2(
					bw_mul(
						bw_int_to_fixed(
							results->lb_bpc[i]),
						results->source_width_in_lb),
					bw_int_to_fixed(48));
				break;
			}
			results->lb_partitions[i] = bw_floor2(
				bw_div(
					results->lb_size_per_component[i],
					results->lb_line_pitch),
				bw_int_to_fixed(1));
			/*clamp the partitions to the maxium number supported by the lb*/
			if ((surface_type[i] != bw_def_graphics
				|| dceip->graphics_lb_nodownscaling_multi_line_prefetching
					== 1)) {
				results->lb_partitions_max[i] = bw_int_to_fixed(
					10);
			} else {
				results->lb_partitions_max[i] = bw_int_to_fixed(
					7);
			}
			results->lb_partitions[i] = bw_min2(
				results->lb_partitions_max[i],
				results->lb_partitions[i]);
			if (bw_mtn(
				bw_add(results->v_taps[i], bw_int_to_fixed(1)),
				results->lb_partitions[i])) {
				lb_size_check = bw_def_notok;
			}
		}
	}
	if (mode_data->d0_fbc_enable
		&& (mode_data->graphics_rotation_angle == 90
			|| mode_data->graphics_rotation_angle == 270
			|| mode_data->d0_graphics_stereo_mode != bw_def_mono
			|| mode_data->graphics_bytes_per_pixel != 4)) {
		fbc_check = bw_def_invalid_rotation_or_bpp_or_stereo;
	} else {
		fbc_check = bw_def_ok;
	}
	rotation_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if ((bw_equ(
				results->rotation_angle[i],
				bw_int_to_fixed(90))
				|| bw_equ(
					results->rotation_angle[i],
					bw_int_to_fixed(270)))
				&& (tiling_mode[i] == bw_def_linear
					|| stereo_mode[i] != bw_def_mono)) {
				rotation_check =
					bw_def_invalid_linear_or_stereo_mode;
			}
		}
	}
	if (pipe_check == bw_def_ok && hsr_check == bw_def_ok
		&& vsr_check == bw_def_ok && lb_size_check == bw_def_ok
		&& fbc_check == bw_def_ok && rotation_check == bw_def_ok) {
		mode_check = bw_def_ok;
	} else {
		mode_check = bw_def_notok;
	}
	/*memory request size and latency hiding:*/
	/*request size is normally 64 byte, 2-line interleaved, with full latency hiding*/
	/*the display write-back requests are single line*/
	/*for tiled graphics surfaces, or undelay surfaces with width higher than the maximum size for full efficiency, request size is 32 byte in 8 and 16 bpp or if the rotation is orthogonal to the tiling grain. only half is useful of the bytes in the request size in 8 bpp or in 32 bpp if the rotation is orthogonal to the tiling grain.*/
	/*for undelay surfaces with width lower than the maximum size for full efficiency, requests are 4-line interleaved in 16bpp if the rotation is parallel to the tiling grain, and 8-line interleaved with 4-line latency hiding in 8bpp or if the rotation is orthogonal to the tiling grain.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if ((bw_equ(
				results->rotation_angle[i],
				bw_int_to_fixed(90))
				|| bw_equ(
					results->rotation_angle[i],
					bw_int_to_fixed(270)))) {
				if ((tiling_mode[i] == bw_def_portrait)) {
					results->orthogonal_rotation[i] = 0;
				} else {
					results->orthogonal_rotation[i] = 1;
				}
			} else {
				if ((tiling_mode[i] == bw_def_portrait)) {
					results->orthogonal_rotation[i] = 1;
				} else {
					results->orthogonal_rotation[i] = 0;
				}
			}
			if (bw_equ(
				results->rotation_angle[i],
				bw_int_to_fixed(90))
				|| bw_equ(
					results->rotation_angle[i],
					bw_int_to_fixed(270))) {
				results->underlay_maximum_source_efficient_for_tiling =
					dceip->underlay_maximum_height_efficient_for_tiling;
			} else {
				results->underlay_maximum_source_efficient_for_tiling =
					dceip->underlay_maximum_width_efficient_for_tiling;
			}
			if (bw_equ(
				dceip->de_tiling_buffer,
				bw_int_to_fixed(0))) {
				if (surface_type[i]
					== bw_def_display_write_back420_luma
					|| surface_type[i]
						== bw_def_display_write_back420_chroma) {
					results->bytes_per_request[i] =
						bw_int_to_fixed(64);
					results->useful_bytes_per_request[i] =
						bw_int_to_fixed(64);
					results->lines_interleaved_in_mem_access[i] =
						bw_int_to_fixed(1);
					results->latency_hiding_lines[i] =
						bw_int_to_fixed(1);
				} else if (tiling_mode[i] == bw_def_linear) {
					results->bytes_per_request[i] =
						bw_int_to_fixed(64);
					results->useful_bytes_per_request[i] =
						bw_int_to_fixed(64);
					results->lines_interleaved_in_mem_access[i] =
						bw_int_to_fixed(2);
					results->latency_hiding_lines[i] =
						bw_int_to_fixed(2);
				} else {
					if (surface_type[i] == bw_def_graphics
						|| (bw_mtn(
							results->source_width_rounded_up_to_chunks[i],
							bw_ceil2(
								results->underlay_maximum_source_efficient_for_tiling,
								bw_int_to_fixed(
									256))))) {
						switch (results->bytes_per_pixel[i]) {
						case 8:
							results->lines_interleaved_in_mem_access[i] =
								bw_int_to_fixed(
									2);
							results->latency_hiding_lines[i] =
								bw_int_to_fixed(
									2);
							if (results->orthogonal_rotation[i]) {
								results->bytes_per_request[i] =
									bw_int_to_fixed(
										32);
								results->useful_bytes_per_request[i] =
									bw_int_to_fixed(
										32);
							} else {
								results->bytes_per_request[i] =
									bw_int_to_fixed(
										64);
								results->useful_bytes_per_request[i] =
									bw_int_to_fixed(
										64);
							}
							break;
						case 4:
							if (results->orthogonal_rotation[i]) {
								results->lines_interleaved_in_mem_access[i] =
									bw_int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									bw_int_to_fixed(
										2);
								results->bytes_per_request[i] =
									bw_int_to_fixed(
										32);
								results->useful_bytes_per_request[i] =
									bw_int_to_fixed(
										16);
							} else {
								results->lines_interleaved_in_mem_access[i] =
									bw_int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									bw_int_to_fixed(
										2);
								results->bytes_per_request[i] =
									bw_int_to_fixed(
										64);
								results->useful_bytes_per_request[i] =
									bw_int_to_fixed(
										64);
							}
							break;
						case 2:
							results->lines_interleaved_in_mem_access[i] =
								bw_int_to_fixed(
									2);
							results->latency_hiding_lines[i] =
								bw_int_to_fixed(
									2);
							results->bytes_per_request[i] =
								bw_int_to_fixed(
									32);
							results->useful_bytes_per_request[i] =
								bw_int_to_fixed(
									32);
							break;
						default:
							results->lines_interleaved_in_mem_access[i] =
								bw_int_to_fixed(
									2);
							results->latency_hiding_lines[i] =
								bw_int_to_fixed(
									2);
							results->bytes_per_request[i] =
								bw_int_to_fixed(
									32);
							results->useful_bytes_per_request[i] =
								bw_int_to_fixed(
									16);
							break;
						}
					} else {
						results->bytes_per_request[i] =
							bw_int_to_fixed(64);
						results->useful_bytes_per_request[i] =
							bw_int_to_fixed(64);
						if (results->orthogonal_rotation[i]) {
							results->lines_interleaved_in_mem_access[i] =
								bw_int_to_fixed(
									8);
							results->latency_hiding_lines[i] =
								bw_int_to_fixed(
									4);
						} else {
							switch (results->bytes_per_pixel[i]) {
							case 4:
								results->lines_interleaved_in_mem_access[i] =
									bw_int_to_fixed(
										2);
								results->latency_hiding_lines[i] =
									bw_int_to_fixed(
										2);
								break;
							case 2:
								results->lines_interleaved_in_mem_access[i] =
									bw_int_to_fixed(
										4);
								results->latency_hiding_lines[i] =
									bw_int_to_fixed(
										4);
								break;
							default:
								results->lines_interleaved_in_mem_access[i] =
									bw_int_to_fixed(
										8);
								results->latency_hiding_lines[i] =
									bw_int_to_fixed(
										4);
								break;
							}
						}
					}
				}
			} else {
				results->bytes_per_request[i] = bw_int_to_fixed(
					256);
				results->useful_bytes_per_request[i] =
					bw_int_to_fixed(256);
				results->lines_interleaved_in_mem_access[i] =
					bw_int_to_fixed(4);
				results->latency_hiding_lines[i] =
					bw_int_to_fixed(4);
			}
		}
	}
	/*requested peak bandwidth:*/
	/*the peak request-per-second bandwidth is the product of the maximum source lines in per line out in the beginning and in the middle of the frame, the ratio of the source width to the line time, the ratio of line interleaving in memory to lines of latency hiding, and the ratio of bytes per pixel to useful bytes per request.*/
	/*the peak bandwidth is the peak request-per-second bandwidth times the request size.*/
	/*the line buffer lines in per line out in the beginning of the frame is the vertical filter initialization value rounded up to even and divided by the line times for initialization, which is normally three.*/
	/*the line buffer lines in per line out in the middle of the frame is at least one, or the vertical scale ratio, rounded up to line pairs if not doing line buffer prefetching.*/
	/*the non-prefetching rounding up of the vertical scale ratio can also be done up to 1 (for a 0,2 pattern), 4/3 (for a 0,2,2 pattern), 6/4 (for a 0,2,2,2 pattern), or 3 (for a 2,4 pattern).*/
	/*the scaler vertical filter initialization value is calculated by the hardware as the floor of the average of the vertical scale ratio and the number of vertical taps increased by one.  add one more for possible odd line panning/bezel adjustment mode.*/
	/*for the bottom interlace field an extra 50% of the vertical scale ratio is considered for this calculation.*/
	/*in top-bottom stereo mode software has to set the filter initialization value manually and explicitly limit it to 4.  further, there is only one line time for initialization.*/
	/*line buffer prefetching is done when the number of lines in the line buffer exceeds the number of taps plus the ceiling of the vertical scale ratio.*/
	/*line buffer prefetching is not done when downscaling in the graphics pipe or for possible odd line panning/bezel adjustment mode.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->v_filter_init[i] =
				bw_floor2(
					bw_div(
						(bw_add(
							bw_add(
								bw_add(
									bw_int_to_fixed(
										1),
									results->v_taps[i]),
								results->vsr[i]),
							bw_mul(
								bw_mul(
									bw_int_to_fixed(
										results->interlace_mode[i]),
									bw_frc_to_fixed(
										5,
										10)),
								results->vsr[i]))),
						bw_int_to_fixed(2)),
					bw_int_to_fixed(1));
			if (mode_data->panning_and_bezel_adjustment
				== bw_def_any_lines) {
				results->v_filter_init[i] = bw_add(
					results->v_filter_init[i],
					bw_int_to_fixed(1));
			}
			if (stereo_mode[i] == bw_def_top_bottom) {
				v_filter_init_mode[i] = bw_def_manual;
				results->v_filter_init[i] = bw_min2(
					results->v_filter_init[i],
					bw_int_to_fixed(4));
			} else {
				v_filter_init_mode[i] = bw_def_auto;
			}
			if (stereo_mode[i] == bw_def_top_bottom) {
				results->num_lines_at_frame_start =
					bw_int_to_fixed(1);
			} else {
				results->num_lines_at_frame_start =
					bw_int_to_fixed(3);
			}
			if ((bw_mtn(results->vsr[i], bw_int_to_fixed(1))
				&& surface_type[i] == bw_def_graphics)
				|| mode_data->panning_and_bezel_adjustment
					== bw_def_any_lines) {
				results->line_buffer_prefetch[i] = 0;
			} else if ((((dceip->underlay_downscale_prefetch_enabled
				== 1 && surface_type[i] != bw_def_graphics)
				|| surface_type[i] == bw_def_graphics)
				&& (bw_mtn(
					results->lb_partitions[i],
					bw_add(
						results->v_taps[i],
						bw_ceil2(
							results->vsr[i],
							bw_int_to_fixed(1))))))) {
				results->line_buffer_prefetch[i] = 1;
			} else {
				results->line_buffer_prefetch[i] = 0;
			}
			results->lb_lines_in_per_line_out_in_beginning_of_frame[i] =
				bw_div(
					bw_ceil2(
						results->v_filter_init[i],
						bw_int_to_fixed(
							dceip->lines_interleaved_into_lb)),
					results->num_lines_at_frame_start);
			if (results->line_buffer_prefetch[i] == 1) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_max2(
						bw_int_to_fixed(1),
						results->vsr[i]);
			} else if (bw_leq(
				results->vsr[i],
				bw_int_to_fixed(1))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_int_to_fixed(1);
			} else if (bw_leq(
				results->vsr[i],
				bw_int_to_fixed(4 / 3))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_div(
						bw_int_to_fixed(4),
						bw_int_to_fixed(3));
			} else if (bw_leq(
				results->vsr[i],
				bw_int_to_fixed(6 / 4))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_div(
						bw_int_to_fixed(6),
						bw_int_to_fixed(4));
			} else if (bw_leq(
				results->vsr[i],
				bw_int_to_fixed(2))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_int_to_fixed(2);
			} else if (bw_leq(
				results->vsr[i],
				bw_int_to_fixed(3))) {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_int_to_fixed(3);
			} else {
				results->lb_lines_in_per_line_out_in_middle_of_frame[i] =
					bw_int_to_fixed(4);
			}
			if (results->line_buffer_prefetch[i] == 1
				|| bw_equ(
					results->lb_lines_in_per_line_out_in_middle_of_frame[i],
					bw_int_to_fixed(2))
				|| bw_equ(
					results->lb_lines_in_per_line_out_in_middle_of_frame[i],
					bw_int_to_fixed(4))) {
				results->horizontal_blank_and_chunk_granularity_factor[i] =
					bw_int_to_fixed(1);
			} else {
				results->horizontal_blank_and_chunk_granularity_factor[i] =
					bw_div(
						results->h_total[i],
						(bw_div(
							(bw_add(
								results->h_total[i],
								bw_div(
									(bw_sub(
										results->source_width_pixels[i],
										bw_int_to_fixed(
											dceip->chunk_width))),
									results->hsr[i]))),
							bw_int_to_fixed(2))));
			}
			results->request_bandwidth[i] =
				bw_div(
					bw_mul(
						bw_div(
							bw_mul(
								bw_div(
									bw_mul(
										bw_max2(
											results->lb_lines_in_per_line_out_in_beginning_of_frame[i],
											results->lb_lines_in_per_line_out_in_middle_of_frame[i]),
										results->source_width_rounded_up_to_chunks[i]),
									(bw_div(
										results->h_total[i],
										results->pixel_rate[i]))),
								bw_int_to_fixed(
									results->bytes_per_pixel[i])),
							results->useful_bytes_per_request[i]),
						results->lines_interleaved_in_mem_access[i]),
					results->latency_hiding_lines[i]);
			results->display_bandwidth[i] = bw_mul(
				results->request_bandwidth[i],
				results->bytes_per_request[i]);
		}
	}
	/*outstanding chunk request limit*/
	/*if underlay buffer sharing is enabled, the data buffer size for underlay in 422 or 444 is the sum of the luma and chroma data buffer sizes.*/
	/*underlay buffer sharing mode is only permitted in orthogonal rotation modes.*/
	/*if there is only one display enabled, the data buffer size for graphics is doubled.*/
	/*the memory chunk size in bytes is 1024 for the writeback, and 256 times the memory line interleaving and the bytes per pixel for graphics*/
	/*and underlay.*/
	/*the pipe chunk size uses 2 for line interleaving, except for the write back, in which case it is 1.*/
	/*graphics and underlay data buffer size is adjusted (limited) using the outstanding chunk request limit if there is more than one*/
	/*display enabled or if the dmif request buffer is not large enough for the total data buffer size.*/
	/*the outstanding chunk request limit is the ceiling of the adjusted data buffer size divided by the chunk size in bytes*/
	/*the adjusted data buffer size is the product of the display bandwidth and the minimum effective data buffer size in terms of time,*/
	/*rounded up to the chunk size in bytes, but should not exceed the original data buffer size*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			switch (surface_type[i]) {
			case bw_def_display_write_back420_luma:
				results->data_buffer_size[i] =
					bw_int_to_fixed(
						dceip->display_write_back420_luma_mcifwr_buffer_size);
				break;
			case bw_def_display_write_back420_chroma:
				results->data_buffer_size[i] =
					bw_int_to_fixed(
						dceip->display_write_back420_chroma_mcifwr_buffer_size);
				break;
			case bw_def_underlay420_luma:
				results->data_buffer_size[i] = bw_int_to_fixed(
					dceip->underlay_luma_dmif_size);
				break;
			case bw_def_underlay420_chroma:
				results->data_buffer_size[i] =
					bw_div(
						bw_int_to_fixed(
							dceip->underlay_chroma_dmif_size),
						bw_int_to_fixed(2));
				break;
			case bw_def_underlay422:
			case bw_def_underlay444:
				if (results->orthogonal_rotation[i] == 0) {
					results->data_buffer_size[i] =
						bw_int_to_fixed(
							dceip->underlay_luma_dmif_size);
				} else {
					results->data_buffer_size[i] =
						bw_add(
							bw_int_to_fixed(
								dceip->underlay_luma_dmif_size),
							bw_int_to_fixed(
								dceip->underlay_chroma_dmif_size));
				}
				break;
			default:
				if (mode_data->number_of_displays == 1
					&& bw_equ(
						dceip->de_tiling_buffer,
						bw_int_to_fixed(0))) {
					if (mode_data->d0_fbc_enable) {
						results->data_buffer_size[i] =
							bw_mul(
								bw_int_to_fixed(
									dceip->max_dmif_buffer_allocated),
								bw_int_to_fixed(
									dceip->graphics_dmif_size));
					} else {
						/*the effective dmif buffer size in non-fbc mode is limited by the 16 entry chunk tracker*/
						results->data_buffer_size[i] =
							bw_mul(
								bw_mul(
									bw_int_to_fixed(
										max_chunks_non_fbc_mode),
									bw_int_to_fixed(
										pixels_per_chunk)),
								bw_int_to_fixed(
									results->bytes_per_pixel[i]));
					}
				} else {
					results->data_buffer_size[i] =
						bw_int_to_fixed(
							dceip->graphics_dmif_size);
				}
				break;
			}
			if (surface_type[i] == bw_def_display_write_back420_luma
				|| surface_type[i]
					== bw_def_display_write_back420_chroma) {
				results->memory_chunk_size_in_bytes[i] =
					bw_int_to_fixed(1024);
				results->pipe_chunk_size_in_bytes[i] =
					bw_int_to_fixed(1024);
			} else {
				results->memory_chunk_size_in_bytes[i] =
					bw_mul(
						bw_mul(
							bw_int_to_fixed(
								dceip->chunk_width),
							results->lines_interleaved_in_mem_access[i]),
						bw_int_to_fixed(
							results->bytes_per_pixel[i]));
				results->pipe_chunk_size_in_bytes[i] =
					bw_mul(
						bw_mul(
							bw_int_to_fixed(
								dceip->chunk_width),
							bw_int_to_fixed(
								dceip->lines_interleaved_into_lb)),
						bw_int_to_fixed(
							results->bytes_per_pixel[i]));
			}
		}
	}
	results->min_dmif_size_in_time = bw_int_to_fixed(9999);
	results->min_mcifwr_size_in_time = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				if (bw_ltn(
					bw_div(
						bw_div(
							bw_mul(
								results->data_buffer_size[i],
								results->bytes_per_request[i]),
							results->useful_bytes_per_request[i]),
						results->display_bandwidth[i]),
					results->min_dmif_size_in_time)) {
					results->min_dmif_size_in_time =
						bw_div(
							bw_div(
								bw_mul(
									results->data_buffer_size[i],
									results->bytes_per_request[i]),
								results->useful_bytes_per_request[i]),
							results->display_bandwidth[i]);
				}
			} else {
				if (bw_ltn(
					bw_div(
						bw_div(
							bw_mul(
								results->data_buffer_size[i],
								results->bytes_per_request[i]),
							results->useful_bytes_per_request[i]),
						results->display_bandwidth[i]),
					results->min_mcifwr_size_in_time)) {
					results->min_mcifwr_size_in_time =
						bw_div(
							bw_div(
								bw_mul(
									results->data_buffer_size[i],
									results->bytes_per_request[i]),
								results->useful_bytes_per_request[i]),
							results->display_bandwidth[i]);
				}
			}
		}
	}
	results->total_requests_for_dmif_size = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]
			&& surface_type[i] != bw_def_display_write_back420_luma
			&& surface_type[i]
				!= bw_def_display_write_back420_chroma) {
			results->total_requests_for_dmif_size = bw_add(
				results->total_requests_for_dmif_size,
				bw_div(
					results->data_buffer_size[i],
					results->useful_bytes_per_request[i]));
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma
				&& dceip->limit_excessive_outstanding_dmif_requests
				&& (mode_data->number_of_displays > 1
					|| bw_mtn(
						results->total_requests_for_dmif_size,
						dceip->dmif_request_buffer_size))) {
				results->adjusted_data_buffer_size[i] =
					bw_min2(
						results->data_buffer_size[i],
						bw_ceil2(
							bw_mul(
								results->min_dmif_size_in_time,
								results->display_bandwidth[i]),
							results->memory_chunk_size_in_bytes[i]));
			} else {
				results->adjusted_data_buffer_size[i] =
					results->data_buffer_size[i];
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if ((mode_data->number_of_displays == 1
				&& results->number_of_underlay_surfaces == 0)) {
				/*set maximum chunk limit if only one graphic pipe is enabled*/
				results->outstanding_chunk_request_limit[i] =
					bw_int_to_fixed(255);
			} else {
				results->outstanding_chunk_request_limit[i] =
					bw_ceil2(
						bw_div(
							results->adjusted_data_buffer_size[i],
							results->pipe_chunk_size_in_bytes[i]),
						bw_int_to_fixed(1));
			}
		}
	}
	/*outstanding pte request limit*/
	/*in tiling mode with no rotation the sg pte requests are 8 useful pt_es, the sg row height is the page height and the sg page width x height is 64x64 for 8bpp, 64x32 for 16 bpp, 32x32 for 32 bpp*/
	/*in tiling mode with rotation the sg pte requests are only one useful pte, and the sg row height is also the page height, but the sg page width and height are swapped*/
	/*in linear mode the pte requests are 8 useful pt_es, the sg page width is 4096 divided by the bytes per pixel, the sg page height is 1, but there is just one row whose height is the lines of pte prefetching*/
	/*the outstanding pte request limit is obtained by multiplying the outstanding chunk request limit by the peak pte request to eviction limiting ratio, rounding up to integer, multiplying by the pte requests per chunk, and rounding up to integer again*/
	/*if not using peak pte request to eviction limiting, the outstanding pte request limit is the pte requests in the vblank*/
	/*the pte requests in the vblank is the product of the number of pte request rows times the number of pte requests in a row*/
	/*the number of pte requests in a row is the quotient of the source width divided by 256, multiplied by the pte requests per chunk, rounded up to even, multiplied by the scatter-gather row height and divided by the scatter-gather page height*/
	/*the pte requests per chunk is 256 divided by the scatter-gather page width and the useful pt_es per pte request*/
	if (mode_data->number_of_displays > 1
		|| (mode_data->graphics_rotation_angle != 0
			&& mode_data->graphics_rotation_angle != 180)) {
		results->peak_pte_request_to_eviction_ratio_limiting =
			dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display;
	} else {
		results->peak_pte_request_to_eviction_ratio_limiting =
			dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]
			&& results->scatter_gather_enable_for_pipe[i] == 1) {
			if (tiling_mode[i] == bw_def_linear) {
				results->useful_pte_per_pte_request =
					bw_int_to_fixed(8);
				results->scatter_gather_page_width[i] = bw_div(
					bw_int_to_fixed(4096),
					bw_int_to_fixed(
						results->bytes_per_pixel[i]));
				results->scatter_gather_page_height[i] =
					bw_int_to_fixed(1);
				results->scatter_gather_pte_request_rows =
					bw_int_to_fixed(1);
				results->scatter_gather_row_height =
					bw_int_to_fixed(
						dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode);
			} else if (bw_equ(
				results->rotation_angle[i],
				bw_int_to_fixed(0))
				|| bw_equ(
					results->rotation_angle[i],
					bw_int_to_fixed(180))) {
				results->useful_pte_per_pte_request =
					bw_int_to_fixed(8);
				switch (results->bytes_per_pixel[i]) {
				case 4:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(32);
					break;
				case 2:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(32);
					break;
				default:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(64);
					break;
				}
				results->scatter_gather_pte_request_rows =
					bw_int_to_fixed(
						dceip->scatter_gather_pte_request_rows_in_tiling_mode);
				results->scatter_gather_row_height =
					results->scatter_gather_page_height[i];
			} else {
				results->useful_pte_per_pte_request =
					bw_int_to_fixed(1);
				switch (results->bytes_per_pixel[i]) {
				case 4:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(32);
					break;
				case 2:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(32);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(64);
					break;
				default:
					results->scatter_gather_page_width[i] =
						bw_int_to_fixed(64);
					results->scatter_gather_page_height[i] =
						bw_int_to_fixed(64);
					break;
				}
				results->scatter_gather_pte_request_rows =
					bw_int_to_fixed(
						dceip->scatter_gather_pte_request_rows_in_tiling_mode);
				results->scatter_gather_row_height =
					results->scatter_gather_page_height[i];
			}
			results->pte_request_per_chunk[i] = bw_div(
				bw_div(
					bw_int_to_fixed(dceip->chunk_width),
					results->scatter_gather_page_width[i]),
				results->useful_pte_per_pte_request);
			results->scatter_gather_pte_requests_in_row[i] =
				bw_div(
					bw_mul(
						bw_ceil2(
							bw_mul(
								bw_div(
									results->source_width_rounded_up_to_chunks[i],
									bw_int_to_fixed(
										dceip->chunk_width)),
								results->pte_request_per_chunk[i]),
							bw_int_to_fixed(1)),
						results->scatter_gather_row_height),
					results->scatter_gather_page_height[i]);
			results->scatter_gather_pte_requests_in_vblank = bw_mul(
				results->scatter_gather_pte_request_rows,
				results->scatter_gather_pte_requests_in_row[i]);
			if (bw_equ(
				results->peak_pte_request_to_eviction_ratio_limiting,
				bw_int_to_fixed(0))) {
				results->scatter_gather_pte_request_limit[i] =
					results->scatter_gather_pte_requests_in_vblank;
			} else {
				results->scatter_gather_pte_request_limit[i] =
					bw_max2(
						dceip->minimum_outstanding_pte_request_limit,
						bw_min2(
							results->scatter_gather_pte_requests_in_vblank,
							bw_ceil2(
								bw_mul(
									bw_mul(
										bw_div(
											bw_ceil2(
												results->adjusted_data_buffer_size[i],
												results->memory_chunk_size_in_bytes[i]),
											results->memory_chunk_size_in_bytes[i]),
										results->pte_request_per_chunk[i]),
									results->peak_pte_request_to_eviction_ratio_limiting),
								bw_int_to_fixed(
									1))));
			}
		}
	}
	/*pitch padding recommended for efficiency in linear mode*/
	/*in linear mode graphics or underlay with scatter gather, a pitch that is a multiple of the channel interleave (256 bytes) times the channel-bank rotation is not efficient*/
	/*if that is the case it is recommended to pad the pitch by at least 256 pixels*/
	results->inefficient_linear_pitch_in_bytes = bw_mul(
		bw_mul(
			bw_int_to_fixed(256),
			bw_int_to_fixed(vbios->number_of_dram_banks)),
		bw_int_to_fixed(vbios->number_of_dram_channels));
	switch (mode_data->underlay_surface_type) {
	case bw_def_420:
		results->inefficient_underlay_pitch_in_pixels =
			results->inefficient_linear_pitch_in_bytes;
		break;
	case bw_def_422:
		results->inefficient_underlay_pitch_in_pixels = bw_div(
			results->inefficient_linear_pitch_in_bytes,
			bw_int_to_fixed(2));
		break;
	default:
		results->inefficient_underlay_pitch_in_pixels = bw_div(
			results->inefficient_linear_pitch_in_bytes,
			bw_int_to_fixed(4));
		break;
	}
	if (mode_data->underlay_tiling_mode == bw_def_linear
		&& vbios->scatter_gather_enable == 1
		&& bw_equ(
			bw_mod(
				bw_int_to_fixed(
					mode_data->underlay_pitch_in_pixels),
				results->inefficient_underlay_pitch_in_pixels),
			bw_int_to_fixed(0))) {
		results->minimum_underlay_pitch_padding_recommended_for_efficiency =
			bw_int_to_fixed(256);
	} else {
		results->minimum_underlay_pitch_padding_recommended_for_efficiency =
			bw_int_to_fixed(0);
	}
	/*pixel transfer time*/
	/*the dmif and mcifwr yclk(pclk) required is the one that allows the transfer of all pipe's data buffer size in memory in the time for data transfer*/
	/*for dmif, pte and cursor requests have to be included.*/
	/*the dram data requirement is doubled when the data request size in bytes is less than the dram channel width times the burst size (8)*/
	/*the dram data requirement is also multiplied by the number of channels in the case of low power tiling*/
	/*the page close-open time is determined by trc and the number of page close-opens*/
	/*in tiled mode graphics or underlay with scatter-gather enabled the bytes per page close-open is the product of the memory line interleave times the maximum of the scatter-gather page width and the product of the tile width (8 pixels) times the number of channels times the number of banks.*/
	/*in linear mode graphics or underlay with scatter-gather enabled and inefficient pitch, the bytes per page close-open is the line request alternation slice, because different lines are in completely different 4k address bases.*/
	/*otherwise, the bytes page close-open is the chunk size because that is the arbitration slice.*/
	/*pte requests are grouped by pte requests per chunk if that is more than 1. each group costs a page close-open time for dmif reads*/
	/*cursor requests outstanding are limited to a group of two source lines. each group costs a page close-open time for dmif reads*/
	/*the display reads and writes time for data transfer is the minimum data or cursor buffer size in time minus the mc urgent latency*/
	/*the mc urgent latency is experienced more than one time if the number of dmif requests in the data buffer exceeds the request buffer size plus the request slots reserved for dmif in the dram channel arbiter queues*/
	/*the dispclk required is the maximum for all surfaces of the maximum of the source pixels for first output pixel times the throughput factor, divided by the pixels per dispclk, and divided by the minimum latency hiding minus the dram speed/p-state change latency minus the burst time, and the source pixels for last output pixel, times the throughput factor, divided by the pixels per dispclk, and divided by the minimum latency hiding minus the dram speed/p-state change latency minus the burst time, plus the active time.*/
	/*the data burst time is the maximum of the total page close-open time, total dmif/mcifwr buffer size in memory divided by the dram bandwidth, and the total dmif/mcifwr buffer size in memory divided by the 32 byte sclk data bus bandwidth, each multiplied by its efficiency.*/
	/*the source line transfer time is the maximum for all surfaces of the maximum of the burst time plus the urgent latency times the floor of the data required divided by the buffer size for the fist pixel, and the burst time plus the urgent latency times the floor of the data required divided by the buffer size for the last pixel plus the active time.*/
	/*the source pixels for the first output pixel is 512 if the scaler vertical filter initialization value is greater than 2, and it is 4 times the source width if it is greater than 4.*/
	/*the source pixels for the last output pixel is the source width times the scaler vertical filter initialization value rounded up to even*/
	/*the source data for these pixels is the number of pixels times the bytes per pixel times the bytes per request divided by the useful bytes per request.*/
	results->cursor_total_data = bw_int_to_fixed(0);
	results->cursor_total_request_groups = bw_int_to_fixed(0);
	results->scatter_gather_total_pte_requests = bw_int_to_fixed(0);
	results->scatter_gather_total_pte_request_groups = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->cursor_total_data =
				bw_add(
					results->cursor_total_data,
					bw_mul(
						bw_mul(
							bw_int_to_fixed(2),
							results->cursor_width_pixels[i]),
						bw_int_to_fixed(4)));
			results->cursor_total_request_groups = bw_add(
				results->cursor_total_request_groups,
				bw_ceil2(
					bw_div(
						results->cursor_width_pixels[i],
						dceip->cursor_chunk_width),
					bw_int_to_fixed(1)));
			if (results->scatter_gather_enable_for_pipe[i]) {
				results->scatter_gather_total_pte_requests =
					bw_add(
						results->scatter_gather_total_pte_requests,
						results->scatter_gather_pte_request_limit[i]);
				results->scatter_gather_total_pte_request_groups =
					bw_add(
						results->scatter_gather_total_pte_request_groups,
						bw_ceil2(
							bw_div(
								results->scatter_gather_pte_request_limit[i],
								bw_ceil2(
									results->pte_request_per_chunk[i],
									bw_int_to_fixed(
										1))),
							bw_int_to_fixed(1)));
			}
		}
	}
	results->tile_width_in_pixels = bw_int_to_fixed(8);
	results->dmif_total_number_of_data_request_page_close_open =
		bw_int_to_fixed(0);
	results->mcifwr_total_number_of_data_request_page_close_open =
		bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (results->scatter_gather_enable_for_pipe[i] == 1
				&& tiling_mode[i] != bw_def_linear) {
				results->bytes_per_page_close_open =
					bw_mul(
						results->lines_interleaved_in_mem_access[i],
						bw_max2(
							bw_mul(
								bw_mul(
									bw_mul(
										bw_int_to_fixed(
											results->bytes_per_pixel[i]),
										results->tile_width_in_pixels),
									bw_int_to_fixed(
										vbios->number_of_dram_banks)),
								bw_int_to_fixed(
									vbios->number_of_dram_channels)),
							bw_mul(
								bw_int_to_fixed(
									results->bytes_per_pixel[i]),
								results->scatter_gather_page_width[i])));
			} else if (results->scatter_gather_enable_for_pipe[i]
				== 1 && tiling_mode[i] == bw_def_linear
				&& bw_equ(
					bw_mod(
						(bw_mul(
							results->pitch_in_pixels_after_surface_type[i],
							bw_int_to_fixed(
								results->bytes_per_pixel[i]))),
						results->inefficient_linear_pitch_in_bytes),
					bw_int_to_fixed(0))) {
				results->bytes_per_page_close_open =
					dceip->linear_mode_line_request_alternation_slice;
			} else {
				results->bytes_per_page_close_open =
					results->memory_chunk_size_in_bytes[i];
			}
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				results->dmif_total_number_of_data_request_page_close_open =
					bw_add(
						results->dmif_total_number_of_data_request_page_close_open,
						bw_div(
							bw_ceil2(
								results->adjusted_data_buffer_size[i],
								results->memory_chunk_size_in_bytes[i]),
							results->bytes_per_page_close_open));
			} else {
				results->mcifwr_total_number_of_data_request_page_close_open =
					bw_add(
						results->mcifwr_total_number_of_data_request_page_close_open,
						bw_div(
							bw_ceil2(
								results->adjusted_data_buffer_size[i],
								results->memory_chunk_size_in_bytes[i]),
							results->bytes_per_page_close_open));
			}
		}
	}
	results->dmif_total_page_close_open_time =
		bw_div(
			bw_mul(
				(bw_add(
					bw_add(
						results->dmif_total_number_of_data_request_page_close_open,
						results->scatter_gather_total_pte_request_groups),
					results->cursor_total_request_groups)),
				vbios->trc),
			bw_int_to_fixed(1000));
	results->mcifwr_total_page_close_open_time =
		bw_div(
			bw_mul(
				results->mcifwr_total_number_of_data_request_page_close_open,
				vbios->trc),
			bw_int_to_fixed(1000));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->adjusted_data_buffer_size_in_memory[i] =
				bw_div(
					bw_mul(
						results->adjusted_data_buffer_size[i],
						results->bytes_per_request[i]),
					results->useful_bytes_per_request[i]);
		}
	}
	results->total_requests_for_adjusted_dmif_size = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				results->total_requests_for_adjusted_dmif_size =
					bw_add(
						results->total_requests_for_adjusted_dmif_size,
						bw_div(
							results->adjusted_data_buffer_size[i],
							results->useful_bytes_per_request[i]));
			}
		}
	}
	if (dceip->dcfclk_request_generation == 1) {
		results->total_dmifmc_urgent_trips = bw_int_to_fixed(1);
	} else {
		results->total_dmifmc_urgent_trips =
			bw_ceil2(
				bw_div(
					results->total_requests_for_adjusted_dmif_size,
					(bw_add(
						dceip->dmif_request_buffer_size,
						bw_int_to_fixed(
							vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel
								* vbios->number_of_dram_channels)))),
				bw_int_to_fixed(1));
	}
	results->total_dmifmc_urgent_latency = bw_mul(
		vbios->dmifmc_urgent_latency,
		results->total_dmifmc_urgent_trips);
	results->total_display_reads_required_data = bw_int_to_fixed(0);
	results->total_display_reads_required_dram_access_data =
		bw_int_to_fixed(0);
	results->total_display_writes_required_data = bw_int_to_fixed(0);
	results->total_display_writes_required_dram_access_data =
		bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				results->display_reads_required_data =
					results->adjusted_data_buffer_size_in_memory[i];
				results->display_reads_required_dram_access_data =
					bw_mul(
						results->adjusted_data_buffer_size_in_memory[i],
						bw_ceil2(
							bw_div(
								bw_int_to_fixed(
									vbios->dram_channel_width_in_bits),
								results->bytes_per_request[i]),
							bw_int_to_fixed(1)));
				if (results->access_one_channel_only[i]) {
					results->display_reads_required_dram_access_data =
						bw_mul(
							results->display_reads_required_dram_access_data,
							bw_int_to_fixed(
								vbios->number_of_dram_channels));
				}
				results->total_display_reads_required_data =
					bw_add(
						results->total_display_reads_required_data,
						results->display_reads_required_data);
				results->total_display_reads_required_dram_access_data =
					bw_add(
						results->total_display_reads_required_dram_access_data,
						results->display_reads_required_dram_access_data);
			} else {
				results->total_display_writes_required_data =
					bw_add(
						results->total_display_writes_required_data,
						results->adjusted_data_buffer_size_in_memory[i]);
				results->total_display_writes_required_dram_access_data =
					bw_add(
						results->total_display_writes_required_dram_access_data,
						bw_mul(
							results->adjusted_data_buffer_size_in_memory[i],
							bw_ceil2(
								bw_div(
									bw_int_to_fixed(
										vbios->dram_channel_width_in_bits),
									results->bytes_per_request[i]),
								bw_int_to_fixed(
									1))));
			}
		}
	}
	results->total_display_reads_required_data = bw_add(
		bw_add(
			results->total_display_reads_required_data,
			results->cursor_total_data),
		bw_mul(
			results->scatter_gather_total_pte_requests,
			bw_int_to_fixed(64)));
	results->total_display_reads_required_dram_access_data = bw_add(
		bw_add(
			results->total_display_reads_required_dram_access_data,
			results->cursor_total_data),
		bw_mul(
			results->scatter_gather_total_pte_requests,
			bw_int_to_fixed(64)));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_mtn(
				results->v_filter_init[i],
				bw_int_to_fixed(4))) {
				results->src_pixels_for_first_output_pixel[i] =
					bw_mul(
						bw_int_to_fixed(4),
						results->source_width_rounded_up_to_chunks[i]);
			} else {
				if (bw_mtn(
					results->v_filter_init[i],
					bw_int_to_fixed(2))) {
					results->src_pixels_for_first_output_pixel[i] =
						bw_int_to_fixed(512);
				} else {
					results->src_pixels_for_first_output_pixel[i] =
						bw_int_to_fixed(0);
				}
			}
			results->src_data_for_first_output_pixel[i] =
				bw_div(
					bw_mul(
						bw_mul(
							results->src_pixels_for_first_output_pixel[i],
							bw_int_to_fixed(
								results->bytes_per_pixel[i])),
						results->bytes_per_request[i]),
					results->useful_bytes_per_request[i]);
			results->src_pixels_for_last_output_pixel[i] =
				bw_mul(
					results->source_width_rounded_up_to_chunks[i],
					bw_max2(
						bw_ceil2(
							results->v_filter_init[i],
							bw_int_to_fixed(
								dceip->lines_interleaved_into_lb)),
						bw_mul(
							bw_ceil2(
								results->vsr[i],
								bw_int_to_fixed(
									dceip->lines_interleaved_into_lb)),
							results->horizontal_blank_and_chunk_granularity_factor[i])));
			results->src_data_for_last_output_pixel[i] =
				bw_div(
					bw_mul(
						bw_mul(
							bw_mul(
								results->source_width_rounded_up_to_chunks[i],
								bw_max2(
									bw_ceil2(
										results->v_filter_init[i],
										bw_int_to_fixed(
											dceip->lines_interleaved_into_lb)),
									results->lines_interleaved_in_mem_access[i])),
							bw_int_to_fixed(
								results->bytes_per_pixel[i])),
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
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 2; j++) {
			results->dmif_burst_time[i][j] =
				bw_max3(
					results->dmif_total_page_close_open_time,
					bw_div(
						results->total_display_reads_required_dram_access_data,
						(bw_mul(
							bw_div(
								bw_mul(
									yclk[i],
									bw_int_to_fixed(
										vbios->dram_channel_width_in_bits)),
								bw_int_to_fixed(
									8)),
							bw_int_to_fixed(
								vbios->number_of_dram_channels)))),
					bw_div(
						results->total_display_reads_required_data,
						(bw_mul(
							sclk[j],
							vbios->data_return_bus_width))));
			if (mode_data->d1_display_write_back_dwb_enable == 1) {
				results->mcifwr_burst_time[i][j] =
					bw_max3(
						results->mcifwr_total_page_close_open_time,
						bw_div(
							results->total_display_writes_required_dram_access_data,
							(bw_mul(
								bw_div(
									bw_mul(
										yclk[i],
										bw_int_to_fixed(
											vbios->dram_channel_width_in_bits)),
									bw_int_to_fixed(
										8)),
								bw_int_to_fixed(
									vbios->number_of_dram_channels)))),
						bw_div(
							results->total_display_writes_required_data,
							(bw_mul(
								sclk[j],
								vbios->data_return_bus_width))));
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		for (j = 0; j <= 2; j++) {
			for (k = 0; k <= 2; k++) {
				if (results->enable[i]) {
					if (surface_type[i]
						!= bw_def_display_write_back420_luma
						&& surface_type[i]
							!= bw_def_display_write_back420_chroma) {
						results->line_source_transfer_time[i][j][k] =
							bw_max2(
								bw_mul(
									(bw_add(
										results->total_dmifmc_urgent_latency,
										results->dmif_burst_time[j][k])),
									bw_floor2(
										bw_div(
											results->src_data_for_first_output_pixel[i],
											results->adjusted_data_buffer_size_in_memory[i]),
										bw_int_to_fixed(
											1))),
								bw_sub(
									bw_mul(
										(bw_add(
											results->total_dmifmc_urgent_latency,
											results->dmif_burst_time[j][k])),
										bw_floor2(
											bw_div(
												results->src_data_for_last_output_pixel[i],
												results->adjusted_data_buffer_size_in_memory[i]),
											bw_int_to_fixed(
												1))),
									results->active_time[i]));
					} else {
						results->line_source_transfer_time[i][j][k] =
							bw_max2(
								bw_mul(
									(bw_add(
										vbios->mcifwrmc_urgent_latency,
										results->mcifwr_burst_time[j][k])),
									bw_floor2(
										bw_div(
											results->src_data_for_first_output_pixel[i],
											results->adjusted_data_buffer_size_in_memory[i]),
										bw_int_to_fixed(
											1))),
								bw_sub(
									bw_mul(
										(bw_add(
											vbios->mcifwrmc_urgent_latency,
											results->mcifwr_burst_time[j][k])),
										bw_floor2(
											bw_div(
												results->src_data_for_last_output_pixel[i],
												results->adjusted_data_buffer_size_in_memory[i]),
											bw_int_to_fixed(
												1))),
									results->active_time[i]));
					}
				}
			}
		}
	}
	/*cpu c-state and p-state change enable*/
	/*for cpu p-state change to be possible for a yclk(pclk) and sclk level the dispclk required has to be enough for the blackout duration*/
	/*for cpu c-state change to be possible for a yclk(pclk) and sclk level the dispclk required has to be enough for the blackout duration and recovery*/
	/*condition for the blackout duration:*/
	/* minimum latency hiding > blackout duration + dmif burst time + line source transfer time*/
	/*condition for the blackout recovery:*/
	/* recovery time >  dmif burst time + 2 * urgent latency*/
	/* recovery time > (display bw * blackout duration  + (2 * urgent latency + dmif burst time)*dispclk - dmif size )*/
	/*                  / (dispclk - display bw)*/
	/*the minimum latency hiding is the minimum for all pipes of one screen line time, plus one more line time if doing lb prefetch, plus the dmif data buffer size equivalent in time, minus the urgent latency.*/
	/*the minimum latency hiding is  further limited by the cursor.  the cursor latency hiding is the number of lines of the cursor buffer, minus one if the downscaling is less than two, or minus three if it is more*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_equ(
				dceip->stutter_and_dram_clock_state_change_gated_before_cursor,
				bw_int_to_fixed(0))
				&& bw_mtn(
					results->cursor_width_pixels[i],
					bw_int_to_fixed(0))) {
				if (bw_ltn(
					results->vsr[i],
					bw_int_to_fixed(2))) {
					results->cursor_latency_hiding[i] =
						bw_div(
							bw_div(
								bw_mul(
									(bw_sub(
										dceip->cursor_dcp_buffer_lines,
										bw_int_to_fixed(
											1))),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]);
				} else {
					results->cursor_latency_hiding[i] =
						bw_div(
							bw_div(
								bw_mul(
									(bw_sub(
										dceip->cursor_dcp_buffer_lines,
										bw_int_to_fixed(
											3))),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]);
				}
			} else {
				results->cursor_latency_hiding[i] =
					bw_int_to_fixed(9999);
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (dceip->graphics_lb_nodownscaling_multi_line_prefetching
				== 1
				&& (bw_equ(results->vsr[i], bw_int_to_fixed(1))
					|| (bw_leq(
						results->vsr[i],
						bw_frc_to_fixed(8, 10))
						&& bw_leq(
							results->v_taps[i],
							bw_int_to_fixed(2))
						&& results->lb_bpc[i] == 8))
				&& surface_type[i] == bw_def_graphics) {
				results->minimum_latency_hiding[i] =
					bw_sub(
						bw_div(
							bw_mul(
								bw_div(
									(bw_add(
										bw_sub(
											results->lb_partitions[i],
											bw_int_to_fixed(
												1)),
										bw_div(
											bw_div(
												results->data_buffer_size[i],
												bw_int_to_fixed(
													results->bytes_per_pixel[i])),
											results->source_width_pixels[i]))),
									results->vsr[i]),
								results->h_total[i]),
							results->pixel_rate[i]),
						results->total_dmifmc_urgent_latency);
			} else {
				results->minimum_latency_hiding[i] =
					bw_sub(
						bw_div(
							bw_mul(
								(bw_add(
									bw_int_to_fixed(
										1
											+ results->line_buffer_prefetch[i]),
									bw_div(
										bw_div(
											bw_div(
												results->data_buffer_size[i],
												bw_int_to_fixed(
													results->bytes_per_pixel[i])),
											results->source_width_pixels[i]),
										results->vsr[i]))),
								results->h_total[i]),
							results->pixel_rate[i]),
						results->total_dmifmc_urgent_latency);
			}
			results->minimum_latency_hiding_with_cursor[i] =
				bw_min2(
					results->minimum_latency_hiding[i],
					results->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 2; j++) {
			results->blackout_duration_margin[i][j] =
				bw_int_to_fixed(9999);
			results->dispclk_required_for_blackout_duration[i][j] =
				bw_int_to_fixed(0);
			results->dispclk_required_for_blackout_recovery[i][j] =
				bw_int_to_fixed(0);
			for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
				if (results->enable[k]
					&& bw_mtn(
						vbios->blackout_duration,
						bw_int_to_fixed(0))) {
					if (surface_type[k]
						!= bw_def_display_write_back420_luma
						&& surface_type[k]
							!= bw_def_display_write_back420_chroma) {
						results->blackout_duration_margin[i][j] =
							bw_min2(
								results->blackout_duration_margin[i][j],
								bw_sub(
									bw_sub(
										bw_sub(
											results->minimum_latency_hiding_with_cursor[k],
											vbios->blackout_duration),
										results->dmif_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_blackout_duration[i][j] =
							bw_max3(
								results->dispclk_required_for_blackout_duration[i][j],
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_sub(
										bw_sub(
											results->minimum_latency_hiding_with_cursor[k],
											vbios->blackout_duration),
										results->dmif_burst_time[i][j]))),
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_add(
										bw_sub(
											bw_sub(
												results->minimum_latency_hiding_with_cursor[k],
												vbios->blackout_duration),
											results->dmif_burst_time[i][j]),
										results->active_time[k]))));
						if (bw_leq(
							vbios->maximum_blackout_recovery_time,
							bw_add(
								bw_mul(
									bw_int_to_fixed(
										2),
									results->total_dmifmc_urgent_latency),
								results->dmif_burst_time[i][j]))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_int_to_fixed(
									9999);
						} else if (bw_ltn(
							results->adjusted_data_buffer_size[k],
							bw_mul(
								bw_div(
									bw_mul(
										results->display_bandwidth[k],
										results->useful_bytes_per_request[k]),
									results->bytes_per_request[k]),
								(bw_add(
									vbios->blackout_duration,
									bw_add(
										bw_mul(
											bw_int_to_fixed(
												2),
											results->total_dmifmc_urgent_latency),
										results->dmif_burst_time[i][j])))))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_max2(
									results->dispclk_required_for_blackout_recovery[i][j],
									bw_div(
										bw_mul(
											bw_div(
												bw_div(
													(bw_sub(
														bw_mul(
															bw_div(
																bw_mul(
																	results->display_bandwidth[k],
																	results->useful_bytes_per_request[k]),
																results->bytes_per_request[k]),
															(bw_add(
																vbios->blackout_duration,
																vbios->maximum_blackout_recovery_time))),
														results->adjusted_data_buffer_size[k])),
													bw_int_to_fixed(
														results->bytes_per_pixel[k])),
												(bw_sub(
													vbios->maximum_blackout_recovery_time,
													bw_sub(
														bw_mul(
															bw_int_to_fixed(
																2),
															results->total_dmifmc_urgent_latency),
														results->dmif_burst_time[i][j])))),
											results->latency_hiding_lines[k]),
										results->lines_interleaved_in_mem_access[k]));
						}
					} else {
						results->blackout_duration_margin[i][j] =
							bw_min2(
								results->blackout_duration_margin[i][j],
								bw_sub(
									bw_sub(
										bw_sub(
											bw_sub(
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
										bw_mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_sub(
										bw_sub(
											bw_sub(
												results->minimum_latency_hiding_with_cursor[k],
												vbios->blackout_duration),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]))),
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_add(
										bw_sub(
											bw_sub(
												bw_sub(
													results->minimum_latency_hiding_with_cursor[k],
													vbios->blackout_duration),
												results->dmif_burst_time[i][j]),
											results->mcifwr_burst_time[i][j]),
										results->active_time[k]))));
						if (bw_ltn(
							vbios->maximum_blackout_recovery_time,
							bw_add(
								bw_add(
									bw_mul(
										bw_int_to_fixed(
											2),
										vbios->mcifwrmc_urgent_latency),
									results->dmif_burst_time[i][j]),
								results->mcifwr_burst_time[i][j]))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_int_to_fixed(
									9999);
						} else if (bw_ltn(
							results->adjusted_data_buffer_size[k],
							bw_mul(
								bw_div(
									bw_mul(
										results->display_bandwidth[k],
										results->useful_bytes_per_request[k]),
									results->bytes_per_request[k]),
								(bw_add(
									vbios->blackout_duration,
									bw_add(
										bw_mul(
											bw_int_to_fixed(
												2),
											results->total_dmifmc_urgent_latency),
										results->dmif_burst_time[i][j])))))) {
							results->dispclk_required_for_blackout_recovery[i][j] =
								bw_max2(
									results->dispclk_required_for_blackout_recovery[i][j],
									bw_div(
										bw_mul(
											bw_div(
												bw_div(
													(bw_sub(
														bw_mul(
															bw_div(
																bw_mul(
																	results->display_bandwidth[k],
																	results->useful_bytes_per_request[k]),
																results->bytes_per_request[k]),
															(bw_add(
																vbios->blackout_duration,
																vbios->maximum_blackout_recovery_time))),
														results->adjusted_data_buffer_size[k])),
													bw_int_to_fixed(
														results->bytes_per_pixel[k])),
												(bw_sub(
													vbios->maximum_blackout_recovery_time,
													(bw_add(
														bw_mul(
															bw_int_to_fixed(
																2),
															results->total_dmifmc_urgent_latency),
														results->dmif_burst_time[i][j]))))),
											results->latency_hiding_lines[k]),
										results->lines_interleaved_in_mem_access[k]));
						}
					}
				}
			}
		}
	}
	if (bw_mtn(
		results->blackout_duration_margin[high][high],
		bw_int_to_fixed(0))
		&& bw_ltn(
			results->dispclk_required_for_blackout_duration[high][high],
			vbios->high_voltage_max_dispclk)) {
		results->cpup_state_change_enable = bw_def_yes;
		if (bw_ltn(
			results->dispclk_required_for_blackout_recovery[high][high],
			vbios->high_voltage_max_dispclk)) {
			results->cpuc_state_change_enable = bw_def_yes;
		} else {
			results->cpuc_state_change_enable = bw_def_no;
		}
	} else {
		results->cpup_state_change_enable = bw_def_no;
		results->cpuc_state_change_enable = bw_def_no;
	}
	/*nb p-state change enable*/
	/*for dram speed/p-state change to be possible for a yclk(pclk) and sclk level there has to be positive margin and the dispclk required has to be below the maximum.*/
	/*the dram speed/p-state change margin is the minimum for all surfaces of the maximum latency hiding minus the dram speed/p-state change latency, minus the dmif burst time, minus the source line transfer time*/
	/*the maximum latency hiding is the minimum latency hiding plus one source line used for de-tiling in the line buffer, plus half the urgent latency*/
	/*if stutter and dram clock state change are gated before cursor then the cursor latency hiding does not limit stutter or dram clock state change*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (mode_data->number_of_displays <= 1
				|| mode_data->display_synchronization_enabled
					== bw_def_yes) {
				results->maximum_latency_hiding[i] =
					bw_int_to_fixed(450);
			} else {
				results->maximum_latency_hiding[i] =
					bw_add(
						results->minimum_latency_hiding[i],
						bw_add(
							bw_div(
								bw_mul(
									bw_div(
										bw_int_to_fixed(
											1),
										results->vsr[i]),
									results->h_total[i]),
								results->pixel_rate[i]),
							bw_mul(
								bw_frc_to_fixed(
									5,
									10),
								results->total_dmifmc_urgent_latency)));
			}
			results->maximum_latency_hiding_with_cursor[i] =
				bw_min2(
					results->maximum_latency_hiding[i],
					results->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 2; j++) {
			results->dram_speed_change_margin[i][j] =
				bw_int_to_fixed(9999);
			results->dispclk_required_for_dram_speed_change[i][j] =
				bw_int_to_fixed(0);
			for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
				if (results->enable[k]) {
					if (surface_type[k]
						!= bw_def_display_write_back420_luma
						&& surface_type[k]
							!= bw_def_display_write_back420_chroma) {
						results->dram_speed_change_margin[i][j] =
							bw_min2(
								results->dram_speed_change_margin[i][j],
								bw_sub(
									bw_sub(
										bw_sub(
											results->maximum_latency_hiding_with_cursor[k],
											vbios->nbp_state_change_latency),
										results->dmif_burst_time[i][j]),
									results->line_source_transfer_time[k][i][j]));
						results->dispclk_required_for_dram_speed_change[i][j] =
							bw_max3(
								results->dispclk_required_for_dram_speed_change[i][j],
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_sub(
										bw_sub(
											results->maximum_latency_hiding_with_cursor[k],
											vbios->nbp_state_change_latency),
										results->dmif_burst_time[i][j]))),
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_add(
										bw_sub(
											bw_sub(
												results->maximum_latency_hiding_with_cursor[k],
												vbios->nbp_state_change_latency),
											results->dmif_burst_time[i][j]),
										results->active_time[k]))));
					} else {
						results->dram_speed_change_margin[i][j] =
							bw_min2(
								results->dram_speed_change_margin[i][j],
								bw_sub(
									bw_sub(
										bw_sub(
											bw_sub(
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
										bw_mul(
											results->src_pixels_for_first_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_sub(
										bw_sub(
											bw_sub(
												results->maximum_latency_hiding_with_cursor[k],
												vbios->nbp_state_change_latency),
											results->dmif_burst_time[i][j]),
										results->mcifwr_burst_time[i][j]))),
								bw_div(
									bw_div(
										bw_mul(
											results->src_pixels_for_last_output_pixel[k],
											dceip->display_pipe_throughput_factor),
										dceip->lb_write_pixels_per_dispclk),
									(bw_add(
										bw_sub(
											bw_sub(
												bw_sub(
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
	if (bw_mtn(
		results->dram_speed_change_margin[high][high],
		bw_int_to_fixed(0))
		&& bw_ltn(
			results->dispclk_required_for_dram_speed_change[high][high],
			vbios->high_voltage_max_dispclk)) {
		results->nbp_state_change_enable = bw_def_yes;
	} else {
		results->nbp_state_change_enable = bw_def_no;
	}
	/*required yclk(pclk)*/
	/*yclk requirement only makes sense if the dmif and mcifwr data total page close-open time is less than the time for data transfer and the total pte requests fit in the scatter-gather saw queque size*/
	/*if that is the case, the yclk requirement is the maximum of the ones required by dmif and mcifwr, and the high/low yclk(pclk) is chosen accordingly*/
	/*high yclk(pclk) has to be selected when dram speed/p-state change is not possible.*/
	results->min_cursor_memory_interface_buffer_size_in_time =
		bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_mtn(
				results->cursor_width_pixels[i],
				bw_int_to_fixed(0))) {
				results->min_cursor_memory_interface_buffer_size_in_time =
					bw_min2(
						results->min_cursor_memory_interface_buffer_size_in_time,
						bw_div(
							bw_mul(
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
	results->min_read_buffer_size_in_time = bw_min2(
		results->min_cursor_memory_interface_buffer_size_in_time,
		results->min_dmif_size_in_time);
	results->display_reads_time_for_data_transfer = bw_sub(
		results->min_read_buffer_size_in_time,
		results->total_dmifmc_urgent_latency);
	results->display_writes_time_for_data_transfer = bw_sub(
		results->min_mcifwr_size_in_time,
		vbios->mcifwrmc_urgent_latency);
	results->dmif_required_dram_bandwidth = bw_div(
		results->total_display_reads_required_dram_access_data,
		results->display_reads_time_for_data_transfer);
	results->mcifwr_required_dram_bandwidth = bw_div(
		results->total_display_writes_required_dram_access_data,
		results->display_writes_time_for_data_transfer);
	results->required_dmifmc_urgent_latency_for_page_close_open = bw_div(
		(bw_sub(
			results->min_read_buffer_size_in_time,
			results->dmif_total_page_close_open_time)),
		results->total_dmifmc_urgent_trips);
	results->required_mcifmcwr_urgent_latency = bw_sub(
		results->min_mcifwr_size_in_time,
		results->mcifwr_total_page_close_open_time);
	if (bw_mtn(
		results->scatter_gather_total_pte_requests,
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		results->required_dram_bandwidth_gbyte_per_second =
			bw_int_to_fixed(9999);
		yclk_message =
			bw_def_exceeded_allowed_outstanding_pte_req_queue_size;
		results->y_clk_level = high;
		results->dram_bandwidth =
			bw_mul(
				bw_div(
					bw_mul(
						yclk[high],
						bw_int_to_fixed(
							vbios->dram_channel_width_in_bits)),
					bw_int_to_fixed(8)),
				bw_int_to_fixed(
					vbios->number_of_dram_channels));
	} else if (bw_mtn(
		vbios->dmifmc_urgent_latency,
		results->required_dmifmc_urgent_latency_for_page_close_open)
		|| bw_mtn(
			vbios->mcifwrmc_urgent_latency,
			results->required_mcifmcwr_urgent_latency)) {
		results->required_dram_bandwidth_gbyte_per_second =
			bw_int_to_fixed(9999);
		yclk_message = bw_def_exceeded_allowed_page_close_open;
		results->y_clk_level = high;
		results->dram_bandwidth =
			bw_mul(
				bw_div(
					bw_mul(
						yclk[high],
						bw_int_to_fixed(
							vbios->dram_channel_width_in_bits)),
					bw_int_to_fixed(8)),
				bw_int_to_fixed(
					vbios->number_of_dram_channels));
	} else {
		results->required_dram_bandwidth_gbyte_per_second = bw_div(
			bw_max2(
				results->dmif_required_dram_bandwidth,
				results->mcifwr_required_dram_bandwidth),
			bw_int_to_fixed(1000));
		if (bw_ltn(
			bw_mul(
				results->required_dram_bandwidth_gbyte_per_second,
				bw_int_to_fixed(1000)),
			bw_mul(
				bw_div(
					bw_mul(
						yclk[low],
						bw_int_to_fixed(
							vbios->dram_channel_width_in_bits)),
					bw_int_to_fixed(8)),
				bw_int_to_fixed(
					vbios->number_of_dram_channels)))
			&& (results->cpup_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[low][high],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[low][high],
						vbios->high_voltage_max_dispclk)))
			&& (results->cpuc_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[low][high],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[low][high],
						vbios->high_voltage_max_dispclk)
					&& bw_ltn(
						results->dispclk_required_for_blackout_recovery[low][high],
						vbios->high_voltage_max_dispclk)))
			&& bw_mtn(
				results->dram_speed_change_margin[low][high],
				bw_int_to_fixed(0))
			&& bw_ltn(
				results->dispclk_required_for_dram_speed_change[low][high],
				vbios->high_voltage_max_dispclk)) {
			yclk_message = bw_def_low;
			results->y_clk_level = low;
			results->dram_bandwidth =
				bw_mul(
					bw_div(
						bw_mul(
							yclk[low],
							bw_int_to_fixed(
								vbios->dram_channel_width_in_bits)),
						bw_int_to_fixed(8)),
					bw_int_to_fixed(
						vbios->number_of_dram_channels));
		} else if (bw_ltn(
			bw_mul(
				results->required_dram_bandwidth_gbyte_per_second,
				bw_int_to_fixed(1000)),
			bw_mul(
				bw_div(
					bw_mul(
						yclk[high],
						bw_int_to_fixed(
							vbios->dram_channel_width_in_bits)),
					bw_int_to_fixed(8)),
				bw_int_to_fixed(
					vbios->number_of_dram_channels)))) {
			yclk_message = bw_def_high;
			results->y_clk_level = high;
			results->dram_bandwidth =
				bw_mul(
					bw_div(
						bw_mul(
							yclk[high],
							bw_int_to_fixed(
								vbios->dram_channel_width_in_bits)),
						bw_int_to_fixed(8)),
					bw_int_to_fixed(
						vbios->number_of_dram_channels));
		} else {
			yclk_message = bw_def_exceeded_allowed_maximum_bw;
			results->y_clk_level = high;
			results->dram_bandwidth =
				bw_mul(
					bw_div(
						bw_mul(
							yclk[high],
							bw_int_to_fixed(
								vbios->dram_channel_width_in_bits)),
						bw_int_to_fixed(8)),
					bw_int_to_fixed(
						vbios->number_of_dram_channels));
		}
	}
	/*required sclk*/
	/*sclk requirement only makes sense if the total pte requests fit in the scatter-gather saw queque size*/
	/*if that is the case, the sclk requirement is the maximum of the ones required by dmif and mcifwr, and the high/mid/low sclk is chosen accordingly, unless that choice results in foresaking dram speed/nb p-state change.*/
	/*the dmif and mcifwr sclk required is the one that allows the transfer of all pipe's data buffer size through the sclk bus in the time for data transfer*/
	/*for dmif, pte and cursor requests have to be included.*/
	results->dmif_required_sclk = bw_div(
		bw_div(
			results->total_display_reads_required_data,
			results->display_reads_time_for_data_transfer),
		vbios->data_return_bus_width);
	results->mcifwr_required_sclk = bw_div(
		bw_div(
			results->total_display_writes_required_data,
			results->display_writes_time_for_data_transfer),
		vbios->data_return_bus_width);
	if (bw_mtn(
		results->scatter_gather_total_pte_requests,
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		results->required_sclk = bw_int_to_fixed(9999);
		sclk_message =
			bw_def_exceeded_allowed_outstanding_pte_req_queue_size;
		results->sclk_level = high;
	} else if (bw_mtn(
		vbios->dmifmc_urgent_latency,
		results->required_dmifmc_urgent_latency_for_page_close_open)
		|| bw_mtn(
			vbios->mcifwrmc_urgent_latency,
			results->required_mcifmcwr_urgent_latency)) {
		results->required_sclk = bw_int_to_fixed(9999);
		sclk_message = bw_def_exceeded_allowed_page_close_open;
		results->sclk_level = high;
	} else {
		results->required_sclk = bw_max2(
			results->dmif_required_sclk,
			results->mcifwr_required_sclk);
		if (bw_ltn(results->required_sclk, sclk[low])
			&& (results->cpup_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[results->y_clk_level][low],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[results->y_clk_level][low],
						vbios->high_voltage_max_dispclk)))
			&& (results->cpuc_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[results->y_clk_level][low],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[results->y_clk_level][low],
						vbios->high_voltage_max_dispclk)
					&& bw_ltn(
						results->dispclk_required_for_blackout_recovery[results->y_clk_level][low],
						vbios->high_voltage_max_dispclk)))
			&& (results->nbp_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->dram_speed_change_margin[results->y_clk_level][low],
					bw_int_to_fixed(0))
					&& bw_leq(
						results->dispclk_required_for_dram_speed_change[results->y_clk_level][low],
						vbios->high_voltage_max_dispclk)))) {
			sclk_message = bw_def_low;
			results->sclk_level = low;
		} else if (bw_ltn(results->required_sclk, sclk[mid])
			&& (results->cpup_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[results->y_clk_level][mid],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[results->y_clk_level][mid],
						vbios->high_voltage_max_dispclk)))
			&& (results->cpuc_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->blackout_duration_margin[results->y_clk_level][mid],
					bw_int_to_fixed(0))
					&& bw_ltn(
						results->dispclk_required_for_blackout_duration[results->y_clk_level][mid],
						vbios->high_voltage_max_dispclk)
					&& bw_ltn(
						results->dispclk_required_for_blackout_recovery[results->y_clk_level][mid],
						vbios->high_voltage_max_dispclk)))
			&& (results->nbp_state_change_enable == bw_def_no
				|| (bw_mtn(
					results->dram_speed_change_margin[results->y_clk_level][mid],
					bw_int_to_fixed(0))
					&& bw_leq(
						results->dispclk_required_for_dram_speed_change[results->y_clk_level][mid],
						vbios->high_voltage_max_dispclk)))) {
			sclk_message = bw_def_mid;
			results->sclk_level = mid;
		} else if (bw_ltn(results->required_sclk, sclk[high])) {
			sclk_message = bw_def_high;
			results->sclk_level = high;
		} else {
			sclk_message = bw_def_exceeded_allowed_maximum_sclk;
			results->sclk_level = high;
		}
	}
	/*dispclk*/
	/*if dispclk is set to the maximum, ramping is not required.  dispclk required without ramping is less than the dispclk required with ramping.*/
	/*if dispclk required without ramping is more than the maximum dispclk, that is the dispclk required, and the mode is not supported*/
	/*if that does not happen, but dispclk required with ramping is more than the maximum dispclk, dispclk required is just the maximum dispclk*/
	/*if that does not happen either, dispclk required is the dispclk required with ramping.*/
	/*dispclk required without ramping is the maximum of the one required for display pipe pixel throughput, for scaler throughput, for total read request thrrougput and for dram/np p-state change if enabled.*/
	/*the display pipe pixel throughput is the maximum of lines in per line out in the beginning of the frame and lines in per line out in the middle of the frame multiplied by the horizontal blank and chunk granularity factor, altogether multiplied by the ratio of the source width to the line time, divided by the line buffer pixels per dispclk throughput, and multiplied by the display pipe throughput factor.*/
	/*the horizontal blank and chunk granularity factor is the ratio of the line time divided by the line time minus half the horizontal blank and chunk time.  it applies when the lines in per line out is not 2 or 4.*/
	/*the dispclk required for scaler throughput is the product of the pixel rate and the scaling limits factor.*/
	/*the dispclk required for total read request throughput is the product of the peak request-per-second bandwidth and the dispclk cycles per request, divided by the request efficiency.*/
	/*for the dispclk required with ramping, instead of multiplying just the pipe throughput by the display pipe throughput factor, we multiply the scaler and pipe throughput by the ramping factor.*/
	/*the scaling limits factor is the product of the horizontal scale ratio, and the ratio of the vertical taps divided by the scaler efficiency clamped to at least 1.*/
	/*the scaling limits factor itself it also clamped to at least 1*/
	/*if doing downscaling with the pre-downscaler enabled, the horizontal scale ratio should not be considered above (use "1")*/
	results->downspread_factor = bw_add(
		bw_int_to_fixed(1),
		bw_div(vbios->down_spread_percentage, bw_int_to_fixed(100)));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] == bw_def_graphics) {
				switch (results->lb_bpc[i]) {
				case 6:
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency6_bit_per_component;
					break;
				case 8:
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency8_bit_per_component;
					break;
				case 10:
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency10_bit_per_component;
					break;
				default:
					results->v_scaler_efficiency =
						dceip->graphics_vscaler_efficiency12_bit_per_component;
					break;
				}
				if (results->use_alpha[i] == 1) {
					results->v_scaler_efficiency =
						bw_min2(
							results->v_scaler_efficiency,
							dceip->alpha_vscaler_efficiency);
				}
			} else {
				switch (results->lb_bpc[i]) {
				case 6:
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency6_bit_per_component;
					break;
				case 8:
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency8_bit_per_component;
					break;
				case 10:
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency10_bit_per_component;
					break;
				default:
					results->v_scaler_efficiency =
						dceip->underlay_vscaler_efficiency12_bit_per_component;
					break;
				}
			}
			if (dceip->pre_downscaler_enabled
				&& bw_mtn(
					results->hsr[i],
					bw_int_to_fixed(1))) {
				results->scaler_limits_factor =
					bw_max2(
						bw_div(
							results->v_taps[i],
							results->v_scaler_efficiency),
						bw_div(
							results->source_width_rounded_up_to_chunks[i],
							results->h_total[i]));
			} else {
				results->scaler_limits_factor =
					bw_max3(
						bw_int_to_fixed(1),
						bw_ceil2(
							bw_div(
								results->h_taps[i],
								bw_int_to_fixed(
									4)),
							bw_int_to_fixed(1)),
						bw_mul(
							results->hsr[i],
							bw_max2(
								bw_div(
									results->v_taps[i],
									results->v_scaler_efficiency),
								bw_int_to_fixed(
									1))));
			}
			results->display_pipe_pixel_throughput =
				bw_div(
					bw_div(
						bw_mul(
							bw_max2(
								results->lb_lines_in_per_line_out_in_beginning_of_frame[i],
								bw_mul(
									results->lb_lines_in_per_line_out_in_middle_of_frame[i],
									results->horizontal_blank_and_chunk_granularity_factor[i])),
							results->source_width_rounded_up_to_chunks[i]),
						(bw_div(
							results->h_total[i],
							results->pixel_rate[i]))),
					dceip->lb_write_pixels_per_dispclk);
			results->dispclk_required_without_ramping[i] =
				bw_mul(
					results->downspread_factor,
					bw_max2(
						bw_mul(
							results->pixel_rate[i],
							results->scaler_limits_factor),
						bw_mul(
							dceip->display_pipe_throughput_factor,
							results->display_pipe_pixel_throughput)));
			results->dispclk_required_with_ramping[i] =
				bw_mul(
					dceip->dispclk_ramping_factor,
					bw_max2(
						bw_mul(
							results->pixel_rate[i],
							results->scaler_limits_factor),
						results->display_pipe_pixel_throughput));
		}
	}
	results->total_dispclk_required_with_ramping = bw_int_to_fixed(0);
	results->total_dispclk_required_without_ramping = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_ltn(
				results->total_dispclk_required_with_ramping,
				results->dispclk_required_with_ramping[i])) {
				results->total_dispclk_required_with_ramping =
					results->dispclk_required_with_ramping[i];
			}
			if (bw_ltn(
				results->total_dispclk_required_without_ramping,
				results->dispclk_required_without_ramping[i])) {
				results->total_dispclk_required_without_ramping =
					results->dispclk_required_without_ramping[i];
			}
		}
	}
	results->total_read_request_bandwidth = bw_int_to_fixed(0);
	results->total_write_request_bandwidth = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				results->total_read_request_bandwidth = bw_add(
					results->total_read_request_bandwidth,
					results->request_bandwidth[i]);
			} else {
				results->total_write_request_bandwidth = bw_add(
					results->total_write_request_bandwidth,
					results->request_bandwidth[i]);
			}
		}
	}
	results->dispclk_required_for_total_read_request_bandwidth = bw_div(
		bw_mul(
			results->total_read_request_bandwidth,
			dceip->dispclk_per_request),
		dceip->request_efficiency);
	if (dceip->dcfclk_request_generation == 0) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_with_ramping,
				results->dispclk_required_for_total_read_request_bandwidth);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_without_ramping,
				results->dispclk_required_for_total_read_request_bandwidth);
	} else {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			results->total_dispclk_required_with_ramping;
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			results->total_dispclk_required_without_ramping;
	}
	if (results->cpuc_state_change_enable == bw_def_yes) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max3(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[results->y_clk_level][results->sclk_level],
				results->dispclk_required_for_blackout_recovery[results->y_clk_level][results->sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max3(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[results->y_clk_level][results->sclk_level],
				results->dispclk_required_for_blackout_recovery[results->y_clk_level][results->sclk_level]);
	}
	if (results->cpup_state_change_enable == bw_def_yes) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[results->y_clk_level][results->sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_blackout_duration[results->y_clk_level][results->sclk_level]);
	}
	if (results->nbp_state_change_enable == bw_def_yes) {
		results->total_dispclk_required_with_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_with_ramping_with_request_bandwidth,
				results->dispclk_required_for_dram_speed_change[results->y_clk_level][results->sclk_level]);
		results->total_dispclk_required_without_ramping_with_request_bandwidth =
			bw_max2(
				results->total_dispclk_required_without_ramping_with_request_bandwidth,
				results->dispclk_required_for_dram_speed_change[results->y_clk_level][results->sclk_level]);
	}
	if (bw_ltn(
		results->total_dispclk_required_with_ramping_with_request_bandwidth,
		vbios->high_voltage_max_dispclk)) {
		results->dispclk =
			results->total_dispclk_required_with_ramping_with_request_bandwidth;
	} else if (bw_ltn(
		results->total_dispclk_required_without_ramping_with_request_bandwidth,
		vbios->high_voltage_max_dispclk)) {
		results->dispclk = vbios->high_voltage_max_dispclk;
	} else {
		results->dispclk =
			results->total_dispclk_required_without_ramping_with_request_bandwidth;
	}
	/* required core voltage*/
	/* the core voltage required is low if sclk, yclk(pclk)and dispclk are within the low limits*/
	/* otherwise, the core voltage required is medium if yclk (pclk) is within the low limit and sclk and dispclk are within the medium limit*/
	/* otherwise, the core voltage required is high if the three clocks are within the high limits*/
	/* otherwise, or if the mode is not supported, core voltage requirement is not applicable*/
	if (pipe_check == bw_def_notok) {
		voltage = bw_def_na;
	} else if (mode_check == bw_def_notok) {
		voltage = bw_def_notok;
	} else if (yclk_message == bw_def_low && sclk_message == bw_def_low
		&& bw_ltn(results->dispclk, vbios->low_voltage_max_dispclk)) {
		voltage = bw_def_low;
	} else if (yclk_message == bw_def_low
		&& (sclk_message == bw_def_low || sclk_message == bw_def_mid)
		&& bw_ltn(results->dispclk, vbios->mid_voltage_max_dispclk)) {
		voltage = bw_def_mid;
	} else if ((yclk_message == bw_def_low || yclk_message == bw_def_high)
		&& (sclk_message == bw_def_low || sclk_message == bw_def_mid
			|| sclk_message == bw_def_high)
		&& bw_leq(results->dispclk, vbios->high_voltage_max_dispclk)) {
		if (results->nbp_state_change_enable == bw_def_yes) {
			voltage = bw_def_high;
		} else {
			voltage = bw_def_high_no_nbp_state_change;
		}
	} else {
		voltage = bw_def_notok;
	}
	/*required blackout recovery time*/
	results->blackout_recovery_time = bw_int_to_fixed(0);
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		if (results->enable[k]
			&& bw_mtn(vbios->blackout_duration, bw_int_to_fixed(0))
			&& results->cpup_state_change_enable == bw_def_yes) {
			if (surface_type[k] != bw_def_display_write_back420_luma
				&& surface_type[k]
					!= bw_def_display_write_back420_chroma) {
				results->blackout_recovery_time =
					bw_max2(
						results->blackout_recovery_time,
						bw_add(
							bw_mul(
								bw_int_to_fixed(
									2),
								results->total_dmifmc_urgent_latency),
							results->dmif_burst_time[results->y_clk_level][results->sclk_level]));
				if (bw_ltn(
					results->adjusted_data_buffer_size[k],
					bw_mul(
						bw_div(
							bw_mul(
								results->display_bandwidth[k],
								results->useful_bytes_per_request[k]),
							results->bytes_per_request[k]),
						(bw_add(
							vbios->blackout_duration,
							bw_add(
								bw_mul(
									bw_int_to_fixed(
										2),
									results->total_dmifmc_urgent_latency),
								results->dmif_burst_time[results->y_clk_level][results->sclk_level])))))) {
					results->blackout_recovery_time =
						bw_max2(
							results->blackout_recovery_time,
							bw_div(
								(bw_add(
									bw_mul(
										bw_div(
											bw_mul(
												results->display_bandwidth[k],
												results->useful_bytes_per_request[k]),
											results->bytes_per_request[k]),
										vbios->blackout_duration),
									bw_sub(
										bw_div(
											bw_mul(
												bw_mul(
													bw_mul(
														(bw_add(
															bw_mul(
																bw_int_to_fixed(
																	2),
																results->total_dmifmc_urgent_latency),
															results->dmif_burst_time[results->y_clk_level][results->sclk_level])),
														results->dispclk),
													bw_int_to_fixed(
														results->bytes_per_pixel[k])),
												results->lines_interleaved_in_mem_access[k]),
											results->latency_hiding_lines[k]),
										results->adjusted_data_buffer_size[k]))),
								(bw_sub(
									bw_div(
										bw_mul(
											bw_mul(
												results->dispclk,
												bw_int_to_fixed(
													results->bytes_per_pixel[k])),
											results->lines_interleaved_in_mem_access[k]),
										results->latency_hiding_lines[k]),
									bw_div(
										bw_mul(
											results->display_bandwidth[k],
											results->useful_bytes_per_request[k]),
										results->bytes_per_request[k])))));
				}
			} else {
				results->blackout_recovery_time =
					bw_max2(
						results->blackout_recovery_time,
						bw_add(
							bw_mul(
								bw_int_to_fixed(
									2),
								vbios->mcifwrmc_urgent_latency),
							results->mcifwr_burst_time[results->y_clk_level][results->sclk_level]));
				if (bw_ltn(
					results->adjusted_data_buffer_size[k],
					bw_mul(
						bw_div(
							bw_mul(
								results->display_bandwidth[k],
								results->useful_bytes_per_request[k]),
							results->bytes_per_request[k]),
						(bw_add(
							vbios->blackout_duration,
							bw_add(
								bw_mul(
									bw_int_to_fixed(
										2),
									vbios->mcifwrmc_urgent_latency),
								results->mcifwr_burst_time[results->y_clk_level][results->sclk_level])))))) {
					results->blackout_recovery_time =
						bw_max2(
							results->blackout_recovery_time,
							bw_div(
								(bw_add(
									bw_mul(
										bw_div(
											bw_mul(
												results->display_bandwidth[k],
												results->useful_bytes_per_request[k]),
											results->bytes_per_request[k]),
										vbios->blackout_duration),
									bw_sub(
										bw_div(
											bw_mul(
												bw_mul(
													bw_mul(
														(bw_add(
															bw_add(
																bw_mul(
																	bw_int_to_fixed(
																		2),
																	vbios->mcifwrmc_urgent_latency),
																results->dmif_burst_time[i][j]),
															results->mcifwr_burst_time[results->y_clk_level][results->sclk_level])),
														results->dispclk),
													bw_int_to_fixed(
														results->bytes_per_pixel[k])),
												results->lines_interleaved_in_mem_access[k]),
											results->latency_hiding_lines[k]),
										results->adjusted_data_buffer_size[k]))),
								(bw_sub(
									bw_div(
										bw_mul(
											bw_mul(
												results->dispclk,
												bw_int_to_fixed(
													results->bytes_per_pixel[k])),
											results->lines_interleaved_in_mem_access[k]),
										results->latency_hiding_lines[k]),
									bw_div(
										bw_mul(
											results->display_bandwidth[k],
											results->useful_bytes_per_request[k]),
										results->bytes_per_request[k])))));
				}
			}
		}
	}
	/*sclk deep sleep*/
	/*during self-refresh, sclk can be reduced to dispclk divided by the minimum pixels in the data fifo entry, with 15% margin, but shoudl not be set to less than the request bandwidth.*/
	/*the data fifo entry is 16 pixels for the writeback, 64 bytes/bytes_per_pixel for the graphics, 16 pixels for the parallel rotation underlay,*/
	/*and 16 bytes/bytes_per_pixel for the orthogonal rotation underlay.*/
	/*in parallel mode (underlay pipe), the data read from the dmifv buffer is variable and based on the pixel depth (8bbp - 16 bytes, 16 bpp - 32 bytes, 32 bpp - 64 bytes)*/
	/*in orthogonal mode (underlay pipe), the data read from the dmifv buffer is fixed at 16 bytes.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (surface_type[i] == bw_def_display_write_back420_luma
				|| surface_type[i]
					== bw_def_display_write_back420_chroma) {
				results->pixels_per_data_fifo_entry[i] =
					bw_int_to_fixed(16);
			} else if (surface_type[i] == bw_def_graphics) {
				results->pixels_per_data_fifo_entry[i] = bw_div(
					bw_int_to_fixed(64),
					bw_int_to_fixed(
						results->bytes_per_pixel[i]));
			} else if (results->orthogonal_rotation[i] == 0) {
				results->pixels_per_data_fifo_entry[i] =
					bw_int_to_fixed(16);
			} else {
				results->pixels_per_data_fifo_entry[i] = bw_div(
					bw_int_to_fixed(16),
					bw_int_to_fixed(
						results->bytes_per_pixel[i]));
			}
		}
	}
	results->min_pixels_per_data_fifo_entry = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_mtn(
				results->min_pixels_per_data_fifo_entry,
				results->pixels_per_data_fifo_entry[i])) {
				results->min_pixels_per_data_fifo_entry =
					results->pixels_per_data_fifo_entry[i];
			}
		}
	}
	results->sclk_deep_sleep = bw_max2(
		bw_div(
			bw_mul(results->dispclk, bw_frc_to_fixed(115, 100)),
			results->min_pixels_per_data_fifo_entry),
		results->total_read_request_bandwidth);
	/*urgent, stutter and nb-p_state watermark*/
	/*the urgent watermark is the maximum of the urgent trip time plus the pixel transfer time, the urgent trip times to get data for the first pixel, and the urgent trip times to get data for the last pixel.*/
	/*the stutter exit watermark is the self refresh exit time plus the maximum of the data burst time plus the pixel transfer time, the data burst times to get data for the first pixel, and the data burst times to get data for the last pixel.  it does not apply to the writeback.*/
	/*the nb p-state change watermark is the dram speed/p-state change time plus the maximum of the data burst time plus the pixel transfer time, the data burst times to get data for the first pixel, and the data burst times to get data for the last pixel.*/
	/*the pixel transfer time is the maximum of the time to transfer the source pixels required for the first output pixel, and the time to transfer the pixels for the last output pixel minus the active line time.*/
	/*blackout_duration is added to the urgent watermark*/
	results->chunk_request_time = bw_int_to_fixed(0);
	results->cursor_request_time = bw_int_to_fixed(0);
	/*compute total time to request one chunk from each active display pipe*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->chunk_request_time =
				bw_add(
					results->chunk_request_time,
					(bw_div(
						(bw_div(
							bw_int_to_fixed(
								pixels_per_chunk
									* results->bytes_per_pixel[i]),
							results->useful_bytes_per_request[i])),
						bw_min2(
							sclk[results->sclk_level],
							bw_div(
								results->dispclk,
								bw_int_to_fixed(
									2))))));
		}
	}
	/*compute total time to request cursor data*/
	results->cursor_request_time = (bw_div(
		results->cursor_total_data,
		(bw_mul(bw_int_to_fixed(32), sclk[results->sclk_level]))));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->line_source_pixels_transfer_time =
				bw_max2(
					bw_div(
						bw_div(
							results->src_pixels_for_first_output_pixel[i],
							dceip->lb_write_pixels_per_dispclk),
						(bw_div(
							results->dispclk,
							dceip->display_pipe_throughput_factor))),
					bw_sub(
						bw_div(
							bw_div(
								results->src_pixels_for_last_output_pixel[i],
								dceip->lb_write_pixels_per_dispclk),
							(bw_div(
								results->dispclk,
								dceip->display_pipe_throughput_factor))),
						results->active_time[i]));
			if (surface_type[i] != bw_def_display_write_back420_luma
				&& surface_type[i]
					!= bw_def_display_write_back420_chroma) {
				results->urgent_watermark[i] =
					bw_add(
						bw_add(
							bw_add(
								bw_add(
									bw_add(
										results->total_dmifmc_urgent_latency,
										results->dmif_burst_time[results->y_clk_level][results->sclk_level]),
									bw_max2(
										results->line_source_pixels_transfer_time,
										results->line_source_transfer_time[i][results->y_clk_level][results->sclk_level])),
								vbios->blackout_duration),
							results->chunk_request_time),
						results->cursor_request_time);
				results->stutter_exit_watermark[i] =
					bw_add(
						bw_sub(
							vbios->stutter_self_refresh_exit_latency,
							results->total_dmifmc_urgent_latency),
						results->urgent_watermark[i]);
				/*unconditionally remove black out time from the nb p_state watermark*/
				results->nbp_state_change_watermark[i] =
					bw_sub(
						bw_add(
							bw_sub(
								vbios->nbp_state_change_latency,
								results->total_dmifmc_urgent_latency),
							results->urgent_watermark[i]),
						vbios->blackout_duration);
			} else {
				results->urgent_watermark[i] =
					bw_add(
						bw_add(
							bw_add(
								bw_add(
									bw_add(
										vbios->mcifwrmc_urgent_latency,
										results->mcifwr_burst_time[results->y_clk_level][results->sclk_level]),
									bw_max2(
										results->line_source_pixels_transfer_time,
										results->line_source_transfer_time[i][results->y_clk_level][results->sclk_level])),
								vbios->blackout_duration),
							results->chunk_request_time),
						results->cursor_request_time);
				results->stutter_exit_watermark[i] =
					bw_int_to_fixed(0);
				results->nbp_state_change_watermark[i] =
					bw_add(
						bw_sub(
							bw_add(
								vbios->nbp_state_change_latency,
								results->dmif_burst_time[results->y_clk_level][results->sclk_level]),
							vbios->mcifwrmc_urgent_latency),
						results->urgent_watermark[i]);
			}
		}
	}
	/*stutter mode enable*/
	/*in the multi-display case the stutter exit watermark cannot exceed the cursor dcp buffer size*/
	results->stutter_mode_enable = results->cpuc_state_change_enable;
	if (mode_data->number_of_displays > 1) {
		for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
			if (results->enable[i]) {
				if (bw_mtn(
					results->stutter_exit_watermark[i],
					results->cursor_latency_hiding[i])) {
					results->stutter_mode_enable =
						bw_def_no;
				}
			}
		}
	}
	/*performance metrics*/
	/* display read access efficiency (%)*/
	/* display write back access efficiency (%)*/
	/* stutter efficiency (%)*/
	/* extra underlay pitch recommended for efficiency (pixels)*/
	/* immediate flip time (us)*/
	/* latency for other clients due to urgent display read (us)*/
	/* latency for other clients due to urgent display write (us)*/
	/* average bandwidth consumed by display (no compression) (gb/s)*/
	/* required dram  bandwidth (gb/s)*/
	/* required sclk (m_hz)*/
	/* required rd urgent latency (us)*/
	/* nb p-state change margin (us)*/
	/*dmif and mcifwr dram access efficiency*/
	/*is the ratio between the ideal dram access time (which is the data buffer size in memory divided by the dram bandwidth), and the actual time which is the total page close-open time.  but it cannot exceed the dram efficiency provided by the memory subsystem*/
	results->dmifdram_access_efficiency =
		bw_min2(
			bw_div(
				bw_div(
					results->total_display_reads_required_dram_access_data,
					results->dram_bandwidth),
				results->dmif_total_page_close_open_time),
			bw_int_to_fixed(1));
	if (bw_mtn(
		results->total_display_writes_required_dram_access_data,
		bw_int_to_fixed(0))) {
		results->mcifwrdram_access_efficiency =
			bw_min2(
				bw_div(
					bw_div(
						results->total_display_writes_required_dram_access_data,
						results->dram_bandwidth),
					results->mcifwr_total_page_close_open_time),
				bw_int_to_fixed(1));
	} else {
		results->mcifwrdram_access_efficiency = bw_int_to_fixed(0);
	}
	/*average bandwidth*/
	/*the average bandwidth with no compression is the vertical active time is the source width times the bytes per pixel divided by the line time, multiplied by the vertical scale ratio and the ratio of bytes per request divided by the useful bytes per request.*/
	/*the average bandwidth with compression is the same, divided by the compression ratio*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->average_bandwidth_no_compression[i] =
				bw_div(
					bw_mul(
						bw_mul(
							bw_div(
								bw_mul(
									results->source_width_rounded_up_to_chunks[i],
									bw_int_to_fixed(
										results->bytes_per_pixel[i])),
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
	results->total_average_bandwidth_no_compression = bw_int_to_fixed(0);
	results->total_average_bandwidth = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->total_average_bandwidth_no_compression =
				bw_add(
					results->total_average_bandwidth_no_compression,
					results->average_bandwidth_no_compression[i]);
			results->total_average_bandwidth = bw_add(
				results->total_average_bandwidth,
				results->average_bandwidth[i]);
		}
	}
	/*stutter efficiency*/
	/*the stutter efficiency is the frame-average time in self-refresh divided by the frame-average stutter cycle duration.  only applies if the display write-back is not enabled.*/
	/*the frame-average stutter cycle used is the minimum for all pipes of the frame-average data buffer size in time, times the compression rate*/
	/*the frame-average time in self-refresh is the stutter cycle minus the self refresh exit latency and the burst time*/
	/*the stutter cycle is the dmif buffer size reduced by the excess of the stutter exit watermark over the lb size in time.*/
	/*the burst time is the data needed during the stutter cycle divided by the available bandwidth*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->stutter_cycle_duration[i] =
				bw_sub(
					bw_mul(
						bw_div(
							bw_div(
								bw_mul(
									bw_div(
										bw_div(
											results->adjusted_data_buffer_size[i],
											bw_int_to_fixed(
												results->bytes_per_pixel[i])),
										results->source_width_rounded_up_to_chunks[i]),
									results->h_total[i]),
								results->vsr[i]),
							results->pixel_rate[i]),
						results->compression_rate[i]),
					bw_max2(
						bw_int_to_fixed(0),
						bw_sub(
							results->stutter_exit_watermark[i],
							bw_div(
								bw_mul(
									bw_int_to_fixed(
										(2
											+ results->line_buffer_prefetch[i])),
									results->h_total[i]),
								results->pixel_rate[i]))));
		}
	}
	results->total_stutter_cycle_duration = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			if (bw_mtn(
				results->total_stutter_cycle_duration,
				results->stutter_cycle_duration[i])) {
				results->total_stutter_cycle_duration =
					results->stutter_cycle_duration[i];
			}
		}
	}
	results->stutter_burst_time = bw_div(
		bw_mul(
			results->total_stutter_cycle_duration,
			results->total_average_bandwidth),
		bw_min2(
			(bw_mul(
				results->dram_bandwidth,
				results->dmifdram_access_efficiency)),
			bw_mul(
				sclk[results->sclk_level],
				vbios->data_return_bus_width)));
	results->time_in_self_refresh = bw_sub(
		bw_sub(
			results->total_stutter_cycle_duration,
			vbios->stutter_self_refresh_exit_latency),
		results->stutter_burst_time);
	if (mode_data->d1_display_write_back_dwb_enable == 1) {
		results->stutter_efficiency = bw_int_to_fixed(0);
	} else if (bw_ltn(results->time_in_self_refresh, bw_int_to_fixed(0))) {
		results->stutter_efficiency = bw_int_to_fixed(0);
	} else {
		results->stutter_efficiency = bw_mul(
			bw_div(
				results->time_in_self_refresh,
				results->total_stutter_cycle_duration),
			bw_int_to_fixed(100));
	}
	/*immediate flip time*/
	/*if scatter gather is enabled, the immediate flip takes a number of urgent memory trips equivalent to the pte requests in a row divided by the pte request limit.*/
	/*otherwise, it may take just one urgenr memory trip*/
	results->worst_number_of_trips_to_memory = bw_int_to_fixed(1);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]
			&& results->scatter_gather_enable_for_pipe[i] == 1) {
			results->number_of_trips_to_memory_for_getting_apte_row[i] =
				bw_ceil2(
					bw_div(
						results->scatter_gather_pte_requests_in_row[i],
						results->scatter_gather_pte_request_limit[i]),
					bw_int_to_fixed(1));
			if (bw_ltn(
				results->worst_number_of_trips_to_memory,
				results->number_of_trips_to_memory_for_getting_apte_row[i])) {
				results->worst_number_of_trips_to_memory =
					results->number_of_trips_to_memory_for_getting_apte_row[i];
			}
		}
	}
	results->immediate_flip_time = bw_mul(
		results->worst_number_of_trips_to_memory,
		results->total_dmifmc_urgent_latency);
	/*worst latency for other clients*/
	/*it is the urgent latency plus the urgent burst time*/
	results->latency_for_non_dmif_clients =
		bw_add(
			results->total_dmifmc_urgent_latency,
			results->dmif_burst_time[results->y_clk_level][results->sclk_level]);
	if (mode_data->d1_display_write_back_dwb_enable == 1) {
		results->latency_for_non_mcifwr_clients = bw_add(
			vbios->mcifwrmc_urgent_latency,
			dceip->mcifwr_all_surfaces_burst_time);
	} else {
		results->latency_for_non_mcifwr_clients = bw_int_to_fixed(0);
	}
	/*dmif mc urgent latency suppported in high sclk and yclk*/
	results->dmifmc_urgent_latency_supported_in_high_sclk_and_yclk = bw_div(
		(bw_sub(
			results->min_read_buffer_size_in_time,
			results->dmif_burst_time[high][high])),
		results->total_dmifmc_urgent_trips);
	/*dram speed/p-state change margin*/
	/*in the multi-display case the nb p-state change watermark cannot exceed the average lb size plus the dmif size or the cursor dcp buffer size*/
	results->nbp_state_dram_speed_change_margin = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (results->enable[i]) {
			results->nbp_state_dram_speed_change_margin =
				bw_min2(
					results->nbp_state_dram_speed_change_margin,
					bw_sub(
						results->maximum_latency_hiding_with_cursor[i],
						results->nbp_state_change_watermark[i]));
		}
	}
	/*sclk required vs urgent latency*/
	for (i = 1; i <= 5; i++) {
		results->display_reads_time_for_data_transfer_and_urgent_latency =
			bw_sub(
				results->min_read_buffer_size_in_time,
				bw_mul(
					results->total_dmifmc_urgent_trips,
					bw_int_to_fixed(i)));
		if (pipe_check == bw_def_ok
			&& (bw_mtn(
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
				bw_int_to_fixed(bw_def_na);
		}
	}
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
void bw_calcs_init(struct bw_calcs_dceip *bw_dceip,
	struct bw_calcs_vbios *bw_vbios,
	enum bw_calcs_version version)
{
	struct bw_calcs_dceip dceip = {{ 0 }};
	struct bw_calcs_vbios vbios = { 0 };

	switch (version) {
	case BW_CALCS_VERSION_CARRIZO:
		vbios.number_of_dram_channels = 2;
		vbios.dram_channel_width_in_bits = 64;
		vbios.number_of_dram_banks = 8;
		vbios.high_yclk = bw_int_to_fixed(1600);
		vbios.mid_yclk = bw_int_to_fixed(1600);
		vbios.low_yclk = bw_frc_to_fixed(66666, 100);
		vbios.low_sclk = bw_int_to_fixed(200);
		vbios.mid_sclk = bw_int_to_fixed(300);
		vbios.high_sclk = bw_frc_to_fixed(62609, 100);
		vbios.low_voltage_max_dispclk = bw_int_to_fixed(352);
		vbios.mid_voltage_max_dispclk = bw_int_to_fixed(467);
		vbios.high_voltage_max_dispclk = bw_int_to_fixed(643);
		vbios.data_return_bus_width = bw_int_to_fixed(32);
		vbios.trc = bw_int_to_fixed(50);
		vbios.dmifmc_urgent_latency = bw_int_to_fixed(4);
		vbios.stutter_self_refresh_exit_latency = bw_frc_to_fixed(
			153,
			10);
		vbios.nbp_state_change_latency = bw_frc_to_fixed(19649, 1000);
		vbios.mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios.scatter_gather_enable = true;
		vbios.down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios.cursor_width = 32;
		vbios.average_compression_rate = 4;
		vbios.number_of_request_slots_gmc_reserves_for_dmif_per_channel =
			256;
		vbios.blackout_duration = bw_int_to_fixed(18); /* us */
		vbios.maximum_blackout_recovery_time = bw_int_to_fixed(20);

		dceip.dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip.de_tiling_buffer = bw_int_to_fixed(0);
		dceip.dcfclk_request_generation = 0;
		dceip.lines_interleaved_into_lb = 2;
		dceip.chunk_width = 256;
		dceip.number_of_graphics_pipes = 3;
		dceip.number_of_underlay_pipes = 1;
		dceip.display_write_back_supported = false;
		dceip.argb_compression_support = false;
		dceip.underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip.underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip.graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip.max_dmif_buffer_allocated = 2;
		dceip.graphics_dmif_size = 12288;
		dceip.underlay_luma_dmif_size = 19456;
		dceip.underlay_chroma_dmif_size = 23552;
		dceip.pre_downscaler_enabled = true;
		dceip.underlay_downscale_prefetch_enabled = true;
		dceip.lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip.lb_size_per_component444 = bw_int_to_fixed(82176);
		dceip.graphics_lb_nodownscaling_multi_line_prefetching = false;
		dceip.stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(0);
		dceip.underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip.underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.cursor_chunk_width = bw_int_to_fixed(64);
		dceip.cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip.cursor_memory_interface_buffer_pixels = bw_int_to_fixed(
			64);
		dceip.underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip.underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip.peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip.peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip.minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip.maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip.limit_excessive_outstanding_dmif_requests = true;
		dceip.linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip.scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip.display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip.display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip.request_efficiency = bw_frc_to_fixed(8, 10);
		dceip.dispclk_per_request = bw_int_to_fixed(2);
		dceip.dispclk_ramping_factor = bw_frc_to_fixed(11, 10);
		dceip.display_pipe_throughput_factor = bw_frc_to_fixed(
			105,
			100);
		dceip.scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip.mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0); /* todo: this is a bug*/
		break;
	case BW_CALCS_VERSION_ELLESMERE:
		vbios.number_of_dram_channels = 8;
		vbios.dram_channel_width_in_bits = 32;
		vbios.number_of_dram_banks = 8;
		vbios.high_yclk = bw_int_to_fixed(6000);
		vbios.mid_yclk = bw_int_to_fixed(3200);
		vbios.low_yclk = bw_int_to_fixed(1000);
		vbios.low_sclk = bw_int_to_fixed(300);
		vbios.mid_sclk = bw_int_to_fixed(974);
		vbios.high_sclk = bw_int_to_fixed(1154);
		vbios.low_voltage_max_dispclk = bw_int_to_fixed(459);
		vbios.mid_voltage_max_dispclk = bw_int_to_fixed(654);
		vbios.high_voltage_max_dispclk = bw_int_to_fixed(1132);
		vbios.data_return_bus_width = bw_int_to_fixed(32);
		vbios.trc = bw_int_to_fixed(48);
		vbios.dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios.stutter_self_refresh_exit_latency = bw_int_to_fixed(5);
		vbios.nbp_state_change_latency = bw_int_to_fixed(45);
		vbios.mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios.scatter_gather_enable = true;
		vbios.down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios.cursor_width = 32;
		vbios.average_compression_rate = 4;
		vbios.number_of_request_slots_gmc_reserves_for_dmif_per_channel =
			256;
		vbios.blackout_duration = bw_int_to_fixed(0); /* us */
		vbios.maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip.dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip.de_tiling_buffer = bw_int_to_fixed(0);
		dceip.dcfclk_request_generation = 0;
		dceip.lines_interleaved_into_lb = 2;
		dceip.chunk_width = 256;
		dceip.number_of_graphics_pipes = 6;
		dceip.number_of_underlay_pipes = 0;
		dceip.display_write_back_supported = false;
		dceip.argb_compression_support = false;
		dceip.underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip.underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip.graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip.max_dmif_buffer_allocated = 4;
		dceip.graphics_dmif_size = 12288;
		dceip.underlay_luma_dmif_size = 19456;
		dceip.underlay_chroma_dmif_size = 23552;
		dceip.pre_downscaler_enabled = true;
		dceip.underlay_downscale_prefetch_enabled = true;
		dceip.lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip.lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip.graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip.stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip.underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip.underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.cursor_chunk_width = bw_int_to_fixed(64);
		dceip.cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip.cursor_memory_interface_buffer_pixels = bw_int_to_fixed(
			64);
		dceip.underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip.underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip.peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip.peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip.minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip.maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip.limit_excessive_outstanding_dmif_requests = true;
		dceip.linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip.scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip.display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip.display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip.request_efficiency = bw_frc_to_fixed(8, 10);
		dceip.dispclk_per_request = bw_int_to_fixed(2);
		dceip.dispclk_ramping_factor = bw_frc_to_fixed(11, 10);
		dceip.display_pipe_throughput_factor = bw_frc_to_fixed(
			105,
			100);
		dceip.scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip.mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	case BW_CALCS_VERSION_BAFFIN:
		vbios.number_of_dram_channels = 4;
		vbios.dram_channel_width_in_bits = 32;
		vbios.number_of_dram_banks = 8;
		vbios.high_yclk = bw_int_to_fixed(6000);
		vbios.mid_yclk = bw_int_to_fixed(3200);
		vbios.low_yclk = bw_int_to_fixed(1000);
		vbios.low_sclk = bw_int_to_fixed(300);
		vbios.mid_sclk = bw_int_to_fixed(974);
		vbios.high_sclk = bw_int_to_fixed(1154);
		vbios.low_voltage_max_dispclk = bw_int_to_fixed(459);
		vbios.mid_voltage_max_dispclk = bw_int_to_fixed(654);
		vbios.high_voltage_max_dispclk = bw_int_to_fixed(1132);
		vbios.data_return_bus_width = bw_int_to_fixed(32);
		vbios.trc = bw_int_to_fixed(48);
		vbios.dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios.stutter_self_refresh_exit_latency = bw_int_to_fixed(5);
		vbios.nbp_state_change_latency = bw_int_to_fixed(45);
		vbios.mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios.scatter_gather_enable = true;
		vbios.down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios.cursor_width = 32;
		vbios.average_compression_rate = 4;
		vbios.number_of_request_slots_gmc_reserves_for_dmif_per_channel =
			256;
		vbios.blackout_duration = bw_int_to_fixed(0); /* us */
		vbios.maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip.dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip.de_tiling_buffer = bw_int_to_fixed(0);
		dceip.dcfclk_request_generation = 0;
		dceip.lines_interleaved_into_lb = 2;
		dceip.chunk_width = 256;
		dceip.number_of_graphics_pipes = 5;
		dceip.number_of_underlay_pipes = 0;
		dceip.display_write_back_supported = false;
		dceip.argb_compression_support = false;
		dceip.underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip.underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip.graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip.graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip.graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip.alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip.max_dmif_buffer_allocated = 4;
		dceip.graphics_dmif_size = 12288;
		dceip.underlay_luma_dmif_size = 19456;
		dceip.underlay_chroma_dmif_size = 23552;
		dceip.pre_downscaler_enabled = true;
		dceip.underlay_downscale_prefetch_enabled = true;
		dceip.lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip.lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip.graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip.stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip.underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip.underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip.cursor_chunk_width = bw_int_to_fixed(64);
		dceip.cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip.cursor_memory_interface_buffer_pixels = bw_int_to_fixed(
			64);
		dceip.underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip.underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip.peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip.peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip.minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip.maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip.limit_excessive_outstanding_dmif_requests = true;
		dceip.linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip.scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip.display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip.display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip.request_efficiency = bw_frc_to_fixed(8, 10);
		dceip.dispclk_per_request = bw_int_to_fixed(2);
		dceip.dispclk_ramping_factor = bw_frc_to_fixed(11, 10);
		dceip.display_pipe_throughput_factor = bw_frc_to_fixed(
			105,
			100);
		dceip.scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip.mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	default:
		break;
	}
	*bw_dceip = dceip;
	*bw_vbios = vbios;
}

/**
 * Compare calculated (required) clocks against the clocks available at
 * maximum voltage (max Performance Level).
 */
static bool is_display_configuration_supported(
	const struct bw_calcs_vbios *vbios,
	const struct bw_calcs_output *calcs_output)
{
	uint32_t int_max_clk;

	int_max_clk = bw_fixed_to_int(vbios->high_voltage_max_dispclk);
	int_max_clk *= 1000; /* MHz to kHz */
	if (calcs_output->dispclk_khz > int_max_clk)
		return false;

	int_max_clk = bw_fixed_to_int(vbios->high_sclk);
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

bool bw_calcs(struct dc_context *ctx, const struct bw_calcs_dceip *dceip,
	const struct bw_calcs_vbios *vbios,
	const struct bw_calcs_mode_data *mode_data,
	struct bw_calcs_output *calcs_output)
{
	struct bw_calcs_results *bw_results_internal = dm_alloc(sizeof(struct bw_calcs_results));
	struct bw_calcs_mode_data_internal *bw_data_internal =
		dm_alloc(sizeof(struct bw_calcs_mode_data_internal));

	switch (mode_data->number_of_displays) {
	case (6):
		bw_data_internal->d5_htotal =
			mode_data->displays_data[5].h_total;
		bw_data_internal->d5_pixel_rate =
			mode_data->displays_data[5].pixel_rate;
		bw_data_internal->d5_graphics_src_width =
			mode_data->displays_data[5].graphics_src_width;
		bw_data_internal->d5_graphics_src_height =
			mode_data->displays_data[5].graphics_src_height;
		bw_data_internal->d5_graphics_scale_ratio =
			mode_data->displays_data[5].graphics_scale_ratio;
		bw_data_internal->d5_graphics_stereo_mode =
			mode_data->displays_data[5].graphics_stereo_mode;
		/* fall through */
	case (5):
		bw_data_internal->d4_htotal =
			mode_data->displays_data[4].h_total;
		bw_data_internal->d4_pixel_rate =
			mode_data->displays_data[4].pixel_rate;
		bw_data_internal->d4_graphics_src_width =
			mode_data->displays_data[4].graphics_src_width;
		bw_data_internal->d4_graphics_src_height =
			mode_data->displays_data[4].graphics_src_height;
		bw_data_internal->d4_graphics_scale_ratio =
			mode_data->displays_data[4].graphics_scale_ratio;
		bw_data_internal->d4_graphics_stereo_mode =
			mode_data->displays_data[4].graphics_stereo_mode;
		/* fall through */
	case (4):
		bw_data_internal->d3_htotal =
			mode_data->displays_data[3].h_total;
		bw_data_internal->d3_pixel_rate =
			mode_data->displays_data[3].pixel_rate;
		bw_data_internal->d3_graphics_src_width =
			mode_data->displays_data[3].graphics_src_width;
		bw_data_internal->d3_graphics_src_height =
			mode_data->displays_data[3].graphics_src_height;
		bw_data_internal->d3_graphics_scale_ratio =
			mode_data->displays_data[3].graphics_scale_ratio;
		bw_data_internal->d3_graphics_stereo_mode =
			mode_data->displays_data[3].graphics_stereo_mode;
		/* fall through */
	case (3):
		bw_data_internal->d2_htotal =
			mode_data->displays_data[2].h_total;
		bw_data_internal->d2_pixel_rate =
			mode_data->displays_data[2].pixel_rate;
		bw_data_internal->d2_graphics_src_width =
			mode_data->displays_data[2].graphics_src_width;
		bw_data_internal->d2_graphics_src_height =
			mode_data->displays_data[2].graphics_src_height;
		bw_data_internal->d2_graphics_scale_ratio =
			mode_data->displays_data[2].graphics_scale_ratio;
		bw_data_internal->d2_graphics_stereo_mode =
			mode_data->displays_data[2].graphics_stereo_mode;
		/* fall through */
	case (2):
		bw_data_internal->d1_display_write_back_dwb_enable = false;
		bw_data_internal->d1_underlay_mode = bw_def_none;
		bw_data_internal->d1_underlay_scale_ratio = bw_int_to_fixed(0);
		bw_data_internal->d1_htotal =
			mode_data->displays_data[1].h_total;
		bw_data_internal->d1_pixel_rate =
			mode_data->displays_data[1].pixel_rate;
		bw_data_internal->d1_graphics_src_width =
			mode_data->displays_data[1].graphics_src_width;
		bw_data_internal->d1_graphics_src_height =
			mode_data->displays_data[1].graphics_src_height;
		bw_data_internal->d1_graphics_scale_ratio =
			mode_data->displays_data[1].graphics_scale_ratio;
		bw_data_internal->d1_graphics_stereo_mode =
			mode_data->displays_data[1].graphics_stereo_mode;
		/* fall through */
	case (1):
		bw_data_internal->d0_fbc_enable =
			mode_data->displays_data[0].fbc_enable;
		bw_data_internal->d0_lpt_enable =
			mode_data->displays_data[0].lpt_enable;
		bw_data_internal->d0_underlay_mode =
			mode_data->displays_data[0].underlay_mode;
		bw_data_internal->d0_underlay_scale_ratio = bw_int_to_fixed(0);
		bw_data_internal->d0_htotal =
			mode_data->displays_data[0].h_total;
		bw_data_internal->d0_pixel_rate =
			mode_data->displays_data[0].pixel_rate;
		bw_data_internal->d0_graphics_src_width =
			mode_data->displays_data[0].graphics_src_width;
		bw_data_internal->d0_graphics_src_height =
			mode_data->displays_data[0].graphics_src_height;
		bw_data_internal->d0_graphics_scale_ratio =
			mode_data->displays_data[0].graphics_scale_ratio;
		bw_data_internal->d0_graphics_stereo_mode =
			mode_data->displays_data[0].graphics_stereo_mode;
		/* fall through */
	default:
		/* data for all displays */
		bw_data_internal->number_of_displays =
			mode_data->number_of_displays;
		bw_data_internal->graphics_rotation_angle =
			mode_data->displays_data[0].graphics_rotation_angle;
		bw_data_internal->underlay_rotation_angle =
			mode_data->displays_data[0].underlay_rotation_angle;
		bw_data_internal->underlay_surface_type =
			mode_data->displays_data[0].underlay_surface_type;
		bw_data_internal->panning_and_bezel_adjustment =
			mode_data->displays_data[0].panning_and_bezel_adjustment;
		bw_data_internal->graphics_tiling_mode =
			mode_data->displays_data[0].graphics_tiling_mode;
		bw_data_internal->graphics_interlace_mode =
			mode_data->displays_data[0].graphics_interlace_mode;
		bw_data_internal->graphics_bytes_per_pixel =
			mode_data->displays_data[0].graphics_bytes_per_pixel;
		bw_data_internal->graphics_htaps =
			mode_data->displays_data[0].graphics_h_taps;
		bw_data_internal->graphics_vtaps =
			mode_data->displays_data[0].graphics_v_taps;
		bw_data_internal->graphics_lb_bpc =
			mode_data->displays_data[0].graphics_lb_bpc;
		bw_data_internal->underlay_lb_bpc =
			mode_data->displays_data[0].underlay_lb_bpc;
		bw_data_internal->underlay_tiling_mode =
			mode_data->displays_data[0].underlay_tiling_mode;
		bw_data_internal->underlay_htaps =
			mode_data->displays_data[0].underlay_h_taps;
		bw_data_internal->underlay_vtaps =
			mode_data->displays_data[0].underlay_v_taps;
		bw_data_internal->underlay_src_width =
			mode_data->displays_data[0].underlay_src_width;
		bw_data_internal->underlay_src_height =
			mode_data->displays_data[0].underlay_src_height;
		bw_data_internal->underlay_pitch_in_pixels =
			mode_data->displays_data[0].underlay_pitch_in_pixels;
		bw_data_internal->underlay_stereo_mode =
			mode_data->displays_data[0].underlay_stereo_mode;
		bw_data_internal->display_synchronization_enabled =
			mode_data->display_synchronization_enabled;
	}

	if (bw_data_internal->number_of_displays != 0) {
		uint8_t yclk_lvl, sclk_lvl;
		struct bw_fixed high_sclk = vbios->high_sclk;
		struct bw_fixed mid_sclk = vbios->mid_sclk;
		struct bw_fixed low_sclk = vbios->low_sclk;
		struct bw_fixed high_yclk = vbios->high_yclk;
		struct bw_fixed mid_yclk = vbios->mid_yclk;
		struct bw_fixed low_yclk = vbios->low_yclk;

		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		yclk_lvl = bw_results_internal->y_clk_level;
		sclk_lvl = bw_results_internal->sclk_level;

		calcs_output->all_displays_in_sync =
			mode_data->display_synchronization_enabled;
		calcs_output->nbp_state_change_enable =
			bw_results_internal->nbp_state_change_enable;
		calcs_output->cpuc_state_change_enable =
				bw_results_internal->cpuc_state_change_enable;
		calcs_output->cpup_state_change_enable =
				bw_results_internal->cpup_state_change_enable;
		calcs_output->stutter_mode_enable =
				bw_results_internal->stutter_mode_enable;
		calcs_output->dispclk_khz =
			bw_fixed_to_int(bw_mul(bw_results_internal->dispclk,
					bw_int_to_fixed(1000)));
		calcs_output->required_blackout_duration_us =
			bw_fixed_to_int(bw_add(bw_results_internal->
				blackout_duration_margin[yclk_lvl][sclk_lvl],
				vbios->blackout_duration));
		calcs_output->required_sclk =
			bw_fixed_to_int(bw_mul(bw_results_internal->required_sclk,
					bw_int_to_fixed(1000)));
		calcs_output->required_sclk_deep_sleep =
			bw_fixed_to_int(bw_mul(bw_results_internal->sclk_deep_sleep,
					bw_int_to_fixed(1000)));
		if (yclk_lvl == 0)
			calcs_output->required_yclk = bw_fixed_to_int(
				bw_mul(low_yclk, bw_int_to_fixed(1000)));
		else if (yclk_lvl == 1)
			calcs_output->required_yclk = bw_fixed_to_int(
				bw_mul(mid_yclk, bw_int_to_fixed(1000)));
		else
			calcs_output->required_yclk = bw_fixed_to_int(
				bw_mul(high_yclk, bw_int_to_fixed(1000)));

		/* units: nanosecond, 16bit storage. */
		calcs_output->nbp_state_change_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[3].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[4].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_exit_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[3].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[4].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[3].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[4].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		/*TODO check correctness*/
		((struct bw_calcs_vbios *)vbios)->low_sclk = mid_sclk;
		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		calcs_output->nbp_state_change_wm_ns[0].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[4],bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[3].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[4].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[5].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_exit_wm_ns[0].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[3].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[4].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[5].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[3].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[4].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[5].b_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		/*TODO check correctness*/
		((struct bw_calcs_vbios *)vbios)->low_sclk = low_sclk;
		((struct bw_calcs_vbios *)vbios)->low_yclk = mid_yclk;
		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		calcs_output->nbp_state_change_wm_ns[0].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[3].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[4].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[5].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_exit_wm_ns[0].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[3].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[4].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[5].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[3].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[4].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[5].c_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		((struct bw_calcs_vbios *)vbios)->low_yclk = high_yclk;
		((struct bw_calcs_vbios *)vbios)->mid_yclk = high_yclk;
		((struct bw_calcs_vbios *)vbios)->low_sclk = high_sclk;
		((struct bw_calcs_vbios *)vbios)->mid_sclk = high_sclk;

		calculate_bandwidth(dceip, vbios, bw_data_internal,
							bw_results_internal);

		calcs_output->nbp_state_change_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[3].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[4].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_exit_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[3].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[4].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[3].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[7], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[4].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[8], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(bw_results_internal->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		((struct bw_calcs_vbios *)vbios)->low_yclk = low_yclk;
		((struct bw_calcs_vbios *)vbios)->mid_yclk = mid_yclk;
		((struct bw_calcs_vbios *)vbios)->low_sclk = low_sclk;
		((struct bw_calcs_vbios *)vbios)->mid_sclk = mid_sclk;
	} else {
		calcs_output->nbp_state_change_enable = true;
		calcs_output->cpuc_state_change_enable = true;
		calcs_output->cpup_state_change_enable = true;
		calcs_output->stutter_mode_enable = true;
		calcs_output->dispclk_khz = 0;
		calcs_output->required_sclk = 0;
	}

	dm_free(bw_data_internal);
	dm_free(bw_results_internal);

	return is_display_configuration_supported(vbios, calcs_output);
}
