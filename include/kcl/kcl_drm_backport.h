#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc.h>
#include <drm/drm_cache.h>
#ifdef HAVE_DRM_FILE_H
#include <drm/drm_file.h>
#endif
#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
#include <drm/drm_syncobj.h>
#endif
#include <kcl/kcl_drm.h>

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

#ifdef BUILD_AS_DKMS
#define drm_arch_can_wc_memory kcl_drm_arch_can_wc_memory
#endif

#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
static inline
int _kcl_drm_syncobj_find_fence(struct drm_file *file_private,
						u32 handle, u64 point, u64 flags,
						struct dma_fence **fence)
{
#if defined(HAVE_DRM_SYNCOBJ_FIND_FENCE)
#if defined(HAVE_DRM_SYNCOBJ_FIND_FENCE_5ARGS)
	return drm_syncobj_find_fence(file_private, handle, point, flags, fence);
#elif defined(HAVE_DRM_SYNCOBJ_FIND_FENCE_4ARGS)
	return drm_syncobj_find_fence(file_private, handle, point, fence);
#else
	return drm_syncobj_find_fence(file_private, handle, fence);
#endif
#elif defined(HAVE_DRM_SYNCOBJ_FENCE_GET)
	return drm_syncobj_fence_get(file_private, handle, fence);
#endif
}
#define drm_syncobj_find_fence _kcl_drm_syncobj_find_fence
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
#endif
