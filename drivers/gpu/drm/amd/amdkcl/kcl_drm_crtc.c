/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm_crtc.h>

#ifndef HAVE_DRM_HELPER_FORCE_DISABLE_ALL
int _kcl_drm_helper_force_disable_all(struct drm_device *dev)
{
       struct drm_crtc *crtc;
       int ret = 0;

       drm_modeset_lock_all(dev);
       drm_for_each_crtc(crtc, dev)
               if (crtc->enabled) {
                       struct drm_mode_set set = {
                               .crtc = crtc,
                       };

                       ret = drm_mode_set_config_internal(&set);
                       if (ret)
                               goto out;
               }
out:
       drm_modeset_unlock_all(dev);
       return ret;
}
EXPORT_SYMBOL(_kcl_drm_helper_force_disable_all);
#endif

#ifndef HAVE_DRM_CRTC_FROM_INDEX
struct drm_crtc *_kcl_drm_crtc_from_index(struct drm_device *dev, int idx)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev)
		if (idx == drm_crtc_index(crtc))
			return crtc;

	return NULL;
}
EXPORT_SYMBOL(_kcl_drm_crtc_from_index);
#endif
