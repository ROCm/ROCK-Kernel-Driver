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

#if !defined(HAVE_DRM_DRM_PRINT_H)
/* Copied from include/drm/drm_print.h */
struct drm_printer {
	void (*printfn)(struct drm_printer *p, struct va_format *vaf);
	void *arg;
	const char *prefix;
};

void drm_printf(struct drm_printer *p, const char *f, ...);
#endif

/**
 * drm_debug_printer - construct a &drm_printer that outputs to pr_debug()
 * @prefix: debug output prefix
 *
 * RETURNS:
 * The &drm_printer object
 */
#if !defined(HAVE_DRM_DEBUG_PRINTER)
extern void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf);

static inline struct drm_printer drm_debug_printer(const char *prefix)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_debug,
#if !defined(HAVE_DRM_DRM_PRINT_H)
		.prefix = prefix
#endif
	};
	return p;
}
#endif

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

#ifndef DRM_NOTE
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_NOTE_ONCE
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_ERROR
#define DRM_ERROR(fmt, ...)                                            \
	drm_printk(KERN_ERR, DRM_UT_NONE, fmt,  ##__VA_ARGS__)
#endif

#if !defined(DRM_DEV_DEBUG)
#define DRM_DEV_DEBUG(dev, fmt, ...)					\
	DRM_DEBUG(fmt, ##__VA_ARGS__)
#endif

#if !defined(DRM_DEV_ERROR)
#define DRM_DEV_ERROR(dev, fmt, ...)					\
	DRM_ERROR(fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_DEBUG_VBL
#define DRM_UT_VBL		0x20
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif

#if !defined(HAVE_DRM_DEV_DBG)
void drm_dev_dbg(const struct device *dev, int category, const char *format, ...);
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
#endif
