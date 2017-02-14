#ifndef AMDKCL_DRM_GLOBAL_H
#define AMDKCL_DRM_GLOBAL_H

#include <drm/drm_global.h>

#if defined(BUILD_AS_DKMS)
extern void _kcl_drm_global_init(void);
extern void _kcl_drm_global_release(void);
extern int _kcl_drm_global_item_ref(struct drm_global_reference *ref);
extern void _kcl_drm_global_item_unref(struct drm_global_reference *ref);
#endif

static inline void kcl_drm_global_init(void)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_drm_global_init();
#endif
}

static inline void kcl_drm_global_release(void)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_drm_global_release();
#else
	return drm_global_release();
#endif
}

static inline int kcl_drm_global_item_ref(struct drm_global_reference *ref)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_drm_global_item_ref(ref);
#else
	return drm_global_item_ref(ref);
#endif
}

static inline void kcl_drm_global_item_unref(struct drm_global_reference *ref)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_drm_global_item_unref(ref);
#else
	return drm_global_item_unref(ref);
#endif
}
#endif /*AMDKCL_DRM_GLOBAL_H*/