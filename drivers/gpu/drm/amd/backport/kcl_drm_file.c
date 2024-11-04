/*
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <drm/drm_file.h>
#include <linux/file.h>
#include <drm/drm_print.h>
#include <kcl/kcl_drm_file.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <linux/pci.h>
#include "amdgpu_fdinfo.h"
#ifndef HAVE_DRM_SHOW_FDINFO
/**
 * drm_show_fdinfo - helper for drm file fops
 * @m: output stream
 * @f: the device file instance
 *
 * Helper to implement fdinfo, for userspace to query usage stats, etc, of a
 * process using the GPU.  See also &drm_driver.show_fdinfo.
 *
 * For text output format description please see Documentation/gpu/drm-usage-stats.rst
 */
void drm_show_fdinfo(struct seq_file *m, struct file *f)
{
        struct drm_file *file = f->private_data;
        struct drm_device *dev = file->minor->dev;
        struct drm_printer p = drm_seq_file_printer(m);

        drm_printf(&p, "drm-driver:\t%s\n", dev->driver->name);

        if (dev_is_pci(dev->dev)) {
                struct pci_dev *pdev = to_pci_dev(dev->dev);

                drm_printf(&p, "drm-pdev:\t%04x:%02x:%02x.%d\n",
                           pci_domain_nr(pdev->bus), pdev->bus->number,
                           PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
        }

	amdgpu_show_fdinfo(&p, file);
}

#ifndef HAVE_DRM_PRINT_MEMORY_STATS
static void print_size(struct drm_printer *p, const char *stat,
		       const char *region, u64 sz)
{
	const char *units[] = {"", " KiB", " MiB"};
	unsigned u;

	for (u = 0; u < ARRAY_SIZE(units) - 1; u++) {
		if (sz == 0 || !IS_ALIGNED(sz, SZ_1K))
			break;
		sz = div_u64(sz, SZ_1K);
	}

	drm_printf(p, "drm-%s-%s:\t%llu%s\n", stat, region, sz, units[u]);
}

void _kcl_drm_print_memory_stats(struct drm_printer *p,
			    const struct drm_memory_stats *stats,
			    enum drm_gem_object_status supported_status,
			    const char *region)
{
	print_size(p, "total", region, stats->private + stats->shared);
	print_size(p, "shared", region, stats->shared);
	print_size(p, "active", region, stats->active);

	if (supported_status & DRM_GEM_OBJECT_RESIDENT)
		print_size(p, "resident", region, stats->resident);

	if (supported_status & DRM_GEM_OBJECT_PURGEABLE)
		print_size(p, "purgeable", region, stats->purgeable);
}
EXPORT_SYMBOL(_kcl_drm_print_memory_stats);
#endif

#endif
