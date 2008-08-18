/*
 * $Id: kl_typeinfo.h 1259 2006-04-25 18:33:20Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2006 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_TYPEINFO_H
#define __KL_TYPEINFO_H

#define KLT_BASE         0x001
#define KLT_STRUCT       0x002
#define KLT_UNION        0x004
#define KLT_ENUMERATION  0x008
#define KLT_MEMBER       0x010
#define KLT_ARRAY        0x020
#define KLT_POINTER      0x040
#define KLT_TYPEDEF      0x080
#define KLT_FUNCTION     0x100
#define KLT_VARIABLE     0x200
#define KLT_SRCFILE      0x400
#define KLT_SUBRANGE     0x800
#define KLT_INCOMPLETE   0x4000
#define KLT_UNKNOWN      0x8000
#define KLT_TYPE	(KLT_BASE|KLT_STRUCT|KLT_UNION|KLT_ENUMERATION)
#define KLT_TYPES	(KLT_BASE|KLT_STRUCT|KLT_UNION|KLT_ENUMERATION|KLT_TYPEDEF)

#define IS_TYPE(T)      ((T) & KLT_TYPE)
#define IS_STRUCT(T)    ((T) & KLT_STRUCT)
#define IS_UNION(T)     ((T) & KLT_UNION)
#define IS_ENUM(T)      ((T) & KLT_ENUM)
#define IS_MEMBER(T)    ((T) & KLT_MEMBER)
#define IS_POINTER(T)   ((T) & KLT_POINTER)
#define IS_TYPEDEF(T)   ((T) & KLT_TYPEDEF)

#define TYP_SETUP_FLG  		0x01
#define TYP_TYPESTRING_FLG	0x02
#define TYP_INCOMPLETE_FLG  	0x04
#define TYP_XREFERENCE_FLG  	0x08
#define TYP_ANONYMOUS_FLG  	0x10 /* Denotes anonymous union or struct */

#define NO_INDENT           0x01000000
#define SUPPRESS_NAME       0x02000000
#define SUPPRESS_NL         0x04000000
#define SUPPRESS_SEMI_COLON 0x08000000
#define NO_REALTYPE         0x10000000

extern int numnmlist;

#define KL_TYPEINFO()	(numnmlist)

typedef struct kltype_s {
	char			*kl_name;	  /* type name */
        char                    *kl_typestr;      /* 'typecast' string */
	void			*kl_ptr;	  /* ptr to arch typeinfo */
        int                      kl_flags;        /* (e.g., STAB_FLG) */
        int                      kl_type;         /* (e.g., KLT_TYPEDEF) */
        int                      kl_offset;       /* offset to 1st byte */
        int                      kl_size;         /* number of bytes */
        int                      kl_bit_offset;   /* offset to 1st data bit */
        int                      kl_bit_size;     /* total num of data bits */
        int                      kl_encoding;     /* for base value types */
        int                      kl_low_bounds;   /* for arrays */
        int                      kl_high_bounds;  /* for arrays */
        unsigned int             kl_value;        /* enum value, etc. */
        struct kltype_s         *kl_member;       /* struct/union member list */
	struct kltype_s         *kl_next;         /* hash lists, etc. */
	struct kltype_s         *kl_realtype;     /* pointer to real type */
	struct kltype_s         *kl_indextype;    /* pointer to index_type */
	struct kltype_s         *kl_elementtype;  /* pointer to element_type */
} kltype_t;

/* Flag values
 */
#define K_HEX            0x1
#define K_OCTAL          0x2
#define K_BINARY         0x4
#define K_NO_SWAP        0x8

/* Base type encoding values
 */
#define ENC_CHAR	0x01
#define ENC_SIGNED	0x02
#define ENC_UNSIGNED	0x04
#define ENC_FLOAT	0x08
#define ENC_ADDRESS	0x10
#define ENC_UNDEFINED	0x20

/* Maximum number of open namelists
 */
#define MAXNMLIST 10

typedef struct nmlist_s {
	int              index;
	char	       	*namelist;
	void		*private;	/* pointer to private control struct */
	string_table_t 	*stringtab;
} nmlist_t;

extern nmlist_t nmlist[];
extern int numnmlist;
extern int curnmlist;

#define KL_TYPESTR_STRUCT "struct"
#define KL_TYPESTR_UNION  "union"
#define KL_TYPESTR_ENUM   "enum"
#define KL_TYPESTR_VOID   "void"

/* Function prototypes
 */
kltype_t *kl_find_type(
	char *		/* type name */,
	int		/* type number */);

kltype_t *kl_find_next_type(
	kltype_t *	/* kltype_t pointer */,
	int		/* type number */);

kltype_t *kl_first_type(
	int		/* type number */);

kltype_t *kl_next_type(
	kltype_t *	/* kltype_t pointer */);

kltype_t *kl_prev_type(
	kltype_t * 	/* kltype_t pointer */);

kltype_t *kl_realtype(
	kltype_t *	/* kltype_t pointer */,
	int 		/* type number */);

kltype_t *kl_find_typenum(
	uint64_t 	/* private typenumber */);

int kl_get_first_similar_typedef(
	char *		/* type name */,
	char *		/* fullname */);

int kl_type_size(
	kltype_t *	/* kltype_t pointer */);

int kl_struct_len(
	char *		/* struct name */);

kltype_t *kl_get_member(
	kltype_t *	/* kltype_t pointer */,
	char *		/* member name */);

int kl_get_member_offset(
	kltype_t *  /* kltype_t pointer */,
	char *		/* member name */);

int kl_is_member(
	char *		/* struct name */,
	char *		/* member name */);

kltype_t *kl_member(
	char *		/* struct name */,
	char *		/* member name */);

int kl_member_offset(
	char *		/* struct name */,
	char *		/* member name */);

int kl_member_size(
	char *		/* struct name */,
	char *		/* member name */);

/* cpw: get rid of last arguent FILE * */
void kl_print_member(void *, kltype_t *, int, int);
void kl_print_pointer_type(void *, kltype_t *, int, int);
void kl_print_function_type(void *, kltype_t *, int, int);
void kl_print_array_type(void *, kltype_t *, int, int);
void kl_print_enumeration_type(void *, kltype_t *, int, int);
void kl_print_base_type(void *, kltype_t *, int, int);
void kl_print_type(void *, kltype_t *, int, int);
void kl_print_struct_type(void *, kltype_t *, int, int);
void kl_print_base_value(void *, kltype_t *, int);

void kl_print_type(
	void *		/* pointer to data */,
	kltype_t *	/* pointer to type information */,
	int		/* indent level */,
	int		/* flags */);

#endif /* __KL_TYPEINFO_H */
