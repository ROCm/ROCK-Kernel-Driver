/***********************************************************************
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * include/asm-mips/gt64120/momenco-ocelot/gt64120-dep.h
 *     Board-dependent definitions for GT-64120 chip.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 ***********************************************************************
 */

#ifndef _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H
#define _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H

#include <asm/addrspace.h>		/* for KSEG1ADDR() */
#include <asm/byteorder.h>		/* for cpu_to_le32() */

/*
 * PCI address allocation
 */
#define GT_PCI_MEM_BASE    (0x22000000)
#define GT_PCI_MEM_SIZE    GT_DEF_PCI0_MEM0_SIZE
#define GT_PCI_IO_BASE     (0x20000000)
#define GT_PCI_IO_SIZE     GT_DEF_PCI0_IO_SIZE

extern unsigned long gt64120_base;

#define GT64120_BASE       (gt64120_base)

/*
 * Because of an error/peculiarity in the Galileo chip, we need to swap the
 * bytes when running bigendian.
 */

#define GT_WRITE(ofs, data)  \
        *(volatile u32 *)(GT64120_BASE+ofs) = cpu_to_le32(data)
#define GT_READ(ofs, data)   \
        *data = le32_to_cpu(*(volatile u32 *)(GT64120_BASE+ofs))


/*
 * gt timer irq
 */
#define	TIMER		6

#endif  /* _ASM_GT64120_MOMENCO_OCELOT_GT64120_DEP_H */
