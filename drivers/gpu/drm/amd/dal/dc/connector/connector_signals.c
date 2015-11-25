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

#include "dal_services.h"

#include "connector.h"

static const enum signal_type signals_none[] = {
		SIGNAL_TYPE_NONE
};

static const enum signal_type signals_single_link_dvii[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_RGB
};

static const enum signal_type signals_dual_link_dvii[] = {
		SIGNAL_TYPE_DVI_DUAL_LINK,
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_RGB
};

static const enum signal_type signals_single_link_dvid[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK
};

static const enum signal_type signals_dual_link_dvid[] = {
		SIGNAL_TYPE_DVI_DUAL_LINK,
		SIGNAL_TYPE_DVI_SINGLE_LINK,
};

static const enum signal_type signals_vga[] = {
		SIGNAL_TYPE_RGB
};

static const enum signal_type signals_hdmi_type_a[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_HDMI_TYPE_A
};

static const enum signal_type signals_lvds[] = {
		SIGNAL_TYPE_LVDS
};

static const enum signal_type signals_pcie[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_HDMI_TYPE_A,
		SIGNAL_TYPE_DISPLAY_PORT
};

static const enum signal_type signals_hardcode_dvi[] = {
		SIGNAL_TYPE_NONE
};

static const enum signal_type signals_displayport[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_HDMI_TYPE_A,
		SIGNAL_TYPE_DISPLAY_PORT,
		SIGNAL_TYPE_DISPLAY_PORT_MST
};

static const enum signal_type signals_edp[] = {
		SIGNAL_TYPE_EDP
};

static const enum signal_type signals_wireless[] = {
		SIGNAL_TYPE_WIRELESS
};

static const enum signal_type signals_miracast[] = {
		SIGNAL_TYPE_WIRELESS
};

static const enum signal_type default_signals_none[] = {
		SIGNAL_TYPE_NONE
};

static const enum signal_type default_signals_single_link_dvii[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK,
		SIGNAL_TYPE_RGB
};

static const enum signal_type default_signals_dual_link_dvii[] = {
		SIGNAL_TYPE_DVI_DUAL_LINK,
		SIGNAL_TYPE_RGB
};

static const enum signal_type default_signals_single_link_dvid[] = {
		SIGNAL_TYPE_DVI_SINGLE_LINK
};

static const enum signal_type default_signals_dual_link_dvid[] = {
		SIGNAL_TYPE_DVI_DUAL_LINK,
};

static const enum signal_type default_signals_vga[] = {
		SIGNAL_TYPE_RGB
};

static const enum signal_type default_signals_hdmi_type_a[] = {
		SIGNAL_TYPE_HDMI_TYPE_A
};

static const enum signal_type default_signals_lvds[] = {
		SIGNAL_TYPE_LVDS
};

static const enum signal_type default_signals_pcie[] = {
		SIGNAL_TYPE_DISPLAY_PORT
};

static const enum signal_type default_signals_hardcode_dvi[] = {
		SIGNAL_TYPE_NONE
};

static const enum signal_type default_signals_displayport[] = {
		SIGNAL_TYPE_DISPLAY_PORT
};

static const enum signal_type default_signals_edp[] = {
		SIGNAL_TYPE_EDP
};

static const enum signal_type default_signals_wireless[] = {
		SIGNAL_TYPE_WIRELESS
};

static const enum signal_type default_signals_miracast[] = {
		SIGNAL_TYPE_WIRELESS
};

/*
 * Signal arrays
 */

#define SIGNALS_ARRAY_ELEM(a) {a, ARRAY_SIZE(a)}

/* Indexed by enum connector_id */
const struct connector_signals default_signals[] = {
		SIGNALS_ARRAY_ELEM(default_signals_none),
		SIGNALS_ARRAY_ELEM(default_signals_single_link_dvii),
		SIGNALS_ARRAY_ELEM(default_signals_dual_link_dvii),
		SIGNALS_ARRAY_ELEM(default_signals_single_link_dvid),
		SIGNALS_ARRAY_ELEM(default_signals_dual_link_dvid),
		SIGNALS_ARRAY_ELEM(default_signals_vga),
		SIGNALS_ARRAY_ELEM(default_signals_hdmi_type_a),
		SIGNALS_ARRAY_ELEM(default_signals_none),
		SIGNALS_ARRAY_ELEM(default_signals_lvds),
		SIGNALS_ARRAY_ELEM(default_signals_pcie),
		SIGNALS_ARRAY_ELEM(default_signals_hardcode_dvi),
		SIGNALS_ARRAY_ELEM(default_signals_displayport),
		SIGNALS_ARRAY_ELEM(default_signals_edp),
		/* MXM dummy connector */
		SIGNALS_ARRAY_ELEM(default_signals_none),
		SIGNALS_ARRAY_ELEM(default_signals_wireless),
		SIGNALS_ARRAY_ELEM(default_signals_miracast)
};

const uint32_t number_of_default_signals = ARRAY_SIZE(default_signals);

/* Indexed by enum connector_id */
const struct connector_signals supported_signals[] = {
		SIGNALS_ARRAY_ELEM(signals_none),
		SIGNALS_ARRAY_ELEM(signals_single_link_dvii),
		SIGNALS_ARRAY_ELEM(signals_dual_link_dvii),
		SIGNALS_ARRAY_ELEM(signals_single_link_dvid),
		SIGNALS_ARRAY_ELEM(signals_dual_link_dvid),
		SIGNALS_ARRAY_ELEM(signals_vga),
		SIGNALS_ARRAY_ELEM(signals_hdmi_type_a),
		SIGNALS_ARRAY_ELEM(signals_none),
		SIGNALS_ARRAY_ELEM(signals_lvds),
		SIGNALS_ARRAY_ELEM(signals_pcie),
		SIGNALS_ARRAY_ELEM(signals_hardcode_dvi),
		SIGNALS_ARRAY_ELEM(signals_displayport),
		SIGNALS_ARRAY_ELEM(signals_edp),
		/* MXM dummy connector */
		SIGNALS_ARRAY_ELEM(signals_none),
		SIGNALS_ARRAY_ELEM(signals_wireless),
		SIGNALS_ARRAY_ELEM(signals_miracast)
};

const uint32_t number_of_signals = ARRAY_SIZE(supported_signals);
