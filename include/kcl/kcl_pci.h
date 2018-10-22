#ifndef AMDKCL_PCI_H
#define AMDKCL_PCI_H

#include <linux/pci.h>
#include <linux/version.h>

#ifdef BUILD_AS_DKMS
#define PCI_EXP_DEVCAP2_ATOMIC_ROUTE	0x00000040 /* Atomic Op routing */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP32	0x00000080 /* 32b AtomicOp completion */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP64	0x00000100 /* 64b AtomicOp completion*/
#define PCI_EXP_DEVCAP2_ATOMIC_COMP128	0x00000200 /* 128b AtomicOp completion*/
#define PCI_EXP_DEVCTL2_ATOMIC_REQ	0x0040	/* Set Atomic requests */
#define PCI_EXP_DEVCTL2_ATOMIC_BLOCK	0x0040	/* Block AtomicOp on egress */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) && \
	!defined(OS_NAME_RHEL_7_6)
int pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 comp_caps);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev);
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev);
u32 pcie_bandwidth_capable(struct pci_dev *dev, enum pci_bus_speed *speed,
		enum pcie_link_width *width);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#define PCIE_SPEED_16_0GT 0x17
#define  PCI_EXP_LNKCAP2_SLS_16_0GB 0x00000010 /* Supported Speed 16GT/s */
#define  PCI_EXP_LNKCAP_SLS_16_0GB 0x00000004 /* LNKCAP2 SLS Vector bit 3 */
#define  PCI_EXP_LNKSTA_CLS_16_0GB 0x0004 /* Current Link Speed 16.0GT/s */
/* PCIe link information */
#define PCIE_SPEED2STR(speed) \
	((speed) == PCIE_SPEED_16_0GT ? "16 GT/s" : \
	(speed) == PCIE_SPEED_8_0GT ? "8 GT/s" : \
	(speed) == PCIE_SPEED_5_0GT ? "5 GT/s" : \
	(speed) == PCIE_SPEED_2_5GT ? "2.5 GT/s" : \
	"Unknown speed")

/* PCIe speed to Mb/s reduced by encoding overhead */
#define PCIE_SPEED2MBS_ENC(speed) \
	((speed) == PCIE_SPEED_16_0GT ? 16000*128/130 : \
	(speed) == PCIE_SPEED_8_0GT  ?  8000*128/130 : \
	(speed) == PCIE_SPEED_5_0GT  ?  5000*8/10 : \
	(speed) == PCIE_SPEED_2_5GT  ?  2500*8/10 : \
	0)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
#define  PCI_EXP_LNKCAP_SLS_8_0GB 0x00000003 /* LNKCAP2 SLS Vector bit 2 */
ssize_t max_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
ssize_t max_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
ssize_t current_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
ssize_t current_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
ssize_t secondary_bus_number_show(struct device *dev,
				    struct device_attribute *attr, char *buf);
ssize_t subordinate_bus_number_show(struct device *dev,
				    struct device_attribute *attr, char *buf);
int  _kcl_pci_create_measure_file(struct pci_dev *pdev);
#endif

void _kcl_pci_configure_extended_tags(struct pci_dev *dev);

#endif

static inline void kcl_pci_configure_extended_tags(struct pci_dev *dev)
{
#if defined(BUILD_AS_DKMS) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	_kcl_pci_configure_extended_tags(dev);
#endif
}

static inline int kcl_pci_create_measure_file(struct pci_dev *pdev)
{
#if defined(BUILD_AS_DKMS) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0))
	_kcl_pci_create_measure_file(pdev);
#else
	return 0;
#endif
}

#endif /* AMDKCL_PCI_H */
