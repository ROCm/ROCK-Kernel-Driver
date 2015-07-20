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

#ifndef __DAL_TM_DETECTION_MGR_H__
#define __DAL_TM_DETECTION_MGR_H__

/* External includes */

#include "include/irq_types.h"
#include "include/topology_mgr_interface.h"
#include "include/display_path_interface.h"

/* Internal includes */
#include "tm_internal_types.h"
#include "tm_resource_mgr.h"

/**
 *****************************************************************************
 * TMDetectionMgr
 *
 * @brief
 *	TMDetectionMgr responsible for performing detection of displays and
 *	handling display interrupts.
 *	Detection includes detecting sink presence, detecting sink signal,
 *	reading EDID and reading sink capabilities.
 *	Detection does NOT include updating display path state.
 *
 *****************************************************************************
 */

/* forward declarations */
struct dal_context;
struct tm_detection_mgr;

struct tm_detection_mgr_init_data {
	struct dal_context *dal_context;
	struct adapter_service *as;
	struct hw_sequencer *hwss;
	struct tm_resource_mgr *resource_mgr;
	struct topology_mgr *tm;
};

struct tm_detection_status {
	/* currently detected signal */
	enum signal_type detected_signal;

	struct display_sink_capability sink_capabilities;

	/* set when no connectivity change, but capabilities changed
	 *  (EDID, SinkCap, etc.) */
	bool capability_changed;
	/* status indicating if the monitor ID changed -
	 * only valid when monitor connected */
	bool monitor_changed;
	/* currently detected connectivity state */
	bool connected;
	/* DpMst signal was detected (including
	 * transitions DpMst-->Dp and Dp-->DpMst) */
	bool dp_mst_detection;
	/* HPD line is low though EDID might be detected */
	bool hpd_pin_failure;
	/* audio capabilities changed */
	bool audio_cap_changed;
};


/**tm_detection will be exposed to Topology only.
 * Function exposed to Topology will be declared in tmdetectionmgr.h
 * Function used by tm_detection only will be static function and
 * not listed in tm_detectionmgr.h
 *
 */

/*TODO: how to handle this
 * void tm_detection_handle_interrupt(
 * struct tm_detection_mgr *tm_dm,
 * InterruptInfo* pInterruptInfo);
 */

struct tm_detection_mgr *dal_tm_detection_mgr_create(
		struct tm_detection_mgr_init_data *init_data);

void dal_tm_detection_mgr_destroy(struct tm_detection_mgr **tm_dm);

void dal_tm_detection_mgr_release_hw(struct tm_detection_mgr *tm_dm);

/* Detection methods */
bool dal_tm_detection_mgr_detect_display(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status);

bool dal_tm_detection_mgr_retreive_sink_info(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		enum tm_detection_method method,
		struct tm_detection_status *detection_status);

void dal_tm_detection_mgr_reschedule_detection(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path,
		bool reschedule);

/* IRQ registration and state update methods */
bool dal_tm_detection_mgr_register_display(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path);

bool dal_tm_detection_mgr_register_hpd_irq(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path);

void dal_tm_detection_mgr_update_active_state(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path);

void dal_tm_detection_mgr_init_hw(struct tm_detection_mgr *tm_dm);

void dal_tm_detection_mgr_set_blocking_detection(
		struct tm_detection_mgr *tm_dm,
		bool blocking);

bool dal_tm_detection_mgr_is_blocking_detection(
		struct tm_detection_mgr *tm_dm);

void dal_tm_detection_mgr_set_blocking_interrupts(
		struct tm_detection_mgr *tm_dm,
		bool blocking);

bool dal_tm_detection_mgr_is_blocking_interrupts(
		struct tm_detection_mgr *tm_dm);

void dal_tm_detection_mgr_program_hpd_filter(
		struct tm_detection_mgr *tm_dm,
		struct display_path *display_path);

#endif /* __TM_DETECTION_MGR_H__ */
