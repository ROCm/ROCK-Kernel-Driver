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
#include <kcl/kcl_kref.h>
#include <kcl/kcl_drm_crtc.h>

/*
 * commit v4.9-rc4-949-g949f08862d66
 * drm: Make the connector .detect() callback optional
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#define AMDKCL_AMDGPU_DRM_CONNECTOR_STATUS_DETECT_MANDATORY
#endif

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

#ifndef DRM_COLOR_FORMAT_YCBCR444
#define DRM_COLOR_FORMAT_YCBCR444      (1<<1)
#endif

#ifndef DRM_COLOR_FORMAT_YCBCR422
#define DRM_COLOR_FORMAT_YCBCR422      (1<<2)
#endif

#ifndef DRM_COLOR_FORMAT_YCBCR420
#define DRM_COLOR_FORMAT_YCBCR420      (1<<3)
#endif

#endif /* AMDKCL_DRM_CONNECTOR_H */
