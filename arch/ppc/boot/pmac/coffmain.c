/*
 * BK Id: SCCS/s.coffmain.c 1.12 06/12/01 16:42:26 paulus
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
#include <asm/bootinfo.h>
#include <asm/processor.h>
#include <asm/page.h>

extern void *finddevice(const char *);
extern int getprop(void *, const char *, void *, int);
extern char *claim(unsigned, unsigned, unsigned);
void make_bi_recs(unsigned long);
void gunzip(void *, int, unsigned char *, int *);

#define get_16be(x)	(*(unsigned short *)(x))
#define get_32be(x)	(*(unsigned *)(x))

#define RAM_START	0
#define RAM_END		(RAM_START + 0x800000)	/* only 8M mapped with BATs */

#define PROG_START	RAM_START
#define PROG_SIZE	0x00400000

#define SCRATCH_SIZE	(128 << 10)

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;
unsigned int heap_use;
unsigned int heap_max;

extern char _start[], _end[];
extern char image_data[];
extern int image_len;
extern char initrd_data[];
extern int initrd_len;

char heap[SCRATCH_SIZE];

boot(int a1, int a2, void *prom)
{
    int ns, oh, i;
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;
    
    printf("coffboot starting: loaded at 0x%x\n", _start);
    setup_bats(RAM_START);
    if (initrd_len) {
	initrd_size = initrd_len;
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	claim(initrd_start - RAM_START, RAM_END - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%x (%x bytes)\n",
	       initrd_start, initrd_data, initrd_size);
	memcpy((char *)initrd_start, initrd_data, initrd_size);
    }
    im = image_data;
    len = image_len;
    /* claim 3MB starting at 0 */
    claim(0, PROG_SIZE, 0);
    dst = (void *) RAM_START;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	/* set up scratch space */
	begin_avail = avail_high = avail_ram = heap;
	end_avail = heap + sizeof(heap);
	printf("heap at 0x%x\n", avail_ram);
	printf("gunzipping (0x%x <- 0x%x:0x%0x)...", dst, im, im+len);
	gunzip(dst, PROG_SIZE, im, &len);
	printf("done %u bytes\n", len);
	printf("%u bytes of heap consumed, max in use %u\n",
	       avail_high - begin_avail, heap_max);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    make_bi_recs((unsigned long)dst + len);
    
    sa = (unsigned long)PROG_START;
    printf("start address = 0x%x\n", sa);

    (*(void (*)())sa)(a1, a2, prom);

    printf("returned?\n");

    pause();
}

void make_bi_recs(unsigned long addr)
{
	struct bi_record *rec;

	addr = _ALIGN(addr+ (1<<20) - 1, (1<<20));
#if 0
	if (addr >= PROG_START + PROG_SIZE)
		claim(addr, 0x1000, 0);
#endif

	rec = (struct bi_record *)addr;
	rec->tag = BI_FIRST;
	rec->size = sizeof(struct bi_record);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	rec->tag = BI_BOOTLOADER_ID;
	sprintf( (char *)rec->data, "coffboot");
	rec->size = sizeof(struct bi_record) + strlen("coffboot") + 1;
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	rec->tag = BI_MACHTYPE;
	rec->data[0] = _MACH_Pmac;
	rec->data[1] = 1;
	rec->size = sizeof(struct bi_record) + 2 * sizeof(unsigned long);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	rec->tag = BI_LAST;
	rec->size = sizeof(struct bi_record);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);
}

struct memchunk {
    unsigned int size;
    struct memchunk *next;
};

static struct memchunk *freechunks;

void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p;
    struct memchunk **mpp, *mp;

    size *= items;
    size = (size + 7) & -8;
    heap_use += size;
    if (heap_use > heap_max)
	heap_max = heap_use;
    for (mpp = &freechunks; (mp = *mpp) != 0; mpp = &mp->next) {
	if (mp->size == size) {
	    *mpp = mp->next;
	    return mp;
	}
    }
    p = avail_ram;
    avail_ram += size;
    if (avail_ram > avail_high)
	avail_high = avail_ram;
    if (avail_ram > end_avail) {
	printf("oops... out of memory\n");
	pause();
    }
    return p;
}

void zfree(void *x, void *addr, unsigned nb)
{
    struct memchunk *mp = addr;

    nb = (nb + 7) & -8;
    heap_use -= nb;
    if (avail_ram == addr + nb) {
	avail_ram = addr;
	return;
    }
    mp->size = nb;
    mp->next = freechunks;
    freechunks = mp;
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
    r = inflate(&s, Z_FINISH);
    if (r != Z_OK && r != Z_STREAM_END) {
	printf("inflate returned %d msg: %s\n", r, s.msg);
	exit();
    }
    *lenp = s.next_out - (unsigned char *) dst;
    inflateEnd(&s);
}
