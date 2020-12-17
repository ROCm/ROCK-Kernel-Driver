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

/*
 * commit d3252ace0bc652a1a244455556b6a549f969bf99
 * PCI: Restore resized BAR state on resume
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
#define AMDKCL_ENABLE_RESIZE_FB_BAR
#endif

#if DRM_VERSION_CODE >= DRM_VERSION(4, 17, 0)
#define AMDKCL_AMDGPU_DMABUF_OPS
#endif

/*
 * commit v5.4-rc4-1120-gb3fac52c5193
 * drm: share address space for dma bufs
 */
#if DRM_VERSION_CODE < DRM_VERSION(5, 5, 0)
#define AMDKCL_DMA_BUF_SHARE_ADDR_SPACE
#endif

#endif/*AMDKCL_DRM_BACKPORT_H*/
