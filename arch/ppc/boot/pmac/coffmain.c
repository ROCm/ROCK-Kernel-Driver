/*
 * BK Id: SCCS/s.coffmain.c 1.14 07/27/01 20:24:18 trini
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
#include "zlib.h"
#include <asm/processor.h>

extern char _start[], _end[];
extern char *claim(unsigned, unsigned, unsigned);
extern char image_data[], initrd_data[];
extern int initrd_len, image_len;
extern int getprop(void *, const char *, void *, int);
extern unsigned int heap_max;
extern void *finddevice(const char *);
extern void flush_cache(void *start, unsigned int len);
extern void gunzip(void *, int, unsigned char *, int *);
extern void make_bi_recs(unsigned long addr, char *name, unsigned int mach,
		unsigned int progend);
extern void pause(void);
extern void setup_bats(unsigned long start);

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;

#define RAM_START	0
#define RAM_END		(RAM_START + 0x800000)	/* only 8M mapped with BATs */

#define PROG_START	RAM_START
#define PROG_SIZE	0x00400000

#define SCRATCH_SIZE	(128 << 10)

static char heap[SCRATCH_SIZE];

void boot(int a1, int a2, void *prom)
{
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;
    
    printf("coffboot starting: loaded at 0x%p\n", &_start);
    setup_bats(RAM_START);
    if (initrd_len) {
	initrd_size = initrd_len;
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	claim(initrd_start - RAM_START, RAM_END - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%p (%x bytes)\n",
	       initrd_start, initrd_data, initrd_size);
	memcpy((char *)initrd_start, initrd_data, initrd_size);
    }
    im = image_data;
    len = image_len;
    /* claim 4MB starting at 0 */
    claim(0, PROG_SIZE, 0);
    dst = (void *) RAM_START;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	/* set up scratch space */
	begin_avail = avail_high = avail_ram = heap;
	end_avail = heap + sizeof(heap);
	printf("heap at 0x%p\n", avail_ram);
	printf("gunzipping (0x%p <- 0x%p:0x%p)...", dst, im, im+len);
	gunzip(dst, PROG_SIZE, im, &len);
	printf("done %u bytes\n", len);
	printf("%u bytes of heap consumed, max in use %u\n",
	       avail_high - begin_avail, heap_max);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    make_bi_recs(((unsigned long) dst + len), "coffboot", _MACH_Pmac,
		    (PROG_START + PROG_SIZE));

    sa = (unsigned long)PROG_START;
    printf("start address = 0x%x\n", sa);

    (*(void (*)())sa)(a1, a2, prom);

    printf("returned?\n");

    pause();
}
