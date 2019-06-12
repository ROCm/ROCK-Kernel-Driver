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
