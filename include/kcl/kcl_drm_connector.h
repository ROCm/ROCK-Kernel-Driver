/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#ifndef AMDKCL_DRM_CONNECTOR_H
#define AMDKCL_DRM_CONNECTOR_H

#include <drm/drm_crtc.h>
#include <drm/drm_connector.h>
#include <kcl/kcl_drm_crtc.h>
#include <kcl/kcl_drm_print.h>

/**
 * drm_connector_for_each_possible_encoder - iterate connector's possible encoders
 * @connector: &struct drm_connector pointer
 * @encoder: &struct drm_encoder pointer used as cursor
 * @__i: int iteration cursor, for macro-internal use
 */
#ifndef drm_connector_for_each_possible_encoder
#define drm_connector_for_each_possible_encoder(connector, encoder, __i) \
	for ((__i) = 0; (__i) < ARRAY_SIZE((connector)->encoder_ids) && \
		     (connector)->encoder_ids[(__i)] != 0; (__i)++) \
		for_each_if((encoder) = \
			    drm_encoder_find((connector)->dev, NULL, \
					     (connector)->encoder_ids[(__i)])) \

#endif

#ifndef HAVE_DRM_CONNECTOR_INIT_WITH_DDC
int _kcl_drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc);
static inline
int drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc)
{
	return _kcl_drm_connector_init_with_ddc(dev, connector, funcs, connector_type, ddc);
}
#endif

#ifndef DP_MAX_DOWNSTREAM_PORTS
#define DP_MAX_DOWNSTREAM_PORTS    0x10
#endif

#ifndef HAVE_DRM_MODE_CONFIG_DP_SUBCONNECTOR_PROPERTY
void drm_connector_attach_dp_subconnector_property(struct drm_connector *connector);
void drm_dp_set_subconnector_property(struct drm_connector *connector, enum drm_connector_status status,
				  const u8 *dpcd, const u8 prot_cap[4]);

#define DRM_MODE_SUBCONNECTOR_VGA 1
#define DRM_MODE_SUBCONNECTOR_DisplayPort 10
#define DRM_MODE_SUBCONNECTOR_HDMIA 11
#define DRM_MODE_SUBCONNECTOR_Native 15
#define DRM_MODE_SUBCONNECTOR_Wireless 18
#endif /* HAVE_DRM_MODE_CONFIG_DP_SUBCONNECTOR_PROPERTY */

#ifndef HAVE_DRM_CONNECTOR_ATOMIC_HDR_METADATA_EQUAL
bool drm_connector_atomic_hdr_metadata_equal(struct drm_connector_state *old_state,
                                            struct drm_connector_state *new_state);
#endif

#if !defined(HAVE_DRM_CONNECTOR_ATTACH_HDR_OUTPUT_METADATA_PROPERTY)
int drm_connector_attach_hdr_output_metadata_property(struct drm_connector *connector);
#endif

#ifndef HAVE_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK
int _kcl_drm_connector_set_panel_orientation_with_quirk(
        struct drm_connector *connector,
        enum drm_panel_orientation panel_orientation,
        int width, int height);

static inline
int drm_connector_set_panel_orientation_with_quirk(
        struct drm_connector *connector,
        enum drm_panel_orientation panel_orientation,
        int width, int height)
{
       return _kcl_drm_connector_set_panel_orientation_with_quirk(connector, panel_orientation, width, height);
}
#endif

#ifndef HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
int _kcl_drm_connector_attach_colorspace_property(struct drm_connector *connector);
#define drm_connector_attach_colorspace_property _kcl_drm_connector_attach_colorspace_property
#endif /* HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY */

#ifndef HAVE_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY_2ARGS
#define KCL_DRM_MODE_CREATE_COLORSPACE_PROPERTY
int _kcl_drm_mode_create_hdmi_colorspace_property(struct drm_connector *connector,
					     u32 supported_colorspaces);
#define drm_mode_create_hdmi_colorspace_property _kcl_drm_mode_create_hdmi_colorspace_property
#endif /* HAVE_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY_2ARGS */

#ifndef HAVE_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY_2ARGS
#define KCL_DRM_MODE_CREATE_COLORSPACE_PROPERTY
int _kcl_drm_mode_create_dp_colorspace_property(struct drm_connector *connector,
					     u32 supported_colorspaces);
#define drm_mode_create_dp_colorspace_property _kcl_drm_mode_create_dp_colorspace_property
#endif /* HAVE_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY_2ARGS */

#ifdef KCL_DRM_MODE_CREATE_COLORSPACE_PROPERTY
#define DRM_MODE_COLORIMETRY_COUNT 16
#endif

#ifndef DRM_COLOR_FORMAT_YCBCR444
#define DRM_COLOR_FORMAT_YCBCR444      (1<<1)
#endif

#ifndef DRM_COLOR_FORMAT_YCBCR422
#define DRM_COLOR_FORMAT_YCBCR422      (1<<2)
#endif

#ifndef DRM_COLOR_FORMAT_YCBCR420
#define DRM_COLOR_FORMAT_YCBCR420      (1<<3)
#endif

/* For Default case, driver will set the colorspace */
#ifndef DRM_MODE_COLORIMETRY_DEFAULT
/* For Default case, driver will set the colorspace */
#define DRM_MODE_COLORIMETRY_DEFAULT			0
/* CEA 861 Normal Colorimetry options */
#define DRM_MODE_COLORIMETRY_NO_DATA			0
#define DRM_MODE_COLORIMETRY_SMPTE_170M_YCC		1
#define DRM_MODE_COLORIMETRY_BT709_YCC			2
/* CEA 861 Extended Colorimetry Options */
#define DRM_MODE_COLORIMETRY_XVYCC_601			3
#define DRM_MODE_COLORIMETRY_XVYCC_709			4
#define DRM_MODE_COLORIMETRY_SYCC_601			5
#define DRM_MODE_COLORIMETRY_OPYCC_601			6
#define DRM_MODE_COLORIMETRY_OPRGB			7
#define DRM_MODE_COLORIMETRY_BT2020_CYCC		8
#define DRM_MODE_COLORIMETRY_BT2020_RGB			9
#define DRM_MODE_COLORIMETRY_BT2020_YCC			10
/* Additional Colorimetry extension added as part of CTA 861.G */
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65		11
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER		12
/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
#define DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED		13
#define DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT		14
#define DRM_MODE_COLORIMETRY_BT601_YCC			15
#endif /* DRM_MODE_COLORIMETRY_DEFAULT */

#endif /* AMDKCL_DRM_CONNECTOR_H */
