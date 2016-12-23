#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <linux/version.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>

extern void (*_kcl_drm_fb_helper_cfb_fillrect)(struct fb_info *info,
				const struct fb_fillrect *rect);
extern void (*_kcl_drm_fb_helper_cfb_copyarea)(struct fb_info *info,
				const struct fb_copyarea *area);
extern void (*_kcl_drm_fb_helper_cfb_imageblit)(struct fb_info *info,
				 const struct fb_image *image);
extern void (*_kcl_drm_fb_helper_unregister_fbi)(struct drm_fb_helper *fb_helper);
extern struct fb_info *(*_kcl_drm_fb_helper_alloc_fbi)(struct drm_fb_helper *fb_helper);
extern void (*_kcl_drm_fb_helper_release_fbi)(struct drm_fb_helper *fb_helper);
extern void (*_kcl_drm_fb_helper_set_suspend)(struct drm_fb_helper *fb_helper, int state);
extern void
(*_kcl_drm_atomic_helper_update_legacy_modeset_state)(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)  && \
	!defined(OS_NAME_UBUNTU)
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);
#endif

static inline void kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_fillrect(info, rect);
#else
	drm_fb_helper_cfb_fillrect(info, rect);
#endif
}

static inline void kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_copyarea(info, area);
#else
	drm_fb_helper_cfb_copyarea(info, area);
#endif
}

static inline void kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_imageblit(info, image);
#else
	drm_fb_helper_cfb_imageblit(info, image);
#endif
}

static inline struct fb_info *kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	return _kcl_drm_fb_helper_alloc_fbi(fb_helper);
#else
	return drm_fb_helper_alloc_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_release_fbi(fb_helper);
#else
	drm_fb_helper_release_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_unregister_fbi(fb_helper);
#else
	drm_fb_helper_unregister_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper, int state)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_set_suspend(fb_helper, state);
#else
	drm_fb_helper_set_suspend(fb_helper, state);
#endif
}

static inline void
kcl_drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
#else
	drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
#endif
}

#ifndef DRM_DEBUG_VBL
#define DRM_UT_VBL		0x20
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif

static inline int
kcl_drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
					  unsigned int pipe,
					  int *max_error,
					  struct timeval *vblank_time,
					  unsigned flags,
					  const struct drm_crtc *refcrtc,
					  const struct drm_display_mode *mode)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, refcrtc, mode);
#else
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, mode);
#endif
}

#endif /* AMDKCL_DRM_H */
