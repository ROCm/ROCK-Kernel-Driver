/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <kcl/header/kcl_drmP_h.h>
#include <drm/drm_gem.h>
#include <kcl/header/kcl_drm_print_h.h>
#include <kcl/header/kcl_drm_file_h.h>
#if defined(HAVE_DRM_COLOR_LUT_SIZE)
#include <drm/drm_color_mgmt.h>
#endif

#if !defined(HAVE_DRM_DRM_PRINT_H)
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
       drm_err(fmt, ##__VA_ARGS__)
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

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv);
#endif

#if !defined(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED)
static inline void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	return drm_gem_object_unreference_unlocked(obj);
}

static inline void
drm_gem_object_get(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}
#endif

#ifndef HAVE_DRM_DEV_PUT
static inline void drm_dev_get(struct drm_device *dev)
{
	drm_dev_ref(dev);
}

static inline void drm_dev_put(struct drm_device *dev)
{
	return drm_dev_unref(dev);
}
#endif

#if !defined(HAVE_DRM_GET_MAX_IOMEM)
u64 drm_get_max_iomem(void);
#endif

#if !defined(HAVE_DRM_SEND_EVENT_LOCKED)
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
#endif

/**
 * drm_color_lut_size - calculate the number of entries in the LUT
 * @blob: blob containing the LUT
 *
 * Returns:
 * The number of entries in the color LUT stored in @blob.
 */
#if !defined(HAVE_DRM_COLOR_LUT_SIZE)
static inline int drm_color_lut_size(const struct drm_property_blob *blob)
{
	return blob->length / sizeof(struct drm_color_lut);
}
#endif

#endif /* AMDKCL_DRM_H */
