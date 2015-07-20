/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_BANDWIDTH_MANAGER_INTERFACE__
#define __DAL_BANDWIDTH_MANAGER_INTERFACE__



#include "include/grph_object_ctrl_defs.h"
#include "bandwidth_manager_types.h"

struct bandwidth_manager;
/*struct bandwidth_params;
struct wm_inp_parameters;
struct bandwidth_mngr_watermark;
struct bandwidth_mgr_static_clk_info;
struct bandwidth_mgr_clk_info;
*/
void dal_bandwidth_manager_destroy(struct bandwidth_manager **bm);

void dal_bandwidth_manager_release_hw(struct bandwidth_manager *bm);

bool dal_bandwidth_manager_validate_video_memory_bandwidth(
		struct bandwidth_manager *bm,
		uint32_t path_num,
		struct bandwidth_params *bw_params,
		uint32_t disp_clk_khz);

/* Returns minimum Memory clock in KHz */
uint32_t dal_bandwidth_manager_get_min_mclk(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);

/* Returns minimum System clock in KHz */
uint32_t dal_bandwidth_manager_get_min_sclk(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);

void dal_bandwidth_manager_program_watermark(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
void dal_bandwidth_manager_program_display_mark(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
void dal_bandwidth_manager_program_safe_display_mark(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
uint32_t dal_bandwidth_manager_get_min_deep_sleep_sclk(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params,
		uint32_t disp_clk_khz);
/* Returns an information about watermarks */
uint32_t dal_bandwidth_manager_get_watermark_info(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		union bandwidth_mngr_watermark_info *watermarks);
/* DMIF buffer handling */
void dal_bandwidth_manager_allocate_dmif_buffer(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);
void dal_bandwidth_manager_deallocate_dmif_buffer(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t paths_num);

/* setter for clock info */
void dal_bandwidth_manager_set_static_clock_info(
		struct bandwidth_manager *bm,
		struct bandwidth_mgr_clk_info *static_clk_info);
void dal_bandwidth_manager_set_dynamic_clock_info(
		struct bandwidth_manager *bm,
		struct bandwidth_mgr_clk_info *clk_info);
/* Static clocks are initialised during creation on GPU/BM.
 * That means it is always ok to get the value of the clocks.*/
void dal_bandwidth_manager_get_static_clock_info(
		struct bandwidth_manager *bm,
		struct bandwidth_mgr_clk_info *static_clk_info);


bool dal_bandwidth_manager_get_min_mem_chnls(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params,
		bool use_max_clk,
		uint32_t disp_clk_khz,
		uint32_t mem_channels_total_number,
		uint32_t *mem_chnls_min_num);
void dal_bandwidth_manager_program_pix_dur(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t pix_clk_khz);

void dal_bandwidth_manager_setup_pipe_max_request(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		struct color_quality *color_info);
uint32_t dal_bandwidth_manager_get_min_vbi_end_us(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t vblank_time,
		uint32_t pix_rate_khz);
uint32_t dal_bandwidth_manager_get_available_mclk_switch_time(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t vblank_time,
		uint32_t pix_rate_khz);

#endif
