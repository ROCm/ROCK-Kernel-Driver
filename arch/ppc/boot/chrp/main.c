/*
 * BK Id: SCCS/s.main.c 1.13 07/27/01 20:24:17 trini
 */
/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "nonstdio.h"
#include <asm/processor.h>

extern char _end[];
extern char initrd_data[];
extern char image_data[];
extern char sysmap_data[];
extern int getprop(void *, const char *, void *, int);
extern int initrd_len;
extern int image_len;
extern int sysmap_len;
extern unsigned int heap_max;
extern void claim(unsigned int virt, unsigned int size, unsigned int align);
extern void *finddevice(const char *);
extern void flush_cache(void *, unsigned long);
extern void gunzip(void *, int, unsigned char *, int *);
extern void make_bi_recs(unsigned long addr, char *name, unsigned int mach,
		unsigned int progend);
extern void pause(void);

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;

#define RAM_START	0x00000000
#define RAM_END		(64<<20)

#define BOOT_START	((unsigned long)_start)
#define BOOT_END	((unsigned long)(_end + 0xFFF) & ~0xFFF)

#define RAM_FREE	((unsigned long)(_end+0x1000)&~0xFFF)
#define PROG_START	0x00010000
#define PROG_SIZE	0x00400000 /* 4MB */

#define SCRATCH_SIZE	(128 << 10)

static char scratch[SCRATCH_SIZE];	/* 1MB of scratch space for gunzip */

void
chrpboot(int a1, int a2, void *prom)
{
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start=0, initrd_size=0;
    extern char _start;
    
    printf("chrpboot starting: loaded at 0x%p\n\r", &_start);

    if (initrd_len) {
	initrd_size = initrd_len;
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	claim(initrd_start, RAM_END - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%p (%x bytes)\n\r",
	       initrd_start, initrd_data, initrd_size);
	memcpy((char *)initrd_start, initrd_data, initrd_size);
    }

    im = image_data;
    len = image_len;
    /* claim 4MB starting at PROG_START */
    claim(PROG_START, PROG_SIZE - PROG_START, 0);
    dst = (void *) PROG_START;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	avail_ram = scratch;
	begin_avail = avail_high = avail_ram;
	end_avail = scratch + sizeof(scratch);
	printf("gunzipping (0x%p <- 0x%p:0x%p)...", dst, im, im+len);
	gunzip(dst, 0x400000, im, &len);
	printf("done %u bytes\n\r", len);
	printf("%u bytes of heap consumed, max in use %u\n\r",
	       avail_high - begin_avail, heap_max);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    make_bi_recs(((unsigned long) dst + len), "chrpboot", _MACH_chrp,
		    (PROG_START + PROG_SIZE));
    
    sa = (unsigned long)PROG_START;
    printf("start address = 0x%x\n\r", sa);

    (*(void (*)())sa)(a1, a2, prom, initrd_start, initrd_size);

    printf("returned?\n\r");

    pause();
}
