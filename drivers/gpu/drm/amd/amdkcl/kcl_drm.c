#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if !defined(HAVE_DRM_MODESET_LOCK_ALL_CTX)
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	drm_for_each_crtc(crtc, dev) {
		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			return ret;
	}

	drm_for_each_plane(plane, dev) {
		ret = drm_modeset_lock(&plane->mutex, ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock_all_ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	int err;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	drm_for_each_connector(conn, dev) {
		struct drm_crtc *crtc = conn->state->crtc;
		struct drm_crtc_state *crtc_state;

		if (!crtc || conn->dpms != DRM_MODE_DPMS_ON)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}

		crtc_state->active = false;
	}

	err = drm_atomic_commit(state);

free:
	if (err < 0)
		drm_atomic_state_free(state);

	return err;
}
EXPORT_SYMBOL(drm_atomic_helper_disable_all);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int err = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}
	}

	drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			err = PTR_ERR(plane_state);
			goto free;
		}
	}

	drm_for_each_connector(conn, dev) {
		struct drm_connector_state *conn_state;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			goto free;
		}
	}

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;

free:
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
	}

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_duplicate_state);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_SUSPEND_RESUME)
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	int err;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	err = drm_modeset_lock_all_ctx(dev, &ctx);
	if (err < 0) {
		state = ERR_PTR(err);
		goto unlock;
	}

	state = drm_atomic_helper_duplicate_state(dev, &ctx);
	if (IS_ERR(state))
		goto unlock;

	err = drm_atomic_helper_disable_all(dev, &ctx);
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
		goto unlock;
	}

unlock:
	if (PTR_ERR(state) == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_suspend);

int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &dev->mode_config;
	int err;

	drm_mode_config_reset(dev);
	drm_modeset_lock_all(dev);
	state->acquire_ctx = config->acquire_ctx;
	err = drm_atomic_commit(state);
	drm_modeset_unlock_all(dev);

	return err;
}
EXPORT_SYMBOL(drm_atomic_helper_resume);
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
/**
 * drm_crtc_force_disable - Forcibly turn off a CRTC
 * @crtc: CRTC to turn off
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_force_disable(struct drm_crtc *crtc)
{
       struct drm_mode_set set = {
               .crtc = crtc,
       };

       return drm_mode_set_config_internal(&set);
}
EXPORT_SYMBOL(drm_crtc_force_disable);

/**
 * drm_crtc_force_disable_all - Forcibly turn off all enabled CRTCs
 * @dev: DRM device whose CRTCs to turn off
 *
 * Drivers may want to call this on unload to ensure that all displays are
 * unlit and the GPU is in a consistent, low power state. Takes modeset locks.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_force_disable_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	int ret = 0;

	drm_modeset_lock_all(dev);
	drm_for_each_crtc(crtc, dev)
		if (crtc->enabled) {
			ret = drm_crtc_force_disable(crtc);
			if (ret)
				goto out;
		}
out:
	drm_modeset_unlock_all(dev);
	return ret;
}
EXPORT_SYMBOL(drm_crtc_force_disable_all);
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

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->master;
}
EXPORT_SYMBOL(drm_is_current_master);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET)
void
__kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
	if (conn_state)
		conn_state->connector = connector;

	connector->state = conn_state;
}
EXPORT_SYMBOL(__kcl_drm_atomic_helper_connector_reset);
#endif

#if !defined(HAVE_DRM_GET_MAX_IOMEM)
u64 drm_get_max_iomem(void)
{
	struct resource *tmp;
	resource_size_t max_iomem = 0;

	for (tmp = iomem_resource.child; tmp; tmp = tmp->sibling) {
		max_iomem = max(max_iomem,  tmp->end);
	}

	return max_iomem;
}
EXPORT_SYMBOL(drm_get_max_iomem);
#endif

#if !defined(HAVE_DRM_SEND_EVENT_LOCKED)
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e)
{
	assert_spin_locked(&dev->event_lock);

	if (!e->file_priv) {
		kfree(e);
		return;
	}

	list_add_tail(&e->link,
		      &e->file_priv->event_list);
	wake_up_interruptible(&e->file_priv->event_wait);
}
EXPORT_SYMBOL(drm_send_event_locked);
#endif

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, fb_helper,
			       sizes->fb_width, sizes->fb_height);

	info->par = fb_helper;
	snprintf(info->fix.id, sizeof(info->fix.id), "%sdrmfb",
		 fb_helper->dev->driver->name);

}
EXPORT_SYMBOL(drm_fb_helper_fill_info);
#endif

#if !defined(HAVE_DRM_PRINTER)
void drm_printf(struct drm_printer *p, const char *f, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, f);
	vaf.fmt = f;
	vaf.va = &args;
	p->printfn(p, &vaf);
	va_end(args);
}
EXPORT_SYMBOL(drm_printf);
#endif

#if !defined(HAVE_DRM_DEBUG_PRINTER)
void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf)
{
#if !defined(HAVE_DRM_PRINTER)
	pr_debug("%s %pV", p->prefix, vaf);
#else
	pr_debug("%s %pV", "no prefix < 4.11", vaf);
#endif
}
EXPORT_SYMBOL(__drm_printfn_debug);
#endif

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

#ifdef HAVE_FB_INFO_APERTURES
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_free_cmap;
	}
#endif

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
