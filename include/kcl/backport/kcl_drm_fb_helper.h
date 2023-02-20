#ifndef _KCL_DRM_FB_HELP_BACKPORT_H_
#define _KCL_DRM_FB_HELP_BACKPORT_H_

#include <drm/drm_fb_helper.h>

#if defined(HAVE_DRM_FB_HELPER_ALLOC_INFO)
static inline struct fb_info *
_kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
        return drm_fb_helper_alloc_info(fb_helper);
}
#define drm_fb_helper_alloc_fbi _kcl_drm_fb_helper_alloc_fbi
#endif

#if defined(HAVE_DRM_FB_HELPER_UNREGISTER_INFO)
static inline void
_kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
        drm_fb_helper_unregister_info(fb_helper);
}
#define drm_fb_helper_unregister_fbi _kcl_drm_fb_helper_unregister_fbi
#endif
#endif
