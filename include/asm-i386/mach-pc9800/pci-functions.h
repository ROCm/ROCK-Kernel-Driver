/*
 *	PCI BIOS function codes for the PC9800. Different to
 *	standard PC systems
 */

/* Note: PC-9800 confirms PCI 2.1 on only few models */

#define PCIBIOS_PCI_FUNCTION_ID 	0xccXX
#define PCIBIOS_PCI_BIOS_PRESENT 	0xcc81
#define PCIBIOS_FIND_PCI_DEVICE		0xcc82
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xcc83
/*      PCIBIOS_GENERATE_SPECIAL_CYCLE	0xcc86	(not supported by bios) */
#define PCIBIOS_READ_CONFIG_BYTE	0xcc88
#define PCIBIOS_READ_CONFIG_WORD	0xcc89
#define PCIBIOS_READ_CONFIG_DWORD	0xcc8a
#define PCIBIOS_WRITE_CONFIG_BYTE	0xcc8b
#define PCIBIOS_WRITE_CONFIG_WORD	0xcc8c
#define PCIBIOS_WRITE_CONFIG_DWORD	0xcc8d
#define PCIBIOS_GET_ROUTING_OPTIONS	0xcc8e	/* PCI 2.1 only */
#define PCIBIOS_SET_PCI_HW_INT		0xcc8f	/* PCI 2.1 only */
