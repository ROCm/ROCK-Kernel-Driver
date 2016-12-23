#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0) && \
	!defined(OS_NAME_UBUNTU)
int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw)
{
	struct pci_dev *root;
	u32 lnkcap;

	*mlw = 0;
	if (!dev->pdev)
		return -EINVAL;

	root = dev->pdev->bus->self;
	if (!root)
		return -EINVAL;

	pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);

	*mlw = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	DRM_INFO("probing mlw for device %x:%x = %x\n", root->vendor, root->device, lnkcap);
	return 0;
}
EXPORT_SYMBOL(drm_pcie_get_max_link_width);
#endif

