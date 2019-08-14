/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <kcl/header/kcl_drm_print_h.h>

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

#endif /* AMDKCL_DRM_H */
