
/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

/* Include */
#include "dal_services.h"

/*#include "include/set_mode_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/dcs_interface.h"
#include "include/link_service_interface.h"*/
#include "include/display_path_interface.h"
#include "include/display_service_types.h"
#include "include/hw_sequencer_types.h"
#include "include/timing_service_types.h"
#include "include/dcs_types.h"

#include "ds_calculation.h"

/*
 * Public function definition
 */

/* Adjust timing to the timing limits. */
void dal_ds_calculation_tuneup_timing(
		struct hw_crtc_timing *timing,
		const struct timing_limits *timing_limits)
{
	uint32_t cur_pixel_clock;
	uint32_t new_pixel_clock;
	uint32_t new_h_total;

	if ((timing_limits == NULL) ||
			(timing == NULL) ||
			(timing->h_total == 0) ||
			(timing->v_total == 0)) {
		return;
	}

	/* Perform all calculations in DS internal units [kHz] */
	cur_pixel_clock = timing->pixel_clock;
	new_h_total = timing->h_total;

	/* Set pixel clock in the middle of the range.
	 * Upper layer should provide a valid range. */
	new_pixel_clock =
		(timing_limits->max_pixel_clock_in_khz +
				timing_limits->min_pixel_clock_in_khz) /
				2;

	if ((new_pixel_clock > 0) &&
			(new_pixel_clock != cur_pixel_clock)) {
		uint32_t v_h_total = timing->h_total * timing->v_total;
		uint32_t refresh_rate =
			((timing->pixel_clock * 1000) + (v_h_total / 2)) /
			v_h_total;

		/* Pixel clock adjusted to fit in the range; adjust HTOTAL to
		 * preserve refresh rate.
		 *
		 *  SS = Sync Start
		 *  SE = Sync End
		 *  SW = Sync Width
		 *  HA = Horizontal Active
		 *  HT = Horizontal Total
		 *  HBS = Horizontal Blank Start
		 *  HBE = Horizontal Blank End
		 *  FP = FrontPorch (affected by changing Horizontal Total)
		 *
		 * SS.      .SE
		 *     ______        ________________        ______
		 * ___|      |______|xxxxxxxxxxxxxxxx|______|      |___
		 *    |      |      |                |      |
		 *    |<-SW->|      |<------HA------>|      |
		 *    |             |                |      |
		 *    |<-----------------HT---------------->|
		 *    |             |                |      |
		 *    |--------------HBS------------>|      |
		 *    |-----HBE---->|                |      |
		 *                                   |      |
		 *                                   |<-FP->|
		 *          32     164             2084    2184
		 *
		 * Assuming there is no pixel repetition, register programming
		 * will look like this:
		 *
		 * CRTC_H_TOTAL         = HTotal
		 * CRTC_H_SYNC_X_START  = 0
		 * CRTC_H_SYNC_X_END    = HSyncWidth
		 * CRTC_H_BANK_START    = HTotal - (HSyncStart - HActive)
		 * CRTC_H_BANK_END      = HTotal - HSyncStart
		 *
		 * pixelClock = HTotal * VTotal * refRate =>
		 *	HTotal = pixelClock / VTotal / refRate
		 *
		 * newFrontPorch = newHTtotal - blankStart =
		 *	(newPixelClock/VTotal/refRate) - blankStart */

		int32_t cur_front_porch =
				timing->h_sync_start - timing->h_addressable;
		int32_t blank_start =
				timing->h_total - cur_front_porch;
		int32_t new_front_porch =
				(new_pixel_clock *
					PIXEL_CLOCK_MULTIPLIER /
					timing->v_total /
					refresh_rate) -
					blank_start;

		/* New HTOTAL differs from current one by front porch
		 * difference */
		new_h_total += new_front_porch - cur_front_porch;
		timing->pixel_clock = new_pixel_clock;
		timing->h_total = new_h_total;
	}
}

/* Setup ranged timing parameters for features such as DRR or PSR. */
void dal_ds_calculation_setup_ranged_timing(
		struct hw_crtc_timing *timing,
		struct display_path *display_path,
		struct ranged_timing_preference_flags flags)
{
	uint64_t fps_in_micro_hz = 0;
	bool drr_supported = false;
	bool psr_supported = false;
	bool vce_supported = false;
	struct drr_config drr_config;
	uint32_t v_h_total;
	struct hw_ranged_timing *rt;

	if ((display_path == NULL) || (timing == NULL))
		return;

	/* Temporary pointer to ranged timing we want to build */
	rt = &timing->ranged_timing;

	/* Initialize flags to program both static screen event triggers and
	 * dynamic refresh rate.
	 * Originally, the purpose of these flags is to prevent DRR from being
	 * enabled if PSR was supported. But in current implementation, PSR may
	 * be used in static screen, while DRR can still be enabled during non-
	 * static state. Because of this, we always need prepare ranged timing
	 * for both features. When we do actual programming, we may set flags
	 * to enable the feature we actually want at the current state. */
	rt->control.program_static_screen_mask = false;
	rt->control.program_dynamic_refresh_rate = false;
	rt->control.force_disable_drr = false;

	/* 1. Check for features that require Static Screen Detection */
	v_h_total = timing->h_total * timing->v_total;
	if (v_h_total != 0) {
		fps_in_micro_hz = timing->pixel_clock * 1000;
		fps_in_micro_hz *= 1000000;
		fps_in_micro_hz = div_u64(fps_in_micro_hz, v_h_total);
	}

	dal_display_path_get_drr_config(display_path, &drr_config);

	if (dal_display_path_is_psr_supported(display_path)) {
		/* Check if PSR is supported. PSR requires static screen
		 * detection enabled in HW. */
		psr_supported = true;

		/* If PSR is enabled, DRR should be force disabled
		 * unless we override later by forced flags. */
		rt->control.force_disable_drr = true;
	} else if (dal_display_path_get_config_signal(
			display_path, SINK_LINK_INDEX) ==
					SIGNAL_TYPE_WIRELESS) {
		/* Check if this display is a Wireless Display by checking
		 * the sink signal type. If this is a Wireless Display, static
		 * screen detection may be enabled for power saving. */
		vce_supported = true;
	} else if (dal_display_path_is_drr_supported(display_path)) {
		/* The check for DRR supported returns true means it satisfied:
		 * 1. EDID reported DRR capability and Stream supports DRR or
		 * 2. Forced capability through runtime parameter or
		 * 3. Forced capability through VBIOS */
		drr_supported = true;
	}

	/* 2. Apply some override flags */
	if (flags.bits.force_disable_drr == 1) {
		/* Setup ranged timing parameters to force disable DRR.
		 * This is typically set if PSR is being enabled and we want
		 * to force disable DRR. */
		rt->control.force_disable_drr = true;
	} else if (flags.bits.prefer_enable_drr == 1) {
		/* Preference flag to enable DRR with higher priority. For
		 * static screen power saving use case, DRR is usually not
		 * enabled if PSR is also supported in the system. But in this
		 * case it may be OS telling us to disable power feature for
		 * screen active. Then we want to use DRR for matching render
		 * and refresh rates. */
		rt->control.force_disable_drr = false;
	}

	/* 3. If any feature is supported on the display, enable static screen
	 * detection and prepare ranged timing. */
	if (drr_supported || psr_supported || vce_supported) {
		struct static_screen_events ss_events;

		rt->control.program_static_screen_mask = true;

		/* Initialize to the VTOTAL value. This means refresh rate will
		 * be constant. */
		rt->vertical_total_min = timing->v_total;
		rt->vertical_total_max = timing->v_total;

		/* Prevent divide by zero. We always want to build DRR settings
		 * if possible, even if not for static screen purpose. It could
		 * still be used for 48 Hz feature. */
		if (drr_config.min_fps_in_microhz != 0) {
			timing->ranged_timing.control.program_dynamic_refresh_rate
					= true;

			/* If DRR is supported, update DRR parameters with min
			 * and max VTOTAL values to define the refresh rate
			 * range. */
			rt->vertical_total_min = timing->v_total;
			rt->vertical_total_max =
				div_u64((timing->v_total * fps_in_micro_hz),
					drr_config.min_fps_in_microhz);

			rt->control.force_lock_on_event =
					drr_config.force_lock_on_event;
			rt->control.lock_to_master_vsync =
					drr_config.lock_to_master_vsync;
		}

		dal_display_path_get_static_screen_triggers(
				display_path, &ss_events);

		rt->control.event_mask.u_all = 0;
		rt->control.event_mask.bits.FRAME_START =
				ss_events.bits.FRAME_START;
		rt->control.event_mask.bits.CURSOR_MOVE =
				ss_events.bits.CURSOR_MOVE;
		rt->control.event_mask.bits.MEM_WRITE =
				ss_events.bits.MEM_WRITE;
		rt->control.event_mask.bits.MEM_REGION0_WRITE =
				ss_events.bits.MEM_REGION0_WRITE;
		rt->control.event_mask.bits.MEM_REGION1_WRITE =
				ss_events.bits.MEM_REGION1_WRITE;
		rt->control.event_mask.bits.MEM_REGION2_WRITE =
				ss_events.bits.MEM_REGION2_WRITE;
		rt->control.event_mask.bits.MEM_REGION3_WRITE =
				ss_events.bits.MEM_REGION3_WRITE;
		rt->control.event_mask.bits.GFX_UPDATE =
				ss_events.bits.GFX_UPDATE;
		rt->control.event_mask.bits.INVALIDATE_FBC_SURFACE =
				ss_events.bits.INVALIDATE_FBC_SURFACE;
		rt->control.event_mask.bits.REG_PENDING_UPDATE =
				ss_events.bits.REG_PENDING_UPDATE;
		rt->control.event_mask.bits.CRTC_TRIG_A =
				ss_events.bits.CRTC_TRIG_A;
		rt->control.event_mask.bits.CRTC_TRIG_B =
				ss_events.bits.CRTC_TRIG_B;
		rt->control.event_mask.bits.READBACK_NOMINAL_VERTICAL =
				ss_events.bits.READBACK_NOMINAL_VERTICAL;
		rt->control.event_mask.bits.READBACK_DYNAMIC_VERTICAL =
				ss_events.bits.READBACK_DYNAMIC_VERTICAL;
	}
}
