/*
 *    Copyright (c) 1997 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Initial Power Macintosh COFF version.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      Modifications for an ELF-based IBM evaluation board version.
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

#include <asm/board.h>

#include "../coffboot/nonstdio.h"
#include "../coffboot/zlib.h"
#include "irSect.h"


/* Preprocessor Defines */

/*
 * Location of the IBM boot ROM function pointer address for retrieving
 * the board information structure.
 */

#define	BOARD_INFO_VECTOR	0xFFFE0B50

#define	RAM_SIZE	(4 * 1024 * 1024)

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

bd_t	board_info;

/*
 * XXX - Until either the IBM boot ROM provides a way of passing arguments to
 *       the program it launches or until I/O is working in the boot loader,
 *       this is a good spot to pass in command line arguments to the kernel
 *       (e.g. console=tty0).
 */

static char *cmdline = "";


/* Function Prototypes */

void *zalloc(void *x, unsigned items, unsigned size);
void zfree(void *x, void *addr, unsigned nb);

void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp);

void printf () {}
void pause () {}
void exit () {}


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

    if ((bip = get_board_info()) != NULL)
	    memcpy(&board_info, bip, sizeof(bd_t));

    /* setup_bats(RAM_START); */

    /* Init RAM disk (initrd) section */

    if (initrdSect_start != 0 && (initrd_size = initrdSect_size) != 0) {
        initrd_start = (RAM_END - initrd_size) & ~0xFFF;

	printf("Initial RAM disk at 0x%08x (%u bytes)\n",
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
	avail_ram = (void *)(cp + ALIGN_UP(len, 8));
	memcpy(cp, im, len);

	/* I'm not sure what the 0x200000 parameter is for, but it works. */

	gunzip(dst, 0x200000, cp, (int *)&len);
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

void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p = avail_ram;

    size *= items;
    size = ALIGN_UP(size, 8);
    avail_ram += size;
    if (avail_ram > end_avail) {
	printf("oops... out of memory\n");
	pause();
    }
    return p;
}

void zfree(void *x, void *addr, unsigned nb)
{

}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

#define DEFLATED	8

void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
    z_stream s;
    int r, i, flags;

    /* skip header */
    i = 10;
    flags = src[3];
    if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
	printf("bad gzipped data\n");
	exit();
    }
    if ((flags & EXTRA_FIELD) != 0)
	i = 12 + src[10] + (src[11] << 8);
    if ((flags & ORIG_NAME) != 0)
	while (src[i++] != 0)
	    ;
    if ((flags & COMMENT) != 0)
	while (src[i++] != 0)
	    ;
    if ((flags & HEAD_CRC) != 0)
	i += 2;
    if (i >= *lenp) {
	printf("gunzip: ran out of data in header\n");
	exit();
    }
    printf("done 1\n");
    s.zalloc = zalloc;
    s.zfree = zfree;
    r = inflateInit2(&s, -MAX_WBITS);
    if (r != Z_OK) {
	printf("inflateInit2 returned %d\n", r);
	exit();
    }
    s.next_in = src + i;
    s.avail_in = *lenp - i;
    s.next_out = dst;
    s.avail_out = dstlen;
    printf("doing inflate\n");
    r = inflate(&s, Z_FINISH);
    printf("done inflate\n");
    if (r != Z_OK && r != Z_STREAM_END) {
	printf("inflate returned %d\n", r);
	exit();
    }
    *lenp = s.next_out - (unsigned char *) dst;
    printf("doing end\n");
    inflateEnd(&s);
}
