/*
 * Orion/Galileo specific header file.
 *  -- Cort <cort@fsmlabs.com>
 */
#ifndef __LINUX_MIPS_ORION_H
#define __LINUX_MIPS_ORION_H

/* base address for the GT-64120 internal registers */
#define GT64120_BASE (0x14000000)
/* GT64120 and PCI_0 interrupt cause register */
#define GT64120_CAUSE_LOW *(unsigned long *)(GT64120_BASE + 0xc18)
#define GT64120_CAUSE_HIGH *(unsigned long *)(GT64120_BASE + 0xc1c)

#endif /* __LINUX_MIPS_ORION_H */
