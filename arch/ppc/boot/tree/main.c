/*
 * BK Id: SCCS/s.main.c 1.7 05/18/01 06:20:29 patch
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

#define stringify(s)    tostring(s)
#define tostring(s)     #s

#define mtdcr(rn, v)    asm volatile("mtdcr " stringify(rn) ",%0" : : "r" (v))
#define mfdcr(rn)       ({unsigned int rval; \
                        asm volatile("mfdcr %0," stringify(rn) \
                                     : "=r" (rval)); rval;})
#define DCRN_MALCR      0x180   /* MAL Configuration                          */
#define   MALCR_SR      0x80000000      /* Software Reset                     */

/* Global Variables */

/* Needed by zalloc and zfree for allocating memory */

char *avail_ram;	/* Indicates start of RAM available for heap */
char *end_avail;	/* Indicates end of RAM available for heap */

bd_t	board_info;

/*
 * XXX - Until either the IBM boot ROM provides a way of passing arguments to
 *       the program it launches or until I/O is working in the boot loader,
 *       this is a good spot to pass in command line arguments to the kernel
 *       (e.g. console=tty0).
 */


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

extern void *zalloc(void *x, unsigned items, unsigned size);

/* serial I/O functions.
 * These should have generic names, although this is similar to 16550....
 */
static volatile unsigned char *uart0_lsr = (unsigned char *)0xef600305;
static volatile unsigned char *uart0_xcvr = (unsigned char *)0xef600300;

void
serial_putc(void *unused, unsigned char c)
{
 while ((*uart0_lsr & LSR_THRE) == 0);
 *uart0_xcvr = c;
}

unsigned char
serial_getc(void *unused)
{
 while ((*uart0_lsr & LSR_DR) == 0);
 return (*uart0_xcvr);
}

int
serial_tstc(void *unused)
{
 return ((*uart0_lsr & LSR_DR) != 0);
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
    volatile unsigned long *em0mr0 = (long *)0xEF600800;    /* ftr fixup */



#if defined(CONFIG_WALNUT)
    /* turn off ethernet */
    /* This is to fix a problem with early walnut bootrom. */

    mtdcr(DCRN_MALCR, MALCR_SR);                /* 1st reset MAL */

    while (mfdcr(DCRN_MALCR) & MALCR_SR) {};    /* wait for the reset */

    *em0mr0 = 0x20000000;                       /* then reset EMAC */
#endif


#if 0
    /* ftr revisit - remove printf()s */

    printf("\n\nbootrom_cmdline = >%s<\n\n", bootrom_cmdline);
    if (*bootrom_cmdline != '\0') {
	printf("bootrom_cmdline != NULL, copying it into cmdline\n\n");
	*treeboot_bootrom_cmdline = '\0';
	strcat(treeboot_bootrom_cmdline, bootrom_cmdline);
	cmdline = treeboot_bootrom_cmdline;
    }
#endif


    if ((bip = get_board_info()) != NULL)
	    memcpy(&board_info, bip, sizeof(bd_t));

    /* Init RAM disk (initrd) section */

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


    flush_cache(dst, len);

    sa = (unsigned long)dst;

    (*(void (*)())sa)(&board_info,
		      initrd_start,
		      initrd_start + initrd_size,
		      cmdline,
		      cmdline + strlen(cmdline));

    pause();
}
