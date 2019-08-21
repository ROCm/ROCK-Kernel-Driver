/* SPDX-License-Identifier: MIT */
#include <kcl/header/kcl_drm_device_h.h>
#include <kcl/header/kcl_drm_drv_h.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <kcl/kcl_drm_fb.h>
#include "kcl_common.h"

#ifndef HAVE_DRM_FB_HELPER_CFB_XX
/**
 * _kcl_drm_fb_helper_cfb_fillrect - wrapper around cfb_fillrect
 * @info: fbdev registered by the helper
 * @rect: info about rectangle to fill
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_fillrect);

/**
 * _kcl_drm_fb_helper_cfb_copyarea - wrapper around cfb_copyarea
 * @info: fbdev registered by the helper
 * @area: info about area to copy
 *
 * A wrapper around cfb_copyarea implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_copyarea);

/**
 * _kcl_drm_fb_helper_cfb_imageblit - wrapper around cfb_imageblit
 * @info: fbdev registered by the helper
 * @image: info about image to blit
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	cfb_imageblit(info, image);
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_imageblit);
#endif

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS)
int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id, const char *name)
{
	struct apertures_struct *ap;
	bool primary = false;
	int err = 0;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, res_id);
	ap->ranges[0].size = pci_resource_len(pdev, res_id);
#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags &
					IORESOURCE_ROM_SHADOW;
#endif
#ifdef HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT
	err = remove_conflicting_framebuffers(ap, name, primary);
#else
	remove_conflicting_framebuffers(ap, name, primary);
#endif
	kfree(ap);
	return err;
}
EXPORT_SYMBOL(remove_conflicting_pci_framebuffers);
#endif

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb = fb_helper->fb;

#ifdef HAVE_DRM_FRAMEBUFFER_FORMAT
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
#else
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
#endif
	drm_fb_helper_fill_var(info, fb_helper,
			       sizes->fb_width, sizes->fb_height);

	info->par = fb_helper;
	snprintf(info->fix.id, sizeof(info->fix.id), "%sdrmfb",
		 fb_helper->dev->driver->name);

}
EXPORT_SYMBOL(drm_fb_helper_fill_info);
#endif
