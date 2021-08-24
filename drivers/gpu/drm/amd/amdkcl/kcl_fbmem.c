/*
 *  linux/drivers/video/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <kcl/kcl_drm_fb.h>

/* Copied from drivers/video/fbdev/core/fbmem.c and modified for KCL */
#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS)

#ifdef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG
int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, const char *name)
{
        struct apertures_struct *ap;
        bool primary = false;
        int err, idx, bar;

        for (idx = 0, bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
                if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
                        continue;
                idx++;
        }

        ap = alloc_apertures(idx);
        if (!ap)
                return -ENOMEM;

        for (idx = 0, bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
                if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
                        continue;
                ap->ranges[idx].base = pci_resource_start(pdev, bar);
                ap->ranges[idx].size = pci_resource_len(pdev, bar);
                pci_dbg(pdev, "%s: bar %d: 0x%lx -> 0x%lx\n", __func__, bar,
                        (unsigned long)pci_resource_start(pdev, bar),
                        (unsigned long)pci_resource_end(pdev, bar));
                idx++;
        }

#ifdef CONFIG_X86
        primary = pdev->resource[PCI_ROM_RESOURCE].flags &
                                        IORESOURCE_ROM_SHADOW;
#endif
        err = remove_conflicting_framebuffers(ap, name, primary);
        kfree(ap);
        return err;
}
#else /* HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG */
int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id, const char *name)
{
	struct apertures_struct *ap;
	bool primary = false;
	int err = 0;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, res_id);
	ap->ranges[0].size = pci_resource_len(pdev, res_id);
#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags &
					IORESOURCE_ROM_SHADOW;
#endif
#ifdef HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT
	err = remove_conflicting_framebuffers(ap, name, primary);
#else
	remove_conflicting_framebuffers(ap, name, primary);
#endif
	kfree(ap);
	return err;
}
#endif /* HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG */

EXPORT_SYMBOL(remove_conflicting_pci_framebuffers);
#endif
