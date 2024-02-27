/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_BACKPORT_KCL_DRM_EXEC_H
#define AMDKCL_BACKPORT_KCL_DRM_EXEC_H

#include <drm/drm_exec.h>
#include <kcl/kcl_drm_exec.h>

#ifndef HAVE_DRM_EXEC_INIT_3_ARGUMENTS
static inline
void _kcl_drm_exec_init(struct drm_exec *exec, uint32_t flags, unsigned nr)
{
	return drm_exec_init(exec, flags);
}

#define drm_exec_init _kcl_drm_exec_init
#endif /* HAVE_DRM_EXEC_INIT_3_ARGUMENTS */

#endif
