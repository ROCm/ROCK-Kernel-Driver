/*
 * BK Id: SCCS/s.bootinfo.h 1.7 05/23/01 00:38:42 cort
 */
/*
 * Non-machine dependent bootinfo structure.  Basic idea
 * borrowed from the m68k.
 *
 * Copyright (C) 1999 Cort Dougan <cort@ppc.kernel.org>
 */

#ifdef __KERNEL__
#ifndef _PPC_BOOTINFO_H
#define _PPC_BOOTINFO_H

#include <linux/config.h>

#if defined(CONFIG_APUS) && !defined(__BOOTER__)
#include <asm-m68k/bootinfo.h>
#else

struct bi_record {
    unsigned long tag;			/* tag ID */
    unsigned long size;			/* size of record (in bytes) */
    unsigned long data[0];		/* data */
};

#define BI_FIRST		0x1010  /* first record - marker */
#define BI_LAST			0x1011	/* last record - marker */
#define BI_CMD_LINE		0x1012
#define BI_BOOTLOADER_ID	0x1013
#define BI_INITRD		0x1014
#define BI_SYSMAP		0x1015
#define BI_MACHTYPE		0x1016

#endif /* CONFIG_APUS */

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the address
 * we're currently running at minus the address we were linked at.
 * (Note that strings count as static variables.)
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls should be done within prom_init(), and prom_init()
 * and all routines called within it must be careful to relocate
 * references as necessary.
 */
#define PTRRELOC(x)   ((typeof(x))((unsigned long)(x) + offset))
#define PTRUNRELOC(x) ((typeof(x))((unsigned long)(x) - offset))
#define RELOC(x)      (*PTRRELOC(&(x)))

#endif /* _PPC_BOOTINFO_H */
#endif /* __KERNEL__ */


