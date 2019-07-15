#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <kcl/kcl_drm.h>

#if DRM_VERSION_CODE >= DRM_VERSION(4, 17, 0)
#define AMDKCL_AMDGPU_DMABUF_OPS
#endif

#if !defined(HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE)
#define drm_encoder_find(dev, file, id) drm_encoder_find(dev, id)
#endif

#if defined(HAVE_DRM_EDID_TO_ELD)
static inline
int _kcl_drm_add_edid_modes(struct drm_connector *connector, struct edid *edid)
{
	int ret;

	ret = drm_add_edid_modes(connector, edid);

	if (drm_edid_is_valid(edid))
		drm_edid_to_eld(connector, edid);

	return ret;
}
#define drm_add_edid_modes _kcl_drm_add_edid_modes
#endif

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
#define drm_fb_helper_remove_conflicting_pci_framebuffers _kcl_drm_fb_helper_remove_conflicting_pci_framebuffers
#endif

#if !defined(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME)
static inline int _kcl_drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
	return drm_encoder_init(dev, encoder, funcs, encoder_type);
}
#define drm_encoder_init _kcl_drm_encoder_init
#endif

#endif /* AMDKCL_DRM_BACKPORT_H */
