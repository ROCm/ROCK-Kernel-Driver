/* SPDX-License-Identifier: MIT */
#include <linux/console.h>
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

#ifndef HAVE_DRM_FB_HELPER_XX_FBI
/**
 * _kcl_drm_fb_helper_alloc_fbi - allocate fb_info and some of its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to alloc fb_info and the members cmap and apertures. Called
 * by the driver within the fb_probe fb_helper callback function.
 *
 * RETURNS:
 * fb_info pointer if things went okay, pointer containing error code
 * otherwise
 */
struct fb_info *_kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
	struct device *dev = fb_helper->dev->dev;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(0, dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_release;

	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_free_cmap;
	}

	fb_helper->fbdev = info;

	return info;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_release:
	framebuffer_release(info);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_alloc_fbi);

/**
 * _kcl_drm_fb_helper_unregister_fbi - unregister fb_info framebuffer device
 * @fb_helper: driver-allocated fbdev helper
 *
 * A wrapper around unregister_framebuffer, to release the fb_info
 * framebuffer device
 */
void _kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_unregister_fbi);
#endif

#ifndef HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED
/**
 * _kcl_drm_fb_helper_set_suspend_stub - wrapper around fb_set_suspend
 * @fb_helper: driver-allocated fbdev helper
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * A wrapper around fb_set_suspend implemented by fbdev core
 */
void _kcl_drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, int state)
{
	if (!fb_helper || !fb_helper->fbdev)
		return;

	console_lock();
	fb_set_suspend(fb_helper->fbdev, state);
	console_unlock();
}
EXPORT_SYMBOL(_kcl_drm_fb_helper_set_suspend_unlocked);
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

#ifndef HAVE_DRM_HELPER_MODE_FILL_FB_STRUCT_DEV
void _kcl_drm_helper_mode_fill_fb_struct(struct drm_device *dev,
				    struct drm_framebuffer *fb,
				    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	fb->dev = dev;
	drm_helper_mode_fill_fb_struct(fb, mode_cmd);
}
EXPORT_SYMBOL(_kcl_drm_helper_mode_fill_fb_struct);
#endif
