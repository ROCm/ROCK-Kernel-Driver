/*
 * $Id: kl_stabs.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2004 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_STABS_H
#define __KL_STABS_H

/* STABS specific types
 */
#define STAB_XSTRUCT	100	/* Cross referense to STAB_STRUCT */
#define STAB_XUNION	101	/* Cross referense to STAB_UNIONB */
#define STAB_XENUM	102	/* Cross referense to STAB_ENUM */

/* Structure allocated for every namelist. A namelist can be either an
 * object file (.o or executible), or it can be an archive (.a).
 */
typedef struct st_nmlist_s {
	char            *sts_filename;	/* disk file name */
	short		 sts_type;	/* ST_OBJ or ST_AR */
	short		 sts_nfiles;	/* number of source/object files */
} st_nmlist_t;

/* Values for type field
 */
#define ST_OBJ	1	/* object file (.o or executible) */
#define ST_AR	2	/* archive */

/* Stab entry type Flags. For determining which stab entries to
 * capture from the symbol table.
 */
#define ST_UNDF		0x0001
#define ST_SO		0x0002
#define ST_LSYM		0x0004
#define ST_GSYM		0x0008
#define ST_PSYM		0x0010
#define ST_STSYM	0x0020
#define ST_LCSYM	0x0040
#define ST_FUN		0x0080
#define ST_BINCL	0x0100
#define ST_EINCL	0x0200
#define ST_EXCL		0x0400
#define ST_SLINE	0x0800
#define ST_RSYM         0x2000
#define ST_ALL		0xffff
#define ST_DEFAULT	(ST_LSYM|ST_GSYM|ST_FUN)

#define N_UNDF 		0

/* Structures that allow us to selectively cycle through only those BFD
 * sections containing STAB data.
 */
typedef struct stab_sect_s {
	char *stabsect_name;
	char *strsect_name;
} stab_sect_t;

/* Local structure that contains the current type string (which may be
 * just a part of the complete type defenition string) and the character
 * index (current) pointer.
 */
typedef struct stab_str_s {
        char            *str;
        char            *ptr;
} stab_str_t;

/* Local structure containing global values that allow us to cycle
 * through multiple object files without reinitializing.
 */
typedef struct st_global_s {
	bfd		*abfd;		/* current bfd pointer */
	int		 type;		/* symbol entry type */
	int		 flags;		/* want flags */
	int		 flag;		/* current ST flag */
	int		 nmlist;	/* current namelist index */
	int		 srcfile;	/* current source file number */
	int		 incfile;	/* current include file */
	int		 symnum;	/* symbol entry number */
	bfd_byte	*stabp;		/* beg of current string table */
	bfd_byte	*stabs_end;	/* end of current string table */
	int		 staboff;	/* current stab table offset */
	unsigned int	 value;		/* value (e.g., function addr) */
	int		 stroffset;	/* offset in stab string table */
	short		 desc;		/* desc value (e.g, line number) */
	stab_str_t 	 stab_str;	/* current stab string */
} st_global_t;

/* Macros for accessing the current global values
 */
#define G_abfd		G_values.abfd
#define G_type 		G_values.type
#define G_flags 	G_values.flags
#define G_flag  	G_values.flag
#define G_nmlist 	G_values.nmlist
#define G_srcfile 	G_values.srcfile
#define G_incfile 	G_values.incfile
#define G_symnum	G_values.symnum
#define G_stabp         G_values.stabp
#define G_stabs_end     G_values.stabs_end
#define G_staboff	G_values.staboff
#define G_value     	G_values.value
#define G_stroffset	G_values.stroffset
#define G_desc     	G_values.desc
#define G_stab_str     	G_values.stab_str
#define CUR_CHAR 	G_stab_str.ptr

#endif /* __KL_STABS_H */
