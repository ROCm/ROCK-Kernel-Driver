/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if !defined(HAVE_DRM_DRM_PRINT_H)
void drm_printf(struct drm_printer *p, const char *f, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, f);
	vaf.fmt = f;
	vaf.va = &args;
	p->printfn(p, &vaf);
	va_end(args);
}
EXPORT_SYMBOL(drm_printf);
#endif

#if !defined(HAVE_DRM_DEBUG_PRINTER)
void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf)
{
#if !defined(HAVE_DRM_DRM_PRINT_H)
	pr_debug("%s %pV", p->prefix, vaf);
#else
	pr_debug("%s %pV", "no prefix < 4.11", vaf);
#endif
}
EXPORT_SYMBOL(__drm_printfn_debug);
#endif
