/* $Id: vaddrs.h,v 1.26 2000/08/01 04:53:58 anton Exp $ */
#ifndef _SPARC_VADDRS_H
#define _SPARC_VADDRS_H

#include <asm/head.h>

/*
 * asm-sparc/vaddrs.h:  Here we define the virtual addresses at
 *                      which important things will be mapped.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000 Anton Blanchard (anton@linuxcare.com)
 */

#define SRMMU_MAXMEM		0x0c000000

#define SRMMU_NOCACHE_VADDR	0xfc000000	/* KERNBASE + SRMMU_MAXMEM */
/* XXX Make this dynamic based on ram size - Anton */
#define SRMMU_NOCACHE_NPAGES	256
#define SRMMU_NOCACHE_SIZE	(SRMMU_NOCACHE_NPAGES * PAGE_SIZE)
#define SRMMU_NOCACHE_END	(SRMMU_NOCACHE_VADDR + SRMMU_NOCACHE_SIZE)

#define FIX_KMAP_BEGIN		0xfc100000
#define FIX_KMAP_END (FIX_KMAP_BEGIN + ((KM_TYPE_NR*NR_CPUS)-1)*PAGE_SIZE)

#define PKMAP_BASE		0xfc140000
#define PKMAP_BASE_END		(PKMAP_BASE+LAST_PKMAP*PAGE_SIZE)

#define SUN4M_IOBASE_VADDR	0xfd000000 /* Base for mapping pages */
#define IOBASE_VADDR		0xfe000000
#define IOBASE_END		0xfe300000

#define VMALLOC_START		0xfe300000
/* XXX Alter this when I get around to fixing sun4c - Anton */
#define VMALLOC_END		0xffc00000

/*
 * On the sun4/4c we need a place
 * to reliably map locked down kernel data.  This includes the
 * task_struct and kernel stack pages of each process plus the
 * scsi buffers during dvma IO transfers, also the floppy buffers
 * during pseudo dma which runs with traps off (no faults allowed).
 * Some quick calculations yield:
 *       NR_TASKS <512> * (3 * PAGE_SIZE) == 0x600000
 * Subtract this from 0xc00000 and you get 0x927C0 of vm left
 * over to map SCSI dvma + floppy pseudo-dma buffers.  So be
 * careful if you change NR_TASKS or else there won't be enough
 * room for it all.
 */
#define SUN4C_LOCK_VADDR	0xff000000
#define SUN4C_LOCK_END		0xffc00000

#define KADB_DEBUGGER_BEGVM	0xffc00000 /* Where kern debugger is in virt-mem */
#define KADB_DEBUGGER_ENDVM	0xffd00000
#define DEBUG_FIRSTVADDR	KADB_DEBUGGER_BEGVM
#define DEBUG_LASTVADDR		KADB_DEBUGGER_ENDVM

#define LINUX_OPPROM_BEGVM	0xffd00000
#define LINUX_OPPROM_ENDVM	0xfff00000

#define DVMA_VADDR		0xfff00000 /* Base area of the DVMA on suns */
#define DVMA_END		0xfffc0000

#endif /* !(_SPARC_VADDRS_H) */
