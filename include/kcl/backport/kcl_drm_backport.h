/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

/*
 * commit v5.4-rc4-1120-gb3fac52c5193
 * drm: share address space for dma bufs
 */
#if DRM_VERSION_CODE < DRM_VERSION(5, 5, 0)
#define AMDKCL_DMA_BUF_SHARE_ADDR_SPACE
#endif

#endif/*AMDKCL_DRM_BACKPORT_H*/
