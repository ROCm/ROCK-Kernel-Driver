/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

#include <linux/ctype.h>
#include <drm/drm_fourcc.h>
#include <kcl/kcl_drm.h>

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

#if !defined(HAVE_DRM_GEM_OBJECT_PUT_LOCKED)
#if defined(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED)
#define drm_gem_object_put _kcl_drm_gem_object_put
#endif
#endif

#endif/*AMDKCL_DRM_BACKPORT_H*/
