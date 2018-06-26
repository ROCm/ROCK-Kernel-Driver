#include <kcl/kcl_drm.h>
#include "kcl_common.h"

void (*_kcl_drm_fb_helper_set_suspend)(struct drm_fb_helper *fb_helper, int state);
EXPORT_SYMBOL(_kcl_drm_fb_helper_set_suspend);

void
(*_kcl_drm_atomic_helper_update_legacy_modeset_state)(struct drm_device *dev,
				struct drm_atomic_state *old_state);
EXPORT_SYMBOL(_kcl_drm_atomic_helper_update_legacy_modeset_state);

/**
 * _kcl_drm_fb_helper_set_suspend_stub - wrapper around fb_set_suspend
 * @fb_helper: driver-allocated fbdev helper
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * A wrapper around fb_set_suspend implemented by fbdev core
 */
void _kcl_drm_fb_helper_set_suspend_stub(struct drm_fb_helper *fb_helper, int state)
{
	if (fb_helper && fb_helper->fbdev)
		fb_set_suspend(fb_helper->fbdev, state);
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

#if DRM_VERSION_CODE >= DRM_VERSION(4, 15, 0) || defined(OS_NAME_SUSE_15_1)

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
			struct drm_property *dpms_prop =
				dev->mode_config.dpms_property;
			int mode = DRM_MODE_DPMS_OFF;

			if (crtc && crtc->state->active)
				mode = DRM_MODE_DPMS_ON;

			connector->dpms = mode;
			drm_object_property_set_value(&connector->base,
						      dpms_prop, mode);
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0) && \
	!defined(OS_NAME_UBUNTU) && !defined(OS_NAME_RHEL_7_3) && \
	!defined(OS_NAME_SLE) && !defined(OS_NAME_RHEL_7_4_5)
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
	_kcl_drm_fb_helper_set_suspend = amdkcl_fp_setup("drm_fb_helper_set_suspend",
					_kcl_drm_fb_helper_set_suspend_stub);
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
