/*
 * linux/include/asm-arm/arch-iop3xx/hardware.h
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>

/*
 * Note about PCI IO space mappings
 *
 * To make IO space accesses efficient, we store virtual addresses in
 * the IO resources.
 *
 * The PCI IO space is located at virtual 0xfe000000 from physical
 * 0x90000000.  The PCI BARs must be programmed with physical addresses,
 * but when we read them, we convert them to virtual addresses.  See
 * arch/arm/mach-iop3xx/iop3XX-pci.c
 */

#define pcibios_assign_all_busses() 1

#ifdef CONFIG_ARCH_IOP321

#define PCIBIOS_MIN_IO		0x90000000
#define PCIBIOS_MIN_MEM		0x80000000

#include "iop321.h"

#ifdef CONFIG_ARCH_IQ80321
#include "iq80321.h"
#endif
#endif




#endif  /* _ASM_ARCH_HARDWARE_H */
