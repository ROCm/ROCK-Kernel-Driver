#include <kcl/kcl_pci.h>

#define PCI_EXP_DEVCAP2_ATOMIC_ROUTE	0x00000040 /* Atomic Op routing */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP32	0x00000080 /* 32b AtomicOp completion */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP64	0x00000100 /* Atomic 64-bit compare */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP128	0x00000200 /* 128b AtomicOp completion*/
#define PCI_EXP_DEVCTL2_ATOMIC_REQ	0x0040	/* Set Atomic requests */
#define PCI_EXP_DEVCTL2_ATOMIC_BLOCK	0x0040	/* Block AtomicOp on egress */

/**
 * pci_enable_atomic_ops_to_root - enable AtomicOp requests to root port
 * @dev: the PCI device
 *
 * Return 0 if the device is capable of generating AtomicOp requests,
 * all upstream bridges support AtomicOp routing, egress blocking is disabled
 * on all upstream ports, and the root port supports 32-bit, 64-bit and/or
 * 128-bit AtomicOp completion, or negative otherwise.
 */
int pci_enable_atomic_ops_to_root(struct pci_dev *dev)
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
			if (!(cap & (PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
				     PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
				     PCI_EXP_DEVCAP2_ATOMIC_COMP128)))
				return -EINVAL;
			break;
		}

		/*
		 * Upstream ports may block AtomicOps on egress.
		 */
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_UPSTREAM) {
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
