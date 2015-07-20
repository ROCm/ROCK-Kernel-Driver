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

#ifndef __DAL_SIGNAL_TYPES_H__
#define __DAL_SIGNAL_TYPES_H__

enum signal_type {
	SIGNAL_TYPE_NONE		= 0L,		/* no signal */
	SIGNAL_TYPE_DVI_SINGLE_LINK	= (1 << 0),
	SIGNAL_TYPE_DVI_SINGLE_LINK1	= (1 << 1),
	SIGNAL_TYPE_DVI_DUAL_LINK	= (1 << 2),
	SIGNAL_TYPE_HDMI_TYPE_A		= (1 << 3),
	SIGNAL_TYPE_LVDS		= (1 << 4),
	SIGNAL_TYPE_RGB			= (1 << 5),
	SIGNAL_TYPE_YPBPR		= (1 << 6),
	SIGNAL_TYPE_SCART		= (1 << 7),
	SIGNAL_TYPE_COMPOSITE		= (1 << 8),
	SIGNAL_TYPE_SVIDEO		= (1 << 9),
	SIGNAL_TYPE_DISPLAY_PORT	= (1 << 10),
	SIGNAL_TYPE_DISPLAY_PORT_MST	= (1 << 11),
	SIGNAL_TYPE_EDP			= (1 << 12),
	SIGNAL_TYPE_DVO			= (1 << 13),	/* lower 12 bits */
	SIGNAL_TYPE_DVO24		= (1 << 14),	/* 24 bits */
	SIGNAL_TYPE_MVPU_A		= (1 << 15),	/* lower 12 bits */
	SIGNAL_TYPE_MVPU_B		= (1 << 16),	/* upper 12 bits */
	SIGNAL_TYPE_MVPU_AB		= (1 << 17),	/* 24 bits */
	SIGNAL_TYPE_WIRELESS		= (1 << 18),	/* Wireless Display */

	SIGNAL_TYPE_COUNT		= 19,
	SIGNAL_TYPE_ALL			= (1 << SIGNAL_TYPE_COUNT) - 1
};

/* help functions for signal types manipulation */
bool dal_is_hdmi_signal(enum signal_type signal);
bool dal_is_dp_sst_signal(enum signal_type signal);
bool dal_is_dp_signal(enum signal_type signal);
bool dal_is_dp_external_signal(enum signal_type signal);
bool dal_is_analog_signal(enum signal_type signal);
bool dal_is_embedded_signal(enum signal_type signal);
bool dal_is_dvi_signal(enum signal_type signal);
bool dal_is_dvo_signal(enum signal_type signal);
bool dal_is_mvpu_signal(enum signal_type signal);
bool dal_is_cf_signal(enum signal_type signal);
bool dal_is_dvi_single_link_signal(enum signal_type signal);
bool dal_is_dual_link_signal(enum signal_type signal);
bool dal_is_audio_capable_signal(enum signal_type signal);
bool dal_is_digital_encoder_compatible_signal(enum signal_type signal);

#endif
