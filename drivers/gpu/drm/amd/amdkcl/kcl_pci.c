#include <kcl/kcl_pci.h>
#include <linux/version.h>

#if defined(BUILD_AS_DKMS)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) && \
	!defined(OS_NAME_RHEL_7_6)
/**
 * pci_enable_atomic_ops_to_root - enable AtomicOp requests to root port
 * @dev: the PCI device
 * @comp_caps: Caps required for atomic request completion
 *
 * Return 0 if all upstream bridges support AtomicOp routing, egress
 * blocking is disabled on all upstream ports, and the root port
 * supports the requested completion capabilities (32-bit, 64-bit
 * and/or 128-bit AtomicOp completion), or negative otherwise.
 *
 */
int pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 comp_caps)
{
	struct pci_bus *bus = dev->bus;

	if (!pci_is_pcie(dev))
		return -EINVAL;

	switch (pci_pcie_type(dev)) {
	/*
	 * PCIe 3.0, 6.15 specifies that endpoints and root ports are permitted
	 * to implement AtomicOp requester capabilities.
	 */
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_LEG_END:
	case PCI_EXP_TYPE_RC_END:
		break;
	default:
		return -EINVAL;
	}

	while (bus->parent) {
		struct pci_dev *bridge = bus->self;
		u32 cap;

		pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap);

		switch (pci_pcie_type(bridge)) {
		/*
		 * Upstream, downstream and root ports may implement AtomicOp
		 * routing capabilities. AtomicOp routing via a root port is
		 * not considered.
		 */
		case PCI_EXP_TYPE_UPSTREAM:
		case PCI_EXP_TYPE_DOWNSTREAM:
			if (!(cap & PCI_EXP_DEVCAP2_ATOMIC_ROUTE))
				return -EINVAL;
			break;

		/*
		 * Root ports are permitted to implement AtomicOp completion
		 * capabilities.
		 */
		case PCI_EXP_TYPE_ROOT_PORT:
			if ((cap & comp_caps) != comp_caps)
				return -EINVAL;
			break;
		}

		/*
		 * Upstream ports may block AtomicOps on egress.
		 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 2, 0) || defined(OS_NAME_RHEL_6)
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_DOWNSTREAM) {
#else
		if (!bridge->has_secondary_link) {
#endif
			u32 ctl2;

			pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2,
						   &ctl2);
			if (ctl2 & PCI_EXP_DEVCTL2_ATOMIC_BLOCK)
				return -EINVAL;
		}

		bus = bus->parent;
	}

	pcie_capability_set_word(dev, PCI_EXP_DEVCTL2,
				 PCI_EXP_DEVCTL2_ATOMIC_REQ);

	return 0;
}
EXPORT_SYMBOL(pci_enable_atomic_ops_to_root);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
/*
 * pcie_get_speed_cap - query for the PCI device's link speed capability
 * @dev: PCI device to query
 *
 * Query the PCI device speed capability.  Return the maximum link speed
 * supported by the device.
 */
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev)
{
	u32 lnkcap2, lnkcap;

	/*
	 * PCIe r4.0 sec 7.5.3.18 recommends using the Supported Link
	 * Speeds Vector in Link Capabilities 2 when supported, falling
	 * back to Max Link Speed in Link Capabilities otherwise.
	 */
	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP2, &lnkcap2);
	if (lnkcap2) { /* PCIe r3.0-compliant */
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
		return PCI_SPEED_UNKNOWN;
	}

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap) {
		if (lnkcap & PCI_EXP_LNKCAP_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
	}

	return PCI_SPEED_UNKNOWN;
}
EXPORT_SYMBOL(pcie_get_speed_cap);

/**
 * pcie_get_width_cap - query for the PCI device's link width capability
 * @dev: PCI device to query
 *
 * Query the PCI device width capability.  Return the maximum link width
 * supported by the device.
 */
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev)
{
	u32 lnkcap;

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap)
		return (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	return PCIE_LNK_WIDTH_UNKNOWN;
}
EXPORT_SYMBOL(pcie_get_width_cap);

#endif

void _kcl_pci_configure_extended_tags(struct pci_dev *dev)
{
	u32 dev_cap;
	int ret;

	if (!pci_is_pcie(dev))
		return;

	ret = pcie_capability_read_dword(dev, PCI_EXP_DEVCAP, &dev_cap);
	if (ret)
		return;

	if (dev_cap & PCI_EXP_DEVCAP_EXT_TAG)
		pcie_capability_set_word(dev, PCI_EXP_DEVCTL,
					 PCI_EXP_DEVCTL_EXT_TAG);
}
EXPORT_SYMBOL(_kcl_pci_configure_extended_tags);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
ssize_t max_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%s\n", PCIE_SPEED2STR(pcie_get_speed_cap(pdev)));
}
static DEVICE_ATTR_RO(max_link_speed);

ssize_t max_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", pcie_get_width_cap(pdev));
}
static DEVICE_ATTR_RO(max_link_width);

ssize_t current_link_speed_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;
	const char *speed;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	switch (linkstat & PCI_EXP_LNKSTA_CLS) {
	case PCI_EXP_LNKSTA_CLS_16_0GB:
		speed = "16 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		speed = "8 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		speed = "5 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		speed = "2.5 GT/s";
		break;
	default:
		speed = "Unknown speed";
	}

	return sprintf(buf, "%s\n", speed);
}
static DEVICE_ATTR_RO(current_link_speed);

ssize_t current_link_width_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n",
		(linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT);
}
static DEVICE_ATTR_RO(current_link_width);

static struct attribute *pcie_dev_attrs[] = {
	&dev_attr_current_link_speed.attr,
	&dev_attr_current_link_width.attr,
	&dev_attr_max_link_width.attr,
	&dev_attr_max_link_speed.attr,
	NULL,
};

static const struct attribute_group pcie_dev_group = {
	.attrs = pcie_dev_attrs,
};

const struct attribute_group *pcie_dev_groups[] = {
	&pcie_dev_group,
	NULL,
};

int _kcl_pci_create_measure_file(struct pci_dev *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_current_link_speed);
	if (ret != 0) {
		dev_err(&pdev->dev,
				"Failed to create current_link_speed sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_current_link_width);
	if (ret != 0) {
		dev_err(&pdev->dev,
				"Failed to create current_link_width sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_max_link_width);
	if (ret != 0) {
		dev_err(&pdev->dev,
				"Failed to create max_link_width sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_max_link_speed);
	if (ret != 0) {
		dev_err(&pdev->dev,
				"Failed to create max_link_speed sysfs files: %d\n", ret);
		return ret;
	}

}
EXPORT_SYMBOL(_kcl_pci_create_measure_file);
#endif

#endif
