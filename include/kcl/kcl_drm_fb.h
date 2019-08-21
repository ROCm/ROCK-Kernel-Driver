/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_FB_H
#define KCL_KCL_DRM_FB_H

#include <linux/fb.h>
#include <linux/pci.h>
#include <drm/drm_fb_helper.h>

#ifndef HAVE_DRM_FB_HELPER_CFB_XX
extern void _kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
extern void _kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
extern void _kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image);

static inline
void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	_kcl_drm_fb_helper_cfb_fillrect(info, rect);
}

static inline
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	_kcl_drm_fb_helper_cfb_copyarea(info, area);
}

static inline
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	_kcl_drm_fb_helper_cfb_imageblit(info, image);
}
#endif

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS)
#if !defined(IS_REACHABLE)
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val

/*
 * The use of "&&" / "||" is limited in certain expressions.
 * The followings enable to calculate "and" / "or" with macro expansion only.
 */
#define __and(x, y)			___and(x, y)
#define ___and(x, y)			____and(__ARG_PLACEHOLDER_##x, y)
#define ____and(arg1_or_junk, y)	__take_second_arg(arg1_or_junk y, 0)

#define __or(x, y)			___or(x, y)
#define ___or(x, y)			____or(__ARG_PLACEHOLDER_##x, y)
#define ____or(arg1_or_junk, y)		__take_second_arg(arg1_or_junk 1, y)

#define IS_REACHABLE(option) __or(IS_BUILTIN(option), \
				__and(IS_MODULE(option), __is_defined(MODULE)))
#endif /*IS_REACHABLE*/

extern int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id,
					       const char *name);
static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_pci_framebuffers(pdev, 0, name);
#else
	return 0;
#endif
}
#elif !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
	return drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, 0, name);
}
#endif /* HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS */

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes);
#endif

#endif
