/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_KCL_DRM_MODESET_LOCK_H_H_
#define _KCL_KCL_DRM_MODESET_LOCK_H_H_

#include <linux/types.h> /* stackdepot.h is not self-contained */
#include <drm/drm_modeset_lock.h>

#ifndef DRM_MODESET_ACQUIRE_INTERRUPTIBLE
#define DRM_MODESET_ACQUIRE_INTERRUPTIBLE BIT(0)
#endif

#endif
