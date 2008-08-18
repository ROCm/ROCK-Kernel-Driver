/*
 * $Id: kl_cmp.h 1216 2005-07-06 10:03:13Z holzheu $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_CMP_H
#define __KL_CMP_H

#define DUMP_INDEX_MAGIC    0xdeadbeef
#define DUMP_INDEX_VERSION  31900
#define NUM_BUCKETS         65535

/*
 * Definitions for compressed cached reads.  I've recently lowered
 * these ... If they need to be increased later, I'll do so.
 */
#define CMP_HIGH_WATER_MARK 25
#define CMP_LOW_WATER_MARK  10

#define CMP_VM_CACHED   0x01
#define CMP_VM_UNCACHED 0x02


/*
 * This structure defines a page table entry, what each value will
 * contain.  Since these can be cached or uncached, we have a flags
 * variable to specify this.
 */
typedef struct _ptableentry {
	int          flags;                /* flags for page in cache   */
	int          length;               /* length of page            */
	int          cached;               /* cached (1 = yes, cached)  */
	kaddr_t      addr;                 /* addr of page              */
	char         *data;                /* data in page              */
	struct _ptableentry *next;         /* ptr to next dump page     */
	struct _ptableentry *prev;         /* ptr to prev dump page     */
	struct _ptableentry *nextcache;    /* ptr to next cached page   */
	struct _ptableentry *prevcache;    /* ptr to prev cached page   */
} ptableentry;

/*
 * This is for the page table index from the compressed core dump.
 * This is separate from the page table entries because these are
 * simply addresses off of the compressed core dump, and not the
 * actual data from the core dump.  If we hash these values, we gain
 * a lot of performance because we only have 1 to search for the
 * page data, 1 to search for the index, and return if both searches
 * failed.
 */
typedef struct _ptableindex {
	kl_dump_page_t dir;          /* directory entry of page         */
	kaddr_t addr;                /* address of page offset          */
	kaddr_t coreaddr;            /* address of page in core         */
	unsigned int hash;           /* hash value for this index item  */
	struct _ptableindex *next;   /* next pointer                    */
} ptableindex;

typedef struct dump_index_s {
	unsigned int magic_number;   /* dump index magic number         */
	unsigned int version_number; /* dump index version number       */
	/* struct timeval depends on machine, use two long values here */
	struct {uint64_t tv_sec;
		uint64_t tv_usec;
	} timebuf; /* the time of the dump */
} __attribute__((packed)) dump_index_t;

/* Compression function */
typedef int (*kl_compress_fn_t)(const unsigned char *old, uint32_t old_size, unsigned char *new, uint32_t size);

/* function declarations
 */
int  kl_cmpreadmem(int, kaddr_t, char*, unsigned int, unsigned int);
int  kl_cmpinit(
	int 			/* fd */,
	char *			/* indexname */,
	int 			/* flags */);

/* Compression routine: No compression */
int kl_compress_none(const char *old, uint32_t old_size, char *new, uint32_t new_size);

/* Compression routine: Run length encoding */
int kl_compress_rle(const char *old, uint32_t old_size, char *new, uint32_t new_size);

/* Compression routine: GZIP */
int kl_compress_gzip(const unsigned char *old, uint32_t old_size, unsigned char *new, uint32_t new_size);

#endif /* __KL_CMP_H */
