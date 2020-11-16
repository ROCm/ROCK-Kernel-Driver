/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm.h>
#include <kcl/kcl_drm_print.h>
#include <stdarg.h>

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv)
{
	return fpriv->is_master && fpriv->master == fpriv->minor->master;
}
EXPORT_SYMBOL(drm_is_current_master);
#endif
