/*
 *
 *    Copyright (c) 2000 Grant Erickson <grant@borg.umn.edu>
 *    All rights reserved.
 *
 *    Module name: galaxy_pci.c
 *
 *    Description:
 *      PCI interface code for the IBM PowerPC 405GP on-chip PCI bus
 *      interface.
 *
 *      Why is this file called "galaxy_pci"? Because on the original
 *      IBM "Walnut" evaluation board schematic I have, the 405GP is
 *      is labeled "GALAXY".
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/machdep.h>

#include "pci.h"


/* Preprocessor Defines */

#define	PCICFGADDR	(volatile unsigned int *)(0xEEC00000)
#define	PCICFGDATA	(volatile unsigned int *)(0xEEC00004)


/* Function Prototypes */

decl_config_access_method(galaxy);


void __init
galaxy_pcibios_fixup(void)
{

}

void __init
galaxy_setup_pci_ptrs(void)
{
	set_config_access_method(galaxy);

	ppc_md.pcibios_fixup = galaxy_pcibios_fixup;
}

int
galaxy_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				unsigned char offset, unsigned char *val)
{

	return (PCIBIOS_SUCCESSFUL);
}

int
galaxy_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				unsigned char offset, unsigned short *val)
{

	return (PCIBIOS_SUCCESSFUL);
}

int
galaxy_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned int *val)
{

	return (PCIBIOS_SUCCESSFUL);
}

int
galaxy_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned char val)
{

	return (PCIBIOS_SUCCESSFUL);
}

int
galaxy_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned short val)
{

	return (PCIBIOS_SUCCESSFUL);
}

int
galaxy_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned int val)
{

	return (PCIBIOS_SUCCESSFUL);
}
