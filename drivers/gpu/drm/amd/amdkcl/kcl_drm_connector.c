/* SPDX-License-Identifier: MIT */
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
