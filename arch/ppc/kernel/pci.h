
#ifndef __PPC_KERNEL_PCI_H__
#define __PPC_KERNEL_PCI_H__

extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;

extern unsigned int  *pci_config_address;
extern unsigned char *pci_config_data;

void fix_intr(struct device_node *node, struct pci_dev *dev);

#if 0
#define decl_config_access_method(name) 	\
struct pci_ops name##_pci_ops = { 		\
	name##_pcibios_read_config_byte,	\
	name##_pcibios_read_config_word,	\
	name##_pcibios_read_config_dword,	\
	name##_pcibios_write_config_byte,	\
	name##_pcibios_write_config_word,	\
	name##_pcibios_write_config_dword	\
}
#endif

#define decl_config_access_method(name) \
extern int name##_pcibios_read_config_byte(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned char *val); \
extern int name##_pcibios_read_config_word(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned short *val); \
extern int name##_pcibios_read_config_dword(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned int *val); \
extern int name##_pcibios_write_config_byte(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned char val); \
extern int name##_pcibios_write_config_word(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned short val); \
extern int name##_pcibios_write_config_dword(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned int val)

#define set_config_access_method(name) \
	ppc_md.pcibios_read_config_byte = name##_pcibios_read_config_byte; \
	ppc_md.pcibios_read_config_word = name##_pcibios_read_config_word; \
	ppc_md.pcibios_read_config_dword = name##_pcibios_read_config_dword; \
	ppc_md.pcibios_write_config_byte = name##_pcibios_write_config_byte; \
	ppc_md.pcibios_write_config_word = name##_pcibios_write_config_word; \
	ppc_md.pcibios_write_config_dword = name##_pcibios_write_config_dword

#endif /* __PPC_KERNEL_PCI_H__ */
