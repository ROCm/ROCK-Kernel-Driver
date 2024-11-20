/* SPDX-License-Identifier: MIT */

#ifndef _KCL_BACKPORT_KCL_LINUX_APERTURE_H_
#define _KCL_BACKPORT_KCL_LINUX_APERTURE_H_ 

#include <linux/types.h>

struct pci_dev;

#ifndef HAVE_APERTURE_REMOVE_CONFLICTING_PCI_DEVICES
#include <drm/drm_aperture.h>
static inline int _kcl_aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name)
{
#ifdef HAVE_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG
	char *nonconst_name = (char *)name;
        struct drm_driver *drm_driver = container_of(&nonconst_name, struct drm_driver, name);
        return drm_aperture_remove_conflicting_pci_framebuffers(pdev, drm_driver);
#else
        return drm_aperture_remove_conflicting_pci_framebuffers(pdev, name);
#endif
}
#define aperture_remove_conflicting_pci_devices _kcl_aperture_remove_conflicting_pci_devices
#endif

#endif /* _KCL_BACKPORT_KCL_LINUX_APERTURE_H_ */
