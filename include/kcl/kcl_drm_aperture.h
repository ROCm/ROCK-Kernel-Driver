/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_APERTURE_H
#define KCL_KCL_DRM_APERTURE_H

#ifndef HAVE_DRM_APERTURE

#include <linux/types.h>

/* Copied from uapi/linux/pci_regs.h */
#ifndef PCI_STD_NUM_BARS
#define PCI_STD_NUM_BARS 6
#endif

/* Copied from drm/drm_aperture.h */
struct drm_device;
struct pci_dev;

int drm_aperture_remove_conflicting_framebuffers(resource_size_t base, resource_size_t size,
                                                 bool primary, const char *name);

int drm_aperture_remove_conflicting_pci_framebuffers(struct pci_dev *pdev, const char *name);

#endif /* HAVE_DRM_APERTURE */

#endif
