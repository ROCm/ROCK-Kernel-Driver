/*
 * Support for indirect PCI bridges.
 *
 * Copyright (C) 1998 Gabriel Paubert.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <asm/io.h>
#include <asm/system.h>

unsigned int * pci_config_address;
unsigned char * pci_config_data;

int indirect_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	unsigned long flags;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	*val= in_8(pci_config_data + (offset&3));

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int indirect_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	unsigned long flags;
	
	if (offset&1) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	*val= in_le16((unsigned short *)(pci_config_data + (offset&3)));

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int indirect_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned int *val)
{
	unsigned long flags;
	
	if (offset&3) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	*val= in_le32((unsigned *)pci_config_data);

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int indirect_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char val)
{
	unsigned long flags;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	out_8(pci_config_data + (offset&3), val);

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int indirect_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short val)
{
	unsigned long flags;

	if (offset&1) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	out_le16((unsigned short *)(pci_config_data + (offset&3)), val);

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int indirect_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned int val)
{
	unsigned long flags;

	if (offset&3) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_flags(flags); cli();
	
	out_be32(pci_config_address, 
		 ((offset&0xfc)<<24) | (dev_fn<<16) | (bus<<8) | 0x80);

	out_le32((unsigned *)pci_config_data, val);

	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}
