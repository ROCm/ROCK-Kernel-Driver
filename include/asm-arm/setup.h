/*
 *  linux/include/asm/setup.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Structure passed to kernel to tell it about the
 *  hardware it's running on.  See linux/Documentation/arm/Setup
 *  for more info.
 */
#ifndef __ASMARM_SETUP_H
#define __ASMARM_SETUP_H

/*
 * Usage:
 *  - do not go blindly adding fields, add them at the end
 *  - when adding fields, don't rely on the address until
 *    a patch from me has been released
 *  - unused fields should be zero (for future expansion)
 *  - this structure is relatively short-lived - only
 *    guaranteed to contain useful data in setup_arch()
 */
#define COMMAND_LINE_SIZE 1024

struct param_struct {
    union {
	struct {
	    unsigned long page_size;		/*  0 */
	    unsigned long nr_pages;		/*  4 */
	    unsigned long ramdisk_size;		/*  8 */
	    unsigned long flags;		/* 12 */
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
	    unsigned long rootdev;		/* 16 */
	    unsigned long video_num_cols;	/* 20 */
	    unsigned long video_num_rows;	/* 24 */
	    unsigned long video_x;		/* 28 */
	    unsigned long video_y;		/* 32 */
	    unsigned long memc_control_reg;	/* 36 */
	    unsigned char sounddefault;		/* 40 */
	    unsigned char adfsdrives;		/* 41 */
	    unsigned char bytes_per_char_h;	/* 42 */
	    unsigned char bytes_per_char_v;	/* 43 */
	    unsigned long pages_in_bank[4];	/* 44 */
	    unsigned long pages_in_vram;	/* 60 */
	    unsigned long initrd_start;		/* 64 */
	    unsigned long initrd_size;		/* 68 */
	    unsigned long rd_start;		/* 72 */
	    unsigned long system_rev;		/* 76 */
	    unsigned long system_serial_low;	/* 80 */
	    unsigned long system_serial_high;	/* 84 */
	    unsigned long mem_fclk_21285;       /* 88 */ 
	} s;
	char unused[256];
    } u1;
    union {
	char paths[8][128];
	struct {
	    unsigned long magic;
	    char n[1024 - sizeof(unsigned long)];
	} s;
    } u2;
    char commandline[COMMAND_LINE_SIZE];
};

/*
 * Memory map description
 */
#define NR_BANKS 4

struct meminfo {
	int nr_banks;
	unsigned long end;
	struct {
		unsigned long start;
		unsigned long size;
		int           node;
	} bank[NR_BANKS];
};

extern struct meminfo meminfo;

#endif
