#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/malloc.h>

#include <asm/machdep.h>
#include <asm/gemini.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "pci.h"

#define pci_config_addr(bus,dev,offset) \
        (0x80000000 | (bus<<16) | (dev<<8) | offset)


int
gemini_pcibios_read_config_byte(unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned char *val)
{
	unsigned long reg;
	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	*val = ((reg >> ((offset & 0x3) << 3)) & 0xff);
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_read_config_word(unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned short *val)
{
	unsigned long reg;
	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	*val = ((reg >> ((offset & 0x3) << 3)) & 0xffff);
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_read_config_dword(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned int *val)
{
	*val = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_byte(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned char val)
{
	unsigned long reg;
	int shifts = offset & 0x3;

	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	reg = (reg & ~(0xff << (shifts << 3))) | (val << (shifts << 3));
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), reg );
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_word(unsigned char bus, unsigned char dev,
                                 unsigned char offset, unsigned short val)
{
	unsigned long reg;
	int shifts = offset & 0x3;

	reg = grackle_read( pci_config_addr( bus, dev, (offset & ~(0x3))));
	reg = (reg & ~(0xffff << (shifts << 3))) | (val << (shifts << 3));
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), reg );
	return PCIBIOS_SUCCESSFUL;
}

int
gemini_pcibios_write_config_dword(unsigned char bus, unsigned char dev,
                                  unsigned char offset, unsigned int val)
{
	grackle_write( pci_config_addr( bus, dev, (offset & ~(0x3))), val );
	return PCIBIOS_SUCCESSFUL;
}

void __init gemini_pcibios_fixup(void)
{
	int i;
	struct pci_dev *dev;
	
	pci_for_each_dev(dev) {
		for(i = 0; i < 6; i++) {
			if (dev->resource[i].flags & IORESOURCE_IO) {
				dev->resource[i].start |= (0xfe << 24);
				dev->resource[i].end |= (0xfe << 24);
			}
		}
	}
}

decl_config_access_method(gemini);

/* The "bootloader" for Synergy boards does none of this for us, so we need to
   lay it all out ourselves... --Dan */
void __init gemini_setup_pci_ptrs(void)
{
	set_config_access_method(gemini);
	ppc_md.pcibios_fixup = gemini_pcibios_fixup;
}
