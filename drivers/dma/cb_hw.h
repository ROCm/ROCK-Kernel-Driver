#ifndef _CB_HW_H_
#define _CB_HW_H_

/* PCI Configuration Space Values */
#define CB_PCI_VID					0x8086
#define CB_PCI_DID					0x1A38
#define CB_PCI_RID					0x00
#define CB_PCI_SVID					0x8086
#define CB_PCI_SID					0x8086

/* MSI Capability */
#define CB_PCI_MSICAPID					0x05

/* PCI Express Capability */
#define CB_PCI_EXPCAPID					0x10

/* Power Management Capability */
#define CB_PCI_PMCAPID					0x01

#define CB_VER						0x12	/* Version 1.2 */

struct cb_pci_pmcap_register {
	uint32_t	capid:8;	/* RO: 01h */
	uint32_t	nxtcapptr:8;
	uint32_t	version:3;	/* RO: 010b */
	uint32_t	pmeclk:1;	/* RO: 0b */
	uint32_t	reserved:1;	/* RV: 0b */
	uint32_t	dsi:1;		/* RO: 0b */
	uint32_t	aux_current:3;	/* RO: 000b */
	uint32_t	d1_support:1;	/* RO: 0b */
	uint32_t	d2_support:1;	/* RO: 0b */
	uint32_t	pme_support:5;	/* RO: 11001b */
};

struct cb_pci_pmcsr_register {
	uint32_t	power_state:2;
	uint32_t	reserved1:6;
	uint32_t	pme_enable:1;
	uint32_t	data_select:4;
	uint32_t	data_scale:2;
	uint32_t	pme_status:1;
	uint32_t	reserved2:6;
	uint32_t	b2_b3_support:1;
	uint32_t	bus_pwr_clk_enable:1;
	uint32_t	data:8;
};

struct cb_dma_descriptor {
	uint32_t	size;
	uint32_t	ctl;
	uint64_t	src_addr;
	uint64_t	dst_addr;
	uint64_t	next;
	uint64_t	rsv1;
	uint64_t	rsv2;
	uint64_t	user1;
	uint64_t	user2;
};

#define CB_DMA_DESCRIPTOR_CTL_INT_GN		0x00000001
#define CB_DMA_DESCRIPTOR_CTL_SRC_SN		0x00000002
#define CB_DMA_DESCRIPTOR_CTL_DST_SN		0x00000004
#define CB_DMA_DESCRIPTOR_CTL_CP_STS		0x00000008
#define CB_DMA_DESCRIPTOR_CTL_FRAME		0x00000010
#define CB_DMA_DESCRIPTOR_NUL			0x00000020
#define CB_DMA_DESCRIPTOR_OPCODE		0xFF000000

#endif
