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

#ifndef __DAL_CLOCK_SOURCE_H__
#define __DAL_CLOCK_SOURCE_H__

#include "include/adapter_service_types.h"
#include "include/bios_parser_types.h"
#include "include/clock_source_interface.h"
#include "include/clock_source_types.h"

struct spread_spectrum_data {
	uint32_t percentage;		/*> In unit of 0.01% or 0.001%*/
	uint32_t percentage_divider;	/*> 100 or 1000	*/
	uint32_t freq_range_khz;
	uint32_t modulation_freq_hz;

	struct spread_spectrum_flags flags;
};

struct clock_source_impl {
	bool (*switch_dp_clock_source)(
			struct clock_source *clk_src,
			enum controller_id,
			enum clock_source_id);
	bool (*adjust_pll_pixel_rate)(
			struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params,
			uint32_t requested_pix_clk_hz);
	bool (*adjust_dto_pixel_rate)(
			struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params,
			uint32_t requested_clk_freq_hz);
	uint32_t (*retrieve_dto_pix_rate_hz)(
			struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params);
	uint32_t (*retrieve_pll_pix_rate_hz)(
			struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params);

	uint32_t (*get_pix_clk_dividers)(struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params,
			struct pll_settings *pll_settings);
	bool (*program_pix_clk)(struct clock_source *clk_src,
			struct pixel_clk_params *pix_clk_params,
			struct pll_settings *pll_settings);
	bool (*power_down_pll)(struct clock_source *clk_src,
			enum controller_id);
	void (*destroy)(struct clock_source **clk_src);
};

void dal_clock_source_get_ss_info_from_atombios(
		struct clock_source *clk_src,
		enum as_signal_type as_signal,
		struct spread_spectrum_data *ss_data[],
		uint32_t *ss_entries_num);
uint32_t dal_clock_source_base_retrieve_dto_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params);
const struct spread_spectrum_data *dal_clock_source_get_ss_data_entry(
		struct clock_source *clk_src,
		enum signal_type signal,
		uint32_t pix_clk_khz);
/* for PLL and EXT clock sources */
struct registers {
	uint32_t dp_dtox_phase;
	uint32_t dp_dtox_modulo;
	uint32_t crtcx_pixel_rate_cntl;
	uint32_t combophyx_pll_wrap_cntl;
	uint32_t combophyx_freq_cntl0;
	uint32_t combophyx_freq_cntl2;
	uint32_t combophyx_freq_cntl3;
};

struct clock_source {
	const struct clock_source_impl *funcs;
	struct graphics_object_id id;
	enum clock_source_id clk_src_id;
	struct adapter_service *adapter_service;
	struct bios_parser *bios_parser;

	struct spread_spectrum_data *ep_ss_params;
	uint32_t ep_ss_params_cnt;
	struct spread_spectrum_data *dp_ss_params;
	uint32_t dp_ss_params_cnt;

	struct spread_spectrum_data *hdmi_ss_params;
	uint32_t hdmi_ss_params_cnt;

	struct spread_spectrum_data *dvi_ss_params;
	uint32_t dvi_ss_params_cnt;

	uint32_t output_signals;
	uint32_t input_signals;

	bool turn_off_ds;
	bool is_gen_lock_capable; /*replacement for virtual method*/
	bool is_clock_source_with_fixed_freq; /*replacement for virtual method*/
	enum clock_sharing_level clk_sharing_lvl;
	struct dc_context *ctx;
};

bool dal_clock_source_construct(
			struct clock_source *clk_src,
			struct clock_source_init_data *clk_src_init_data);
bool dal_clock_source_base_adjust_pll_pixel_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t requested_pix_clk_hz);
bool dal_clock_source_base_adjust_dto_pix_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t requested_pix_clk_hz);
uint32_t dal_clock_source_base_retrieve_pll_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params);

#endif
