/*
 * Copyright (C) 2016 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 */
#ifndef AMDKCL_DRM_PRINT_H
#define AMDKCL_DRM_PRINT_H

#include <drm/drm_print.h>
#include <drm/drm_drv.h>
#include <drm/drm_device.h>

#ifndef _DRM_PRINTK
#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)
#endif

#ifndef DRM_WARN
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_WARN_ONCE
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef drm_WARN_ONCE
#define drm_WARN_ONCE(drm, condition, format, arg...)			\
	WARN_ONCE(condition, "%s %s: " format,				\
			dev_driver_string((drm)->dev),			\
			dev_name((drm)->dev), ## arg)
#endif

#ifndef DRM_NOTE
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_NOTE_ONCE
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef drm_err
#define drm_err(drm, fmt, ...)           \
	dev_err((drm)->dev, "[drm] " fmt, ##__VA_ARGS__)

__printf(1, 2)
void kcl_drm_err(const char *format, ...);

#undef DRM_ERROR
#define DRM_ERROR(fmt, ...)              \
	kcl_drm_err(fmt, ##__VA_ARGS__)

#else
#define HAVE_DRM_ERR_MACRO
#endif /* drm_err */

#ifndef drm_warn
#define drm_warn(drm, fmt, ...)		\
	dev_warn((drm)->dev, "[drm] " fmt, ##__VA_ARGS__)
#endif /* drm_warn */

#ifndef drm_warn_once
#define drm_warn_once(drm, fmt, ...) \
	dev_warn_once((drm)->dev, "[drm] " fmt, ##__VA_ARGS__)
#endif /* drm_warn_once */

#if !defined(DRM_UT_VBL)
#define DRM_UT_VBL		0x20
#endif

#if !defined(DRM_DEV_DEBUG)
#define DRM_DEV_DEBUG(dev, fmt, ...)					\
	DRM_DEBUG(fmt, ##__VA_ARGS__)
#endif

#if !defined(DRM_DEV_ERROR)
#define DRM_DEV_ERROR(dev, fmt, ...)					\
	DRM_ERROR(fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_DEV_INFO
/* NOTE: this is deprecated in favor of drm_info() or dev_info(). */
#define DRM_DEV_INFO(dev, fmt, ...)				\
	DRM_INFO(fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_DEBUG_VBL
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif

#if !defined(drm_dbg_atomic)
#define drm_dbg_atomic(drm, fmt, ...)                                   \
        drm_dev_dbg((drm)->dev, DRM_UT_ATOMIC, fmt, ##__VA_ARGS__)
#endif

#if !defined(drm_dbg_state)
#define drm_dbg_state(drm, fmt, ...)					\
	drm_dev_dbg((drm) ? (drm)->dev : NULL, DRM_UT_STATE, fmt, ##__VA_ARGS__)
#endif

#if !defined(drm_dbg_vbl)
#define drm_dbg_vbl(drm, fmt, ...)					\
	drm_dev_dbg((drm) ? (drm)->dev : NULL, DRM_UT_VBL, fmt, ##__VA_ARGS__)
#endif

#if !defined(drm_dbg_kms)
#define drm_dbg_kms(drm, fmt, ...)				\
	drm_dev_dbg((drm)->dev, 0x04, fmt, ##__VA_ARGS__)
#endif

#ifndef HAVE_DRM_DEBUG_ENABLED
/* Copied from v5.3-rc1-708-gf0a8f533adc2 include/drm/drm_print.h */
static  inline bool drm_debug_enabled(unsigned int category)
{
	return unlikely(drm_debug & category);
}
#endif /* HAVE_DRM_DEBUG_ENABLED */

/* Copied from v4.14-rc3-610-gbf6234a294c5 include/drm/drm_print.h */
#ifndef drm_printf_indent
#define drm_printf_indent(printer, indent, fmt, ...) \
	drm_printf((printer), "%.*s" fmt, (indent), "\t\t\t\t\tX", ##__VA_ARGS__)
#endif

#ifndef HAVE_DRM_PRINT_BITS
void drm_print_bits(struct drm_printer *p, unsigned long value,
		    const char * const bits[], unsigned int nbits);
#endif

#endif
