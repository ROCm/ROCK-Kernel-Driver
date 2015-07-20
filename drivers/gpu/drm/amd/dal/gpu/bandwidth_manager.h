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
#ifndef __DAL_BANDWIDTH_MANAGER_H__
#define __DAL_BANDWIDTH_MANAGER_H__

#include "include/bandwidth_manager_interface.h"
#include "include/logger_interface.h"

struct pm_clock_info;

struct bandwidth_manager_funcs {
	void (*destroy)(struct bandwidth_manager **bm);
	void (*release_hw)(struct bandwidth_manager *bm);
	bool (*validate_video_memory_bandwidth)(
		struct bandwidth_manager *bm,
		uint32_t path_num,
		struct bandwidth_params *bw_params,
		uint32_t disp_clk_khz);
	/* Returns minimum memory clock in KHz */
	uint32_t (*get_min_mem_clk)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);
	/* Returns minimum System clock in KHz */
	uint32_t (*get_min_sclk)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);
	void (*program_watermark)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
	void (*program_display_mark)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
	void (*program_safe_display_mark)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct watermark_input_params *wm_params,
		uint32_t disp_clk_khz);
	uint32_t (*get_min_deep_sleep_sclk)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params,
		uint32_t disp_clk_khz);
	/* Returns an information about watermarks */
	uint32_t (*get_watermark_info)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		union bandwidth_mngr_watermark_info *watermarks);
	/* DMIF buffer handling */
	void (*allocate_dmif_buffer)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t paths_num,
		struct bandwidth_params *bw_params);
	void (*deallocate_dmif_buffer)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t paths_num);
	bool (*get_min_mem_chnls)(
		struct bandwidth_manager *bm,
		uint32_t paths_num,
		struct bandwidth_params *bw_params,
		bool use_max_clk,
		uint32_t disp_clk_khz,
		uint32_t mem_channels_total_number,
		uint32_t *mem_chnls_min_num);
	void (*program_pix_dur)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t pix_clk_khz);
	void (*setup_pipe_max_request)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		struct color_quality *color_info);
	uint32_t (*get_min_vbi_end_us)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t vblank_time,
		uint32_t pix_rate_khz);
	uint32_t (*get_available_mclk_switch_time)(
		struct bandwidth_manager *bm,
		enum controller_id ctrl_id,
		uint32_t vblank_time,
		uint32_t pix_rate_khz);
};

struct bandwidth_manager {
	const struct bandwidth_manager_funcs *funcs;
	uint32_t mc_latency;
	uint32_t memory_bus_width;
	uint32_t memory_type_multiplier;
	uint32_t dram_bandwidth_efficiency;

	/*bandwidth/stutter calculation tuning parameters */
	bool ignore_hblank_time; /*recommended default for dce 4.0 is off*/
	bool no_extra_recording_latency; /*default for dce 4.0 is on */
	bool maximize_urgency_watermarks;/* */
	bool maximize_stutter_marks;
	uint32_t read_dly_stutter_off; /* in usec*/
	uint32_t data_return_bandwidth_eff; /* in % */
	uint32_t dmif_request_bandwidth_eff; /* in % */
	uint32_t sclock_latency_multiplier; /* in unit of 0.01 */
	uint32_t mclock_latency_multiplier; /* in unit of 0.01 */
	uint32_t fix_latency_multiplier; /* in unit of 0.01 */
	uint32_t use_urgency_watermark_offset; /*in unit represent in wtrmark*/

	/***** clock info *****/
	/* DCE-specific clocks - NOT dependent on Mode(s).
	 * Used for Mode validation. */
	struct bandwidth_mgr_clk_info static_clk_info;
	/* Can be changed at run-time - depending on needs of Display Block.
	 * Basically it depends on Mode(s). */
	struct bandwidth_mgr_clk_info dynamic_clk_info;
	/* end of clock info */

	struct dal_context *dal_ctx;

	uint32_t current_display_clock;
	bool dfs_bypass_enabled;
};


bool dal_bandwidth_manager_construct_base(
	struct bandwidth_manager *base,
	struct dal_context *dal_ctx,
	struct adapter_service *as);
void dal_bandwidth_manager_destruct_base(struct bandwidth_manager *bm);
void dal_bandwidth_manager_release_hw(struct bandwidth_manager *bm);

/*protected*/
/*
 * GetAvailableDRAMBandwidth
 */

void dal_bandwidth_manager_update_dc_state(
	struct bandwidth_manager *bm,
	uint32_t disp_clock,
	bool dfs_bypass_enable);

uint32_t dal_bandwidth_manager_controller_id_to_index(enum controller_id id);
uint32_t dal_bandwidth_manager_get_dmif_switch_time_us(
	struct bandwidth_params *params);
enum dal_irq_source dal_bandwidth_manager_irq_source_crtc_map(
	enum controller_id id);

uint32_t dal_bandwidth_manager_min(uint32_t lhs, uint32_t rhs);

#define CLK_SWITCH_TIME_US_DEFAULT 300
/* 300 microseconds for MCLk switch on all DDRs except DDR5*/
/* (Stella Wang, May 2012) */
#define CLK_SWITCH_TIME_US_DDR5 460
/* 460 microseconds for MCLk switch on DDR5*/
/* (Stella Wang, May 2012) */
#define MULTIPLIER 1000
#define MAXIMUM_MEMORY_BANDWIDTH 0xFFFFFFFF

#define NS_PER_MS 1000000 /* nanoseconds per millisecond*/
#define US_PER_MS 1000 /* microseconds per millisecond */
#define KHZ_IN_MHZ 1000

/******************************************************************************
 * Validation and Debugging interface.
 *****************************************************************************/
union bm_debug_flags {
	struct {
		/* Print modes which where filtered OUT. */
		uint32_t MODE_VALIDATION_FAILED:1;
		/* Print modes which passed validation.
		 * Caution - this will produce *a lot* of output. */
		uint32_t MODE_VALIDATION_SUCCEEDED:1;
	};
	uint32_t all;
};

extern union bm_debug_flags bm_dbg_flags;

void dal_bm_log_video_memory_bandwidth(
		struct dal_context *dal_context,
		uint32_t paths_num,
		const struct bandwidth_params *params,
		uint32_t dclk_khz,
		uint32_t required_display_bandwidth,
		uint32_t mc_urgent_bandwidth,
		uint32_t sclk_khz,
		uint32_t mclk_khz,
		bool validation_result);

#define BM_DBG_PPLIB_INPUT(...) \
		dal_logger_write(dal_context->logger, \
			LOG_MAJOR_EC, LOG_MINOR_EC_PPLIB_QUERY, __VA_ARGS__)

#define BM_DBG_MEM_BANDWIDTH_VALIDATION(...) \
		dal_logger_write(dal_context->logger, \
			LOG_MAJOR_BWM, LOG_MINOR_BWM_MODE_VALIDATION, \
				__VA_ARGS__)

#endif /* __DAL_BANDWIDTH_MANAGER_H__ */
