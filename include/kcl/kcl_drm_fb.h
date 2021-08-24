/*
 * Copyright (c) 2006-2009 Red Hat Inc.
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM framebuffer helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#ifndef KCL_KCL_DRM_FB_H
#define KCL_KCL_DRM_FB_H

#include <linux/fb.h>
#include <linux/pci.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>

/* Copied from include/drm/drm_fb_helper.h */
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

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS)
#if !defined(IS_REACHABLE)
/* Copied from include/linux/kconfig.h */
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

#ifdef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG
extern int remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
                                               const char *name);
#else
extern int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id,
					       const char *name);
#endif

static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
#if IS_REACHABLE(CONFIG_FB)
#ifdef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG
	return remove_conflicting_pci_framebuffers(pdev, name);
#else
	return remove_conflicting_pci_framebuffers(pdev, 0, name);
#endif
#else
	return 0;
#endif
}
#elif !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
#ifdef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_WITH_RES_ID_ARG
	return drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, 0, name);
#else
	return drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, name);
#endif
}
#endif /* HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS */

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes);
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
#endif
