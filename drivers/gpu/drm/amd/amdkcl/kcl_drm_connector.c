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
#include <kcl/kcl_drm_connector.h>

#ifndef HAVE_DRM_CONNECTOR_INIT_WITH_DDC
int _kcl_drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc)
{
	return drm_connector_init(dev, connector, funcs, connector_type);
}
EXPORT_SYMBOL(_kcl_drm_connector_init_with_ddc);
#endif

#ifndef HAVE_DRM_MODE_CONFIG_DP_SUBCONNECTOR_PROPERTY
amdkcl_dummy_symbol(drm_connector_attach_dp_subconnector_property, void, return,
				  struct drm_connector *connector)
amdkcl_dummy_symbol(drm_dp_set_subconnector_property, void, return,
				  struct drm_connector *connector, enum drm_connector_status status,
				  const u8 *dpcd, const u8 prot_cap[4])
#endif

#ifndef HAVE_DRM_CONNECTOR_ATOMIC_HDR_METADATA_EQUAL

bool drm_connector_atomic_hdr_metadata_equal(struct drm_connector_state *old_state,
                                             struct drm_connector_state *new_state)
{
#ifdef HAVE_DRM_CONNECTOR_STATE_HDR_OUTPUT_METADATA
        struct drm_property_blob *old_blob = old_state->hdr_output_metadata;
        struct drm_property_blob *new_blob = new_state->hdr_output_metadata;

        if (!old_blob || !new_blob)
                return old_blob == new_blob;

        if (old_blob->length != new_blob->length)
                return false;

        return !memcmp(old_blob->data, new_blob->data, old_blob->length);
#else
	return false;
#endif
}
EXPORT_SYMBOL(drm_connector_atomic_hdr_metadata_equal);
#endif

#if !defined(HAVE_DRM_CONNECTOR_ATTACH_HDR_OUTPUT_METADATA_PROPERTY)
int drm_connector_attach_hdr_output_metadata_property(struct drm_connector *connector)
{
#ifdef HAVE_DRM_CONNECTOR_ATTACH_HDR_OUTPUT_METADATA_PROPERTY
        struct drm_device *dev = connector->dev;
        struct drm_property *prop = dev->mode_config.hdr_output_metadata_property;

        drm_object_attach_property(&connector->base, prop, 0);
#endif

        return 0;
}
EXPORT_SYMBOL(drm_connector_attach_hdr_output_metadata_property);
#endif

#if !defined(HAVE_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK)
int _kcl_drm_connector_set_panel_orientation_with_quirk(
        struct drm_connector *connector,
        enum drm_panel_orientation panel_orientation,
        int width, int height)
{
	return drm_connector_init_panel_orientation_property(connector, width, height);
}
EXPORT_SYMBOL(_kcl_drm_connector_set_panel_orientation_with_quirk);
#endif

#ifndef HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
struct drm_property *prop = NULL;
int _kcl_drm_connector_attach_colorspace_property(struct drm_connector *connector)
{
	if(prop)
        drm_object_attach_property(&connector->base, prop, DRM_MODE_COLORIMETRY_DEFAULT);

        return 0;
}
EXPORT_SYMBOL(_kcl_drm_connector_attach_colorspace_property);
#endif

#ifdef KCL_DRM_MODE_CREATE_COLORSPACE_PROPERTY
/* copy from drivers/gpu/drm/drm_connector.c (v6.1-5788-gac3470b13f0d) */
static const char * const colorspace_names[] = {
	/* For Default case, driver will set the colorspace */
	[DRM_MODE_COLORIMETRY_DEFAULT] = "Default",
	/* Standard Definition Colorimetry based on CEA 861 */
	[DRM_MODE_COLORIMETRY_SMPTE_170M_YCC] = "SMPTE_170M_YCC",
	[DRM_MODE_COLORIMETRY_BT709_YCC] = "BT709_YCC",
	/* Standard Definition Colorimetry based on IEC 61966-2-4 */
	[DRM_MODE_COLORIMETRY_XVYCC_601] = "XVYCC_601",
	/* High Definition Colorimetry based on IEC 61966-2-4 */
	[DRM_MODE_COLORIMETRY_XVYCC_709] = "XVYCC_709",
	/* Colorimetry based on IEC 61966-2-1/Amendment 1 */
	[DRM_MODE_COLORIMETRY_SYCC_601] = "SYCC_601",
	/* Colorimetry based on IEC 61966-2-5 [33] */
	[DRM_MODE_COLORIMETRY_OPYCC_601] = "opYCC_601",
	/* Colorimetry based on IEC 61966-2-5 */
	[DRM_MODE_COLORIMETRY_OPRGB] = "opRGB",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_CYCC] = "BT2020_CYCC",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_RGB] = "BT2020_RGB",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_YCC] = "BT2020_YCC",
	/* Added as part of Additional Colorimetry Extension in 861.G */
	[DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65] = "DCI-P3_RGB_D65",
	[DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER] = "DCI-P3_RGB_Theater",
	[DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED] = "RGB_WIDE_FIXED",
	/* Colorimetry based on scRGB (IEC 61966-2-2) */
	[DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT] = "RGB_WIDE_FLOAT",
	[DRM_MODE_COLORIMETRY_BT601_YCC] = "BT601_YCC",
};

static const u32 hdmi_colorspaces =
	BIT(DRM_MODE_COLORIMETRY_SMPTE_170M_YCC) |
	BIT(DRM_MODE_COLORIMETRY_BT709_YCC) |
	BIT(DRM_MODE_COLORIMETRY_XVYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_XVYCC_709) |
	BIT(DRM_MODE_COLORIMETRY_SYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_OPYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_OPRGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_CYCC) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_RGB) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_YCC) |
	BIT(DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65) |
	BIT(DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER);

static const u32 dp_colorspaces =
	BIT(DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED) |
	BIT(DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT) |
	BIT(DRM_MODE_COLORIMETRY_OPRGB) |
	BIT(DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_RGB) |
	BIT(DRM_MODE_COLORIMETRY_BT601_YCC) |
	BIT(DRM_MODE_COLORIMETRY_BT709_YCC) |
	BIT(DRM_MODE_COLORIMETRY_XVYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_XVYCC_709) |
	BIT(DRM_MODE_COLORIMETRY_SYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_OPYCC_601) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_CYCC) |
	BIT(DRM_MODE_COLORIMETRY_BT2020_YCC);

static int drm_mode_create_colorspace_property(struct drm_connector *connector,
					u32 supported_colorspaces)
{
	struct drm_device *dev = connector->dev;
	u32 colorspaces = supported_colorspaces | BIT(DRM_MODE_COLORIMETRY_DEFAULT);
	struct drm_prop_enum_list enum_list[DRM_MODE_COLORIMETRY_COUNT];
	int i, len;

#ifdef HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
	if (connector->colorspace_property)
#else
	if (prop)
#endif
		return 0;


	if (!supported_colorspaces) {
		drm_err(dev, "No supported colorspaces provded on [CONNECTOR:%d:%s]\n",
			    connector->base.id, connector->name);
		return -EINVAL;
	}

	if ((supported_colorspaces & -BIT(DRM_MODE_COLORIMETRY_COUNT)) != 0) {
		drm_err(dev, "Unknown colorspace provded on [CONNECTOR:%d:%s]\n",
			    connector->base.id, connector->name);
		return -EINVAL;
	}

	len = 0;
	for (i = 0; i < DRM_MODE_COLORIMETRY_COUNT; i++) {
		if ((colorspaces & BIT(i)) == 0)
			continue;

		enum_list[len].type = i;
		enum_list[len].name = colorspace_names[i];
		len++;
	}
#ifdef HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
	connector->colorspace_property =
#else
	prop =
#endif
		drm_property_create_enum(dev, DRM_MODE_PROP_ENUM, "Colorspace",
					enum_list,
					len);

#ifdef HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
	if (!connector->colorspace_property)
#else
	if (!prop)
#endif
		return -ENOMEM;

	return 0;
}
#endif /* KCL_DRM_MODE_CREATE_COLORSPACE_PROPERTY */

#ifndef HAVE_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY_2ARGS
int _kcl_drm_mode_create_hdmi_colorspace_property(struct drm_connector *connector,
                                             u32 supported_colorspaces)
{
	u32 colorspaces;

	if (supported_colorspaces)
		colorspaces = supported_colorspaces & hdmi_colorspaces;
	else
		colorspaces = hdmi_colorspaces;

	return drm_mode_create_colorspace_property(connector, colorspaces);
}
EXPORT_SYMBOL(_kcl_drm_mode_create_hdmi_colorspace_property);
#endif

#ifndef HAVE_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY_2ARGS
int _kcl_drm_mode_create_dp_colorspace_property(struct drm_connector *connector,
                                             u32 supported_colorspaces)
{
	u32 colorspaces;

	if (supported_colorspaces)
		colorspaces = supported_colorspaces & dp_colorspaces;
	else
		colorspaces = dp_colorspaces;

	return drm_mode_create_colorspace_property(connector, colorspaces);
}
EXPORT_SYMBOL(_kcl_drm_mode_create_dp_colorspace_property);
#endif