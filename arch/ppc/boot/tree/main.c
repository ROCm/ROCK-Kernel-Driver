/*
 * BK Id: SCCS/s.main.c 1.9 06/15/01 13:16:10 paulus
 */
/*
 *    Copyright (c) 1997 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Initial Power Macintosh COFF version.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      Modifications for an ELF-based IBM evaluation board version.
 *    Copyright 2000-2001 MontaVista Software Inc.
 *	PPC405GP modifications
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
 *    Module name: main.c
 *
 *    Description:
 *      This module does most of the real work for the boot loader. It
 *      checks the variables holding the absolute start address and size
 *      of the Linux kernel "image" and initial RAM disk "initrd" sections
 *      and if they are present, moves them to their "proper" locations.
 *
 *      For the Linux kernel, "proper" is physical address 0x00000000.
 *      For the RAM disk, "proper" is the image's size below the top
 *      of physical memory. The Linux kernel may be in either raw
 *      binary form or compressed with GNU zip (aka gzip).
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License 
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <asm/ppc4xx.h>

#include "nonstdio.h"
#include "irSect.h"
#if defined(CONFIG_SERIAL_CONSOLE)
#include "ns16550.h"
#endif /* CONFIG_SERIAL_CONSOLE */


/* Preprocessor Defines */

/*
 * Location of the IBM boot ROM function pointer address for retrieving
 * the board information structure.
 */

#define	BOARD_INFO_VECTOR	0xFFFE0B50

/* 
 * Warning: the board_info doesn't contain valid data until get_board_info() 
 * 	    gets called in start().
 */
#define RAM_SIZE        board_info.bi_memsize 

#define	RAM_PBASE	0x00000000
#define	RAM_PEND	(RAM_PBASE + RAM_SIZE)

#define	RAM_VBASE	0xC0000000
#define	RAM_VEND	(RAM_VBASE + RAM_SIZE)

#define	RAM_START	RAM_PBASE
#define	RAM_END		RAM_PEND
#define	RAM_FREE	(imageSect_start + imageSect_size + initrdSect_size)

#define	PROG_START	RAM_START


/* Function Macros */

#define	ALIGN_UP(x, align)	(((x) + ((align) - 1)) & ~((align) - 1))

/* Global Variables */

/* Needed by zalloc and zfree for allocating memory */

char *avail_ram;	/* Indicates start of RAM available for heap */
char *end_avail;	/* Indicates end of RAM available for heap */

/* Needed for serial I/O.
*/
extern unsigned long *com_port;

bd_t	board_info;

/*
** The bootrom may change bootrom_cmdline to point to a buffer in the
** bootrom.
*/
char *bootrom_cmdline = "";
char treeboot_bootrom_cmdline[512];

#ifdef CONFIG_CMDLINE
char *cmdline = CONFIG_CMDLINE;
#else
char *cmdline = "";
#endif

/* Function Prototypes */

extern void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp);

void
kick_watchdog(void)
{
#ifdef CONFIG_405GP
    mtspr(SPRN_TSR, (TSR_ENW | TSR_WIS));
#endif
}

void start(void)
{
    void *options;
    int ns, oh, i;
    unsigned long sa, len;
    void *dst;
    unsigned char *im;
    unsigned long initrd_start, initrd_size;
    bd_t *(*get_board_info)(void) = 
	    (bd_t *(*)(void))(*(unsigned long *)BOARD_INFO_VECTOR);
    bd_t *bip = NULL;


    com_port = (struct NS16550 *)serial_init(0);

#ifdef CONFIG_405GP
    /* turn off on-chip ethernet */
    /* This is to fix a problem with early walnut bootrom. */
 
    {
	/* Physical mapping of ethernet register space. */
	static struct   ppc405_enet_regs *ppc405_enet_regp =
	(struct ppc405_enet_regs *)PPC405_EM0_REG_ADDR;

	mtdcr(DCRN_MALCR, MALCR_MMSR);                /* 1st reset MAL */

	while (mfdcr(DCRN_MALCR) & MALCR_MMSR) {};    /* wait for the reset */
    
	ppc405_enet_regp->em0mr0 = 0x20000000;        /* then reset EMAC */
    }
#endif

    if ((bip = get_board_info()) != NULL)
	    memcpy(&board_info, bip, sizeof(bd_t));

    /* Init RAM disk (initrd) section */

    kick_watchdog();

    if (initrdSect_start != 0 && (initrd_size = initrdSect_size) != 0) {
        initrd_start = (RAM_END - initrd_size) & ~0xFFF;

	_printk("Initial RAM disk at 0x%08x (%u bytes)\n",
	       initrd_start, initrd_size);

	memcpy((char *)initrd_start,
	       (char *)(initrdSect_start),
	       initrdSect_size);

	end_avail = (char *)initrd_start;
    } else {
	initrd_start = initrd_size = 0;
	end_avail = (char *)RAM_END;
    }

    /* Linux kernel image section */
	
    kick_watchdog();

    im = (unsigned char *)(imageSect_start);
    len = imageSect_size;
    dst = (void *)PROG_START;

    /* Check for the gzip archive magic numbers */

    if (im[0] == 0x1f && im[1] == 0x8b) {

        /* The gunzip routine needs everything nice and aligned */

	void *cp = (void *)ALIGN_UP(RAM_FREE, 8);
	avail_ram = (void *)(cp + ALIGN_UP(len, 8));	/* used by zalloc() */
	memcpy(cp, im, len);

	/* I'm not sure what the 0x200000 parameter is for, but it works. */
	/* It tells gzip the end of the area you wish to reserve, and it
	 * can use data past that point....unfortunately, this value
	 * isn't big enough (luck ran out).  -- Dan
	 */

	gunzip(dst, 0x400000, cp, (int *)&len);
    } else {
	memmove(dst, im, len);
    }

    kick_watchdog();

    flush_cache(dst, len);

    sa = (unsigned long)dst;

    (*(void (*)())sa)(&board_info,
		      initrd_start,
		      initrd_start + initrd_size,
		      cmdline,
		      cmdline + strlen(cmdline));

    pause();
}
