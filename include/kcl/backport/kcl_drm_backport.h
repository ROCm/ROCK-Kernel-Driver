/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

/*
 * commit v4.10-rc3-539-g086f2e5cde74
 * drm: debugfs: Remove all files automatically on cleanup
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 11, 0)
#define AMDKCL_AMDGPU_DEBUGFS_CLEANUP
#endif

#if DRM_VERSION_CODE >= DRM_VERSION(4, 17, 0)
#define AMDKCL_AMDGPU_DMABUF_OPS
#endif

#endif/*AMDKCL_DRM_BACKPORT_H*/
