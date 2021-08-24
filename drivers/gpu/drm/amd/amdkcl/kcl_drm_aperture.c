// SPDX-License-Identifier: MIT

#ifndef HAVE_DRM_APERTURE
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <kcl/kcl_drm_aperture.h>

struct drm_aperture {
        struct drm_device *dev;
        resource_size_t base;
        resource_size_t size;
        struct list_head lh;
        void (*detach)(struct drm_device *dev);
};

static LIST_HEAD(drm_apertures);
static DEFINE_MUTEX(drm_apertures_lock);

static bool overlap(resource_size_t base1, resource_size_t end1,
                    resource_size_t base2, resource_size_t end2)
{
        return (base1 < end2) && (end1 > base2);
}


static void drm_aperture_detach_drivers(resource_size_t base, resource_size_t size)
{
        resource_size_t end = base + size;
        struct list_head *pos, *n;

        mutex_lock(&drm_apertures_lock);

        list_for_each_safe(pos, n, &drm_apertures) {
                struct drm_aperture *ap =
                        container_of(pos, struct drm_aperture, lh);
                struct drm_device *dev = ap->dev;

                if (WARN_ON_ONCE(!dev))
                        continue;

                if (!overlap(base, end, ap->base, ap->base + ap->size))
                        continue;

                ap->dev = NULL; /* detach from device */
                list_del(&ap->lh);

                ap->detach(dev);
        }

        mutex_unlock(&drm_apertures_lock);
}


/**
 * drm_aperture_remove_conflicting_framebuffers - remove existing framebuffers in the given range
 * @base: the aperture's base address in physical memory
 * @size: aperture size in bytes
 * @primary: also kick vga16fb if present
 * @name: requesting driver name
 *
 * This function removes graphics device drivers which use memory range described by
 * @base and @size.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_framebuffers(resource_size_t base, resource_size_t size,
                                                 bool primary, const char *name)
{
#if IS_REACHABLE(CONFIG_FB)
        struct apertures_struct *a;
        int ret;

        a = alloc_apertures(1);
        if (!a)
                return -ENOMEM;

        a->ranges[0].base = base;
        a->ranges[0].size = size;

        ret = remove_conflicting_framebuffers(a, name, primary);
        kfree(a);

        if (ret)
                return ret;
#endif

        drm_aperture_detach_drivers(base, size);

        return 0;
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_framebuffers);

/**
 * drm_aperture_remove_conflicting_pci_framebuffers - remove existing framebuffers for PCI devices
 * @pdev: PCI device
 * @name: requesting driver name
 *
 * This function removes graphics device drivers using memory range configured
 * for any of @pdev's memory bars. The function assumes that PCI device with
 * shadowed ROM drives a primary display and so kicks out vga16fb.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_pci_framebuffers(struct pci_dev *pdev, const char *name)
{
        resource_size_t base, size;
        int bar, ret = 0;

        for (bar = 0; bar < PCI_STD_NUM_BARS; ++bar) {
                if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
                        continue;
                base = pci_resource_start(pdev, bar);
                size = pci_resource_len(pdev, bar);
                drm_aperture_detach_drivers(base, size);
        }

        /*
         * WARNING: Apparently we must kick fbdev drivers before vgacon,
         * otherwise the vga fbdev driver falls over.
         */

#ifdef HAVE_VGA_REMOVE_VGACON
#if IS_REACHABLE(CONFIG_FB)

#ifdef HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG
        ret = remove_conflicting_pci_framebuffers(pdev, name);
#else
        ret = remove_conflicting_pci_framebuffers(pdev, 0, name);
#endif

#endif
        if (ret == 0)
                ret = vga_remove_vgacon(pdev);
#endif

        return ret;
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_pci_framebuffers);

#endif /* HAVE_DRM_APERTURE */
