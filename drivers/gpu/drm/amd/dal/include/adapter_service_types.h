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

#ifndef __DAL_ADAPTER_SERVICE_TYPES_H__
#define __DAL_ADAPTER_SERVICE_TYPES_H__

/* TODO: include signal_types.h and remove this enum */
enum as_signal_type {
	AS_SIGNAL_TYPE_NONE = 0L, /* no signal */
	AS_SIGNAL_TYPE_DVI,
	AS_SIGNAL_TYPE_HDMI,
	AS_SIGNAL_TYPE_LVDS,
	AS_SIGNAL_TYPE_DISPLAY_PORT,
	AS_SIGNAL_TYPE_GPU_PLL,
	AS_SIGNAL_TYPE_UNKNOWN
};

/*
 * Struct used for algorithm of Bandwidth tuning parameters
 * the sequence of the fields is binded with runtime parameter.
 */
union bandwidth_tuning_params {
	struct bandwidth_tuning_params_struct {
		uint32_t read_delay_stutter_off_usec;
		uint32_t ignore_hblank_time;/*bool*/
		uint32_t extra_reordering_latency_usec;
		uint32_t extra_mc_latency_usec;
		uint32_t data_return_bandwidth_eff;/*in %*/
		uint32_t dmif_request_bandwidth_eff;/*in %*/
		uint32_t sclock_latency_multiplier;/*in unit of 0.01*/
		uint32_t mclock_latency_multiplier;/*in unit of 0.01*/
		uint32_t fix_latency_multiplier;/*in unit of 0.01*/
		 /*in unit represent in watermark*/
		uint32_t use_urgency_watermark_offset;
	} tuning_info;
	uint32_t arr_info[sizeof(struct bandwidth_tuning_params_struct)
		/ sizeof(uint32_t)];
};

union audio_support {
	struct {
		uint32_t DP_AUDIO:1;
		uint32_t HDMI_AUDIO_ON_DONGLE:1;
		uint32_t HDMI_AUDIO_NATIVE:1;
	} bits;
	uint32_t raw;
};

#endif
