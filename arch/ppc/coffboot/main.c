/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "nonstdio.h"
#include "rs6000.h"
#include "zlib.h"
#include <asm/bootinfo.h>
#include <asm/processor.h>
#define __KERNEL__
#include <asm/page.h>

extern void *finddevice(const char *);
extern int getprop(void *, const char *, void *, int);
void gunzip(void *, int, unsigned char *, int *);

#define get_16be(x)	(*(unsigned short *)(x))
#define get_32be(x)	(*(unsigned *)(x))

#define RAM_START	0xc0000000
#define PROG_START	RAM_START
#define RAM_END		(RAM_START + 0x800000)	/* only 8M mapped with BATs */

#define RAM_FREE	(RAM_START + 0x540000)	/* after image of coffboot */

char *avail_ram;
char *end_avail;

coffboot(int a1, int a2, void *prom)
{
    void *options;
    unsigned loadbase;
    struct external_filehdr *eh;
    struct external_scnhdr *sp;
    struct external_scnhdr *isect, *rsect;
    int ns, oh, i;
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;

    printf("coffboot starting\n");
    options = finddevice("/options");
    if (options == (void *) -1)
	exit();
    if (getprop(options, "load-base", &loadbase, sizeof(loadbase))
	!= sizeof(loadbase)) {
	printf("error getting load-base\n");
	exit();
    }
    setup_bats(RAM_START);

    loadbase += RAM_START;
    eh = (struct external_filehdr *) loadbase;
    ns = get_16be(eh->f_nscns);
    oh = get_16be(eh->f_opthdr);

    sp = (struct external_scnhdr *) (loadbase + sizeof(struct external_filehdr) + oh);
    isect = rsect = NULL;
    for (i = 0; i < ns; ++i, ++sp) {
	if (strcmp(sp->s_name, "image") == 0)
	    isect = sp;
	else if (strcmp(sp->s_name, "initrd") == 0)
	    rsect = sp;
    }
    if (isect == NULL) {
	printf("image section not found\n");
	exit();
    }

    if (rsect != NULL && (initrd_size = get_32be(rsect->s_size)) != 0) {
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	printf("initial ramdisk at %x (%u bytes)\n",
	       initrd_start, initrd_size);
	memcpy((char *) initrd_start,
	       (char *) (loadbase + get_32be(rsect->s_scnptr)),
	       initrd_size);
	end_avail = (char *) initrd_start;
    } else {
	end_avail = (char *) RAM_END;
    }
	
    im = (unsigned char *)(loadbase + get_32be(isect->s_scnptr));
    len = get_32be(isect->s_size);
    dst = (void *) PROG_START;

    if (im[0] == 0x1f && im[1] == 0x8b) {
	void *cp = (void *) RAM_FREE;
	avail_ram = (void *) (RAM_FREE + ((len + 7) & -8));
	memcpy(cp, im, len);
	printf("gunzipping... ");
	gunzip(dst, 0x400000, cp, &len);
	printf("done\n");

    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);

    sa = (unsigned long)dst;
    printf("start address = 0x%x\n", sa);

#if 0
    pause();
#endif
    {
	    struct bi_record *rec;
	    
	    rec = (struct bi_record *)_ALIGN((unsigned long)dst+len+(1<<20)-1,(1<<20));
	    
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
	    rec->size = sizeof(struct bi_record) + sizeof(unsigned long);
	    rec = (struct bi_record *)((unsigned long)rec + rec->size);
	    
	    rec->tag = BI_LAST;
	    rec->size = sizeof(struct bi_record);
	    rec = (struct bi_record *)((unsigned long)rec + rec->size);
    }
    
    (*(void (*)())sa)(a1, a2, prom);

    printf("returned?\n");

    pause();
}

void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p = avail_ram;

    size *= items;
    size = (size + 7) & -8;
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
	printf("inflate returned %d\n", r);
	exit();
    }
    *lenp = s.next_out - (unsigned char *) dst;
    inflateEnd(&s);
}
