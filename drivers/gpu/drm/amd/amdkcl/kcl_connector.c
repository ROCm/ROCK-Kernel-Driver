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

#ifdef CONFIG_DRM_AMD_DC_HDCP
#ifndef HAVE_DRM_HDCP_UPDATE_CONTENT_PROTECTION
void _kcl_drm_hdcp_update_content_protection(struct drm_connector *connector,
                                       u64 val)
{
       struct drm_device *dev = connector->dev;
       struct drm_connector_state *state = connector->state;

       WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
       if (state->content_protection == val)
               return;

       state->content_protection = val;
}
EXPORT_SYMBOL(_kcl_drm_hdcp_update_content_protection);
#endif
#endif
