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
#include <kcl/kcl_drm_print.h>
#include <stdarg.h>

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
