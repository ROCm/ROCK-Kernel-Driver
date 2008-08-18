/*
 * $Id: kl_debug.h 1196 2005-05-17 18:34:12Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2005 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_DEBUG_H
#define __KL_DEBUG_H

/* generic functions for reading kerntypes in stabs and dwarf2 formats */

#define DBG_NONE 	0
#define DBG_STABS 	1
#define DBG_DWARF2 	2

extern int debug_format;

#define TYPE_NUM(X)     ((uint64_t)(X) & 0xffffffff)
#define SRC_FILE(X)     (((uint64_t)(X) >> 48) & 0xfff)
#define TYPE_NUM_SLOTS (255)
#define TYPE_NUM_HASH(X) \
        (((SRC_FILE(X)<<1)+TYPE_NUM(X)) % (TYPE_NUM_SLOTS - 1))

typedef struct dbg_type_s {
        kltype_t        st_klt;          /* must be first */

        int             st_bit_offset;   /* from start of struct/union */
        uint64_t 	st_type_num;     /* DBG type_num */
        uint64_t 	st_real_type;    /* real type type_num */
        uint64_t 	st_index_type;   /* type_num of array index */
        uint64_t 	st_element_type; /* type_num of array element */
} dbg_type_t;

#define st_name st_klt.kl_name
#define st_type st_klt.kl_type
#define st_ptr st_klt.kl_ptr
#define st_flags st_klt.kl_flags
#define st_typestr st_klt.kl_typestr
#define st_size st_klt.kl_size
#define st_offset st_klt.kl_offset
#define st_low_bounds st_klt.kl_low_bounds
#define st_high_bounds st_klt.kl_high_bounds
#define st_value st_klt.kl_value
#define st_bit_size st_klt.kl_bit_size
#define st_next st_klt.kl_next
#define st_member st_klt.kl_member
#define st_realtype st_klt.kl_realtype
#define st_indextype st_klt.kl_indextype
#define st_elementtype st_klt.kl_elementtype
#define st_encoding st_klt.kl_encoding

/* Structure containing information about a symbol entry
 */
/* this must match the definition in lkcd's libklib/include/kl_debug.h */
typedef struct dbg_sym_s {
	btnode_t         	sym_bt;	/* must be first */
	short		 	sym_dbgtyp;	/* STABS, DWARF2, ... */
	short		 	sym_state;	/* current state */
	short		 	sym_flag;	/* current flag value */
	short	         	sym_type;	/* symbol type */
	short		 	sym_pvttype;	/* private type */
	short         	 	sym_nmlist;	/* namelist index */
	short            	sym_srcfile;	/* source file index */
	short            	sym_incfile;	/* include file index */
	int		 	sym_num;	/* symbol number */
	int         	 	sym_off;	/* symbol table offset */
	int         	 	sym_stroff;  /* symbol offset in string table */
	uint64_t	 	sym_typenum;	/* arbitrary type number */
	kltype_t		*sym_kltype;	/* Full type information */
	struct dbg_sym_s	*sym_next;	/* next pointer for chaining */
	struct dbg_sym_s	*sym_link;	/* another pointer for chaining */
	int			sym_dup;	/* duplicate symbol */
} dbg_sym_t;
#define sym_name sym_bt.bt_key

extern dbg_sym_t *type_tree;
extern dbg_sym_t *typedef_tree;
extern dbg_sym_t *func_tree;
extern dbg_sym_t *srcfile_tree;
extern dbg_sym_t *var_tree;
extern dbg_sym_t *xtype_tree;
extern dbg_sym_t *symlist;
extern dbg_sym_t *symlist_end;

/* State flags
 */
#define DBG_SETUP		0x1
#define DBG_SETUP_DONE		0x2
#define DBG_SETUP_FAILED	0x4

/* Flags for identifying individual symbol types
 */
#define DBG_SRCFILE	0x0001
#define DBG_TYPE	0x0002
#define DBG_TYPEDEF	0x0004
#define DBG_FUNC	0x0008
#define DBG_PARAM	0x0010
#define DBG_LINE	0x0020
#define DBG_VAR		0x0040
#define DBG_XTYPE	0x0100
#define DBG_ALL		0xffff

/* Structure for cross referencing one type number to another
 */
typedef struct dbg_hashrec_s {
	uint64_t         	 h_typenum;   	/* type number */
	dbg_sym_t		*h_ptr;		/* pointer to actual type */
	struct dbg_hashrec_s	*h_next; 	/* next pointer (for hashing) */
} dbg_hashrec_t;

extern dbg_hashrec_t *dbg_hash[];

#define HASH_SYM	1
#define HASH_XREF	2

/* DBG function prototypes
 */
dbg_sym_t *dbg_alloc_sym(
	int 		/* format */);

void dbg_free_sym(
	dbg_sym_t *     /* dbg_sym_s pointer */);

int dbg_setup_typeinfo(
	dbg_sym_t *	/* dbg_sym_s pointer */);

int dbg_insert_sym(
	dbg_sym_t *	/* dbg_sym_s pointer */);

void dbg_hash_sym(
	uint64_t 	/* typenum */,
	dbg_sym_t *	/* dbg_sym_s pointer */);

dbg_type_t *dbg_walk_hash(
	int *           /* pointer to hash index */,
	void **         /* pointer to hash record pointer */);

dbg_sym_t *dbg_find_sym(
	char *		/* name */,
	int 		/* type number */,
	uint64_t 	/* typenum */);

dbg_sym_t *dbg_first_sym(
	int 		/* type number */);

dbg_sym_t *dbg_next_sym(
	dbg_sym_t *	/* dbg_sym_s pointer */);

dbg_sym_t *dbg_prev_sym(
	dbg_sym_t *	/* dbg_sym_s pointer */);

dbg_type_t *dbg_find_typenum(
	uint64_t 	/* typenum */);

#endif /* __KL_DEBUG_H */
