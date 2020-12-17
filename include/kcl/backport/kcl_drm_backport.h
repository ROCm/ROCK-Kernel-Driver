/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

#include <linux/ctype.h>
#include <drm/drm_fourcc.h>
#include <kcl/header/kcl_drm_file_h.h>

#if !defined(HAVE_DRM_GET_FORMAT_NAME_I_P)
/**
 * struct drm_format_name_buf - name of a DRM format
 * @str: string buffer containing the format name
 */
struct drm_format_name_buf {
	char str[32];
};

static char printable_char(int c)
{
	return isascii(c) && isprint(c) ? c : '?';
}

static inline const char *_kcl_drm_get_format_name(uint32_t format, struct drm_format_name_buf *buf)
{
	snprintf(buf->str, sizeof(buf->str),
		 "%c%c%c%c %s-endian (0x%08x)",
		 printable_char(format & 0xff),
		 printable_char((format >> 8) & 0xff),
		 printable_char((format >> 16) & 0xff),
		 printable_char((format >> 24) & 0x7f),
		 format & DRM_FORMAT_BIG_ENDIAN ? "big" : "little",
		 format);

	return buf->str;
}
#define drm_get_format_name _kcl_drm_get_format_name
#endif

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
