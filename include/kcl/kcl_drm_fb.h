/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_FB_H
#define KCL_KCL_DRM_FB_H

#include <linux/fb.h>
#include <linux/pci.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>

/*
 * Don't add fb_debug_* since the legacy drm_fb_helper_debug_* has segfault
 * history:
 * v2.6.35-21-gd219adc1228a fb: add hooks to handle KDB enter/exit
 * v2.6.35-22-g1a7aba7f4e45 drm: add KGDB/KDB support
 * v4.8-rc8-1391-g74064893901a drm/fb-helper: add DRM_FB_HELPER_DEFAULT_OPS for fb_ops
 * v4.9-rc4-808-g1e0089288b9b drm/fb-helper: add fb_debug_* to DRM_FB_HELPER_DEFAULT_OPS
 * v4.9-rc4-807-g1b99b72489c6 drm/fb-helper: fix segfaults in drm_fb_helper_debug_*
 * v4.10-rc8-1367-g0f3bbe074dd1 drm/fb-helper: implement ioctl FBIO_WAITFORVSYNC
 */
#ifndef DRM_FB_HELPER_DEFAULT_OPS
#define DRM_FB_HELPER_DEFAULT_OPS \
	.fb_check_var	= drm_fb_helper_check_var, \
	.fb_set_par	= drm_fb_helper_set_par, \
	.fb_setcmap	= drm_fb_helper_setcmap, \
	.fb_blank	= drm_fb_helper_blank, \
	.fb_pan_display	= drm_fb_helper_pan_display
#endif

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

#ifndef HAVE_DRM_FB_HELPER_XX_FBI
extern struct fb_info *_kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper);
extern void _kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper);

static inline
struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)

{
	return _kcl_drm_fb_helper_alloc_fbi(fb_helper);
}

static inline
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	_kcl_drm_fb_helper_unregister_fbi(fb_helper);
}
#endif

#ifndef HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED
extern void _kcl_drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, int state);
static inline
void drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper,
					bool suspend)

{
	_kcl_drm_fb_helper_set_suspend_unlocked(fb_helper, suspend);
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

#ifndef HAVE_DRM_HELPER_MODE_FILL_FB_STRUCT_DEV
void _kcl_drm_helper_mode_fill_fb_struct(struct drm_device *dev,
				    struct drm_framebuffer *fb,
				    const struct drm_mode_fb_cmd2 *mode_cmd);
#endif

#endif
