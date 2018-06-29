#include <kcl/kcl_pci.h>
#include <linux/version.h>

#if defined(BUILD_AS_DKMS)

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
#if defined(OS_NAME_RHEL_6) || defined(OS_NAME_RHEL_7_2)
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
