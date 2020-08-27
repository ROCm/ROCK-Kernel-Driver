/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if !defined(HAVE_DRM_DRM_PRINT_H)
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
#if !defined(HAVE_DRM_DRM_PRINT_H)
	pr_debug("%s %pV", p->prefix, vaf);
#else
	pr_debug("%s %pV", "no prefix < 4.11", vaf);
#endif
}
EXPORT_SYMBOL(__drm_printfn_debug);
#endif

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->master;
}
EXPORT_SYMBOL(drm_is_current_master);
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
