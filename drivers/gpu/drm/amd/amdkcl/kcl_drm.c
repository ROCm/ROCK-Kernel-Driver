#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if DRM_VERSION_CODE < DRM_VERSION(4, 8, 0)
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
	kcl_drm_for_each_crtc(crtc, dev)
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

#if DRM_VERSION_CODE < DRM_VERSION(4, 5, 0) && !(defined(OS_NAME_UBUNTU) || defined(OS_NAME_SLE))
int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw)
{
	struct pci_dev *root;
	u32 lnkcap;

	*mlw = 0;
	if (!dev->pdev)
		return -EINVAL;

	root = dev->pdev->bus->self;
	if (!root)
		return -EINVAL;

	pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);

	*mlw = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	DRM_INFO("probing mlw for device %x:%x = %x\n", root->vendor, root->device, lnkcap);
	return 0;
}
EXPORT_SYMBOL(drm_pcie_get_max_link_width);
#endif

void (*_kcl_drm_fb_helper_cfb_fillrect)(struct fb_info *info,
				const struct fb_fillrect *rect);
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_fillrect);

void (*_kcl_drm_fb_helper_cfb_copyarea)(struct fb_info *info,
				const struct fb_copyarea *area);
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_copyarea);

void (*_kcl_drm_fb_helper_cfb_imageblit)(struct fb_info *info,
				 const struct fb_image *image);
EXPORT_SYMBOL(_kcl_drm_fb_helper_cfb_imageblit);

void (*_kcl_drm_fb_helper_unregister_fbi)(struct drm_fb_helper *fb_helper);
EXPORT_SYMBOL(_kcl_drm_fb_helper_unregister_fbi);

struct fb_info *(*_kcl_drm_fb_helper_alloc_fbi)(struct drm_fb_helper *fb_helper);
EXPORT_SYMBOL(_kcl_drm_fb_helper_alloc_fbi);

void (*_kcl_drm_fb_helper_release_fbi)(struct drm_fb_helper *fb_helper);
EXPORT_SYMBOL(_kcl_drm_fb_helper_release_fbi);

void (*_kcl_drm_fb_helper_set_suspend_unlocked)(struct drm_fb_helper *fb_helper, int state);
EXPORT_SYMBOL(_kcl_drm_fb_helper_set_suspend_unlocked);

void
(*_kcl_drm_atomic_helper_update_legacy_modeset_state)(struct drm_device *dev,
				struct drm_atomic_state *old_state);
EXPORT_SYMBOL(_kcl_drm_atomic_helper_update_legacy_modeset_state);

/**
 * _kcl_drm_fb_helper_cfb_fillrect_stub - wrapper around cfb_fillrect
 * @info: fbdev registered by the helper
 * @rect: info about rectangle to fill
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_fillrect_stub(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
}

/**
 * _kcl_drm_fb_helper_cfb_copyarea_stub - wrapper around cfb_copyarea
 * @info: fbdev registered by the helper
 * @area: info about area to copy
 *
 * A wrapper around cfb_copyarea implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_copyarea_stub(struct fb_info *info,
				const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
}

/**
 * _kcl_drm_fb_helper_cfb_imageblit_stub - wrapper around cfb_imageblit
 * @info: fbdev registered by the helper
 * @image: info about image to blit
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void _kcl_drm_fb_helper_cfb_imageblit_stub(struct fb_info *info,
				 const struct fb_image *image)
{
	cfb_imageblit(info, image);
}

/**
 * _kcl_drm_fb_helper_alloc_fbi_stub - allocate fb_info and some of its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to alloc fb_info and the members cmap and apertures. Called
 * by the driver within the fb_probe fb_helper callback function.
 *
 * RETURNS:
 * fb_info pointer if things went okay, pointer containing error code
 * otherwise
 */
struct fb_info *_kcl_drm_fb_helper_alloc_fbi_stub(struct drm_fb_helper *fb_helper)
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
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

/**
 * _kcl_drm_fb_helper_unregister_fbi_stub - unregister fb_info framebuffer device
 * @fb_helper: driver-allocated fbdev helper
 *
 * A wrapper around unregister_framebuffer, to release the fb_info
 * framebuffer device
 */
void _kcl_drm_fb_helper_unregister_fbi_stub(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}

/**
 * _kcl_drm_fb_helper_release_fbi_stub - dealloc fb_info and its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to free memory taken by fb_info and the members cmap and
 * apertures
 */
void _kcl_drm_fb_helper_release_fbi_stub(struct drm_fb_helper *fb_helper)
{
	if (fb_helper) {
		struct fb_info *info = fb_helper->fbdev;

		if (info) {
			if (info->cmap.len)
				fb_dealloc_cmap(&info->cmap);
			framebuffer_release(info);
		}

		fb_helper->fbdev = NULL;
	}
}

/**
 * _kcl_drm_fb_helper_set_suspend_stub - wrapper around fb_set_suspend
 * @fb_helper: driver-allocated fbdev helper
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * A wrapper around fb_set_suspend implemented by fbdev core
 */
void _kcl_drm_fb_helper_set_suspend_unlocked_stub(struct drm_fb_helper *fb_helper, int state)
{
	if (!fb_helper || !fb_helper->fbdev)
		return;
#if DRM_VERSION_CODE >=  DRM_VERSION(4, 9, 0)
	/* make sure there's no pending/ongoing resume */
	flush_work(&fb_helper->resume_work);

	if (state) {
		if (fb_helper->fbdev->state != FBINFO_STATE_RUNNING)
			return;

		console_lock();

	} else {
		if (fb_helper->fbdev->state == FBINFO_STATE_RUNNING)
			return;

		if (!console_trylock()) {
			schedule_work(&fb_helper->resume_work);

			return;
		}
	}
#else
	console_lock();
#endif
	fb_set_suspend(fb_helper->fbdev, state);
	console_unlock();
}

static inline bool
_kcl_drm_atomic_crtc_needs_modeset(struct drm_crtc_state *state)
{
    return state->mode_changed || state->active_changed ;
}

static inline struct drm_plane_state *
_kcl_drm_atomic_get_existing_plane_state(struct drm_atomic_state *state,
                    struct drm_plane *plane)
{
#if DRM_VERSION_CODE < DRM_VERSION(4, 8, 0)
	return state->plane_states[drm_plane_index(plane)];
#else
	return state->planes[drm_plane_index(plane)].state;
#endif
}

#if DRM_VERSION_CODE >= DRM_VERSION(4, 15, 0)

#define for_each_connector_in_state(__state, connector, connector_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->num_connector &&				\
	     ((connector) = (__state)->connectors[__i].ptr,			\
	     (connector_state) = (__state)->connectors[__i].state, 1); 	\
	     (__i)++)							\
		for_each_if (connector)

#define for_each_crtc_in_state(__state, crtc, crtc_state, __i)	\
	for ((__i) = 0;						\
	     (__i) < (__state)->dev->mode_config.num_crtc &&	\
	     ((crtc) = (__state)->crtcs[__i].ptr,			\
	     (crtc_state) = (__state)->crtcs[__i].state, 1);	\
	     (__i)++)						\
		for_each_if (crtc_state)

#endif

void
_kcl_drm_atomic_helper_update_legacy_modeset_state_stub(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	/* clear out existing links and update dpms */
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (connector->encoder) {
			WARN_ON(!connector->encoder->crtc);

			connector->encoder->crtc = NULL;
			connector->encoder = NULL;
		}

		crtc = connector->state->crtc;
		if ((!crtc && old_conn_state->crtc) ||
		    (crtc && _kcl_drm_atomic_crtc_needs_modeset(crtc->state))) {
			int mode = DRM_MODE_DPMS_OFF;

			if (crtc && crtc->state->active)
				mode = DRM_MODE_DPMS_ON;

			connector->dpms = mode;
		}
	}

	/* set new links */
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (!connector->state->crtc)
			continue;

		if (WARN_ON(!connector->state->best_encoder))
			continue;

		connector->encoder = connector->state->best_encoder;
		connector->encoder->crtc = connector->state->crtc;
	}

	/* set legacy state in the crtc structure */
	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		struct drm_plane *primary = crtc->primary;

		crtc->mode = crtc->state->mode;
		crtc->enabled = crtc->state->enable;

		if (_kcl_drm_atomic_get_existing_plane_state(old_state, primary) &&
		    primary->state->crtc == crtc) {
			crtc->x = primary->state->src_x >> 16;
			crtc->y = primary->state->src_y >> 16;
		}

		if (crtc->state->enable)
			drm_calc_timestamping_constants(crtc,
							&crtc->state->adjusted_mode);
	}
}

#if DRM_VERSION_CODE < DRM_VERSION(4, 5, 0) && !(defined(OS_NAME_UBUNTU) || defined(OS_NAME_SLE))
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	kcl_drm_for_each_crtc(crtc, dev) {
		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			return ret;
	}

	kcl_drm_for_each_plane(plane, dev) {
		ret = drm_modeset_lock(&plane->mutex, ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock_all_ctx);

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

	kcl_drm_for_each_connector(conn, dev) {
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

#if !defined(OS_NAME_RHEL_6) && !defined(OS_NAME_AMZ)
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

	kcl_drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}
	}

	kcl_drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			err = PTR_ERR(plane_state);
			goto free;
		}
	}

	kcl_drm_for_each_connector(conn, dev) {
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

void amdkcl_drm_init(void)
{
	_kcl_drm_fb_helper_cfb_fillrect = amdkcl_fp_setup("drm_fb_helper_cfb_fillrect",
					_kcl_drm_fb_helper_cfb_fillrect_stub);
	_kcl_drm_fb_helper_cfb_copyarea = amdkcl_fp_setup("drm_fb_helper_cfb_copyarea",
					_kcl_drm_fb_helper_cfb_copyarea_stub);
	_kcl_drm_fb_helper_cfb_imageblit = amdkcl_fp_setup("drm_fb_helper_cfb_imageblit",
					_kcl_drm_fb_helper_cfb_imageblit_stub);
	_kcl_drm_fb_helper_alloc_fbi = amdkcl_fp_setup("drm_fb_helper_alloc_fbi",
					_kcl_drm_fb_helper_alloc_fbi_stub);
	_kcl_drm_fb_helper_unregister_fbi = amdkcl_fp_setup("drm_fb_helper_unregister_fbi",
					_kcl_drm_fb_helper_unregister_fbi_stub);
	_kcl_drm_fb_helper_release_fbi = amdkcl_fp_setup("drm_fb_helper_release_fbi",
					_kcl_drm_fb_helper_release_fbi_stub);
	_kcl_drm_fb_helper_set_suspend_unlocked = amdkcl_fp_setup("drm_fb_helper_set_suspend_unlocked",
					_kcl_drm_fb_helper_set_suspend_unlocked_stub);
	_kcl_drm_atomic_helper_update_legacy_modeset_state = amdkcl_fp_setup(
					"drm_atomic_helper_update_legacy_modeset_state",
					_kcl_drm_atomic_helper_update_legacy_modeset_state_stub);
}

#if DRM_VERSION_CODE < DRM_VERSION(4, 8, 0)
bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->master;
}
EXPORT_SYMBOL(drm_is_current_master);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
void __drm_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	dev_printk(KERN_INFO, p->arg, "[" DRM_NAME "] %pV", vaf);
}
EXPORT_SYMBOL(__drm_printfn_info);

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

static void drm_atomic_crtc_print_state(struct drm_printer *p,
		const struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;

#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
	drm_printf(p, "crtc[%u]: %s\n", crtc->base.id, crtc->name);
#else
	drm_printf(p, "crtc[%u]:\n", crtc->base.id);
#endif
	drm_printf(p, "\tenable=%d\n", state->enable);
	drm_printf(p, "\tactive=%d\n", state->active);
	drm_printf(p, "\tplanes_changed=%d\n", state->planes_changed);
	drm_printf(p, "\tmode_changed=%d\n", state->mode_changed);
	drm_printf(p, "\tactive_changed=%d\n", state->active_changed);
#if DRM_VERSION_CODE >= DRM_VERSION(4, 3, 0)
	drm_printf(p, "\tconnectors_changed=%d\n", state->connectors_changed);
#endif
#if DRM_VERSION_CODE >= DRM_VERSION(4, 6, 0)
	drm_printf(p, "\tcolor_mgmt_changed=%d\n", state->color_mgmt_changed);
#endif
#if DRM_VERSION_CODE >= DRM_VERSION(3, 19, 0)
	drm_printf(p, "\tplane_mask=%x\n", state->plane_mask);
#endif
#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
	drm_printf(p, "\tconnector_mask=%x\n", state->connector_mask);
#endif
#if DRM_VERSION_CODE >= DRM_VERSION(4, 6, 0)
	drm_printf(p, "\tencoder_mask=%x\n", state->encoder_mask);
#endif
	drm_printf(p, "\tmode: " DRM_MODE_FMT "\n", DRM_MODE_ARG(&state->mode));
}

static void drm_atomic_plane_print_state(struct drm_printer *p,
		const struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_rect src  = drm_plane_state_src(state);
	struct drm_rect dest = drm_plane_state_dest(state);

#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
	drm_printf(p, "plane[%u]: %s\n", plane->base.id, plane->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
#else
	drm_printf(p, "plane[%u]\n", plane->base.id);
	drm_printf(p, "\tcrtc=%u\n", state->crtc ? state->crtc->base.id : 0);
#endif
	drm_printf(p, "\tfb=%u\n", state->fb ? state->fb->base.id : 0);
	if (state->fb) {
		struct drm_framebuffer *fb = state->fb;
		int i, n = drm_format_num_planes(fb->pixel_format);
		struct drm_format_name_buf format_name;

		drm_printf(p, "\t\tformat=%s\n",
				kcl_drm_get_format_name(fb->pixel_format, &format_name));
		drm_printf(p, "\t\t\tmodifier=0x%llx\n", fb->modifier);
		drm_printf(p, "\t\tsize=%dx%d\n", fb->width, fb->height);
		drm_printf(p, "\t\tlayers:\n");
		for (i = 0; i < n; i++) {
			drm_printf(p, "\t\t\tpitch[%d]=%u\n", i, fb->pitches[i]);
			drm_printf(p, "\t\t\toffset[%d]=%u\n", i, fb->offsets[i]);
		}
	}
	drm_printf(p, "\tcrtc-pos=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&dest));
	drm_printf(p, "\tsrc-pos=" DRM_RECT_FP_FMT "\n", DRM_RECT_FP_ARG(&src));
	drm_printf(p, "\trotation=%x\n", state->rotation);
}

static void drm_atomic_connector_print_state(struct drm_printer *p,
		const struct drm_connector_state *state)
{
	struct drm_connector *connector = state->connector;

	drm_printf(p, "connector[%u]: %s\n", connector->base.id, connector->name);
#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
#else
	drm_printf(p, "\tcrtc=%d\n", state->crtc ? state->crtc->base.id : 0);
#endif
}

static void drm_atomic_print_state(const struct drm_atomic_state *state)
{
	struct drm_printer p = drm_info_printer(state->dev->dev);
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	DRM_DEBUG_ATOMIC("checking %p\n", state);

	for_each_plane_in_state(state, plane, plane_state, i)
		drm_atomic_plane_print_state(&p, plane_state);

	for_each_crtc_in_state(state, crtc, crtc_state, i)
		drm_atomic_crtc_print_state(&p, crtc_state);

	for_each_connector_in_state(state, connector, connector_state, i)
		drm_atomic_connector_print_state(&p, connector_state);
}

void drm_state_dump(struct drm_device *dev, struct drm_printer *p)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_connector *connector;

	if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
		return;

	list_for_each_entry(plane, &config->plane_list, head)
		drm_atomic_plane_print_state(p, plane->state);

	list_for_each_entry(crtc, &config->crtc_list, head)
		drm_atomic_crtc_print_state(p, crtc->state);

	list_for_each_entry(connector, &config->connector_list, head)
		drm_atomic_connector_print_state(p, connector->state);
}
EXPORT_SYMBOL(drm_state_dump);
#endif


#if DRM_VERSION_CODE < DRM_VERSION(4, 17, 0) && defined(BUILD_AS_DKMS)
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

#if DRM_VERSION_CODE < DRM_VERSION(4, 6, 0)
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

void drm_send_event(struct drm_device *dev, struct drm_pending_event *e)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);
	drm_send_event_locked(dev, e);
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}
EXPORT_SYMBOL(drm_send_event);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
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
