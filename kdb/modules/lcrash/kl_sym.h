/*
 * $Id: kl_sym.h 1233 2005-09-10 08:01:11Z tjm $
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

#ifndef __KL_SYM_H
#define __KL_SYM_H

/* The syment struct contains information about kernel symbols (text,
 * data, etc.). The first field in syment_t is a btnode_s sruct. This
 * allows the generic binary search tree routines, insert_tnode() and
 * find_tnode(), to be used.
 */
typedef struct syment_s {
	btnode_t		s_bt;	   /* Must be first */
	struct syment_s        *s_next;    /* For linked lists */
	struct syment_s        *s_prev;    /* For linked lists */
	kaddr_t			s_addr;    /* vaddr of symbol */
	kaddr_t                 s_end;     /* end address of symbol */
	int			s_type;    /* text, data */
	struct syment_s	       *s_forward; /* For linked lists */
} syment_t;

#define s_name s_bt.bt_key

#define SYM_GLOBAL_TEXT	1
#define SYM_LOCAL_TEXT	2
#define SYM_LOCORE_TEXT	3
#define SYM_GLOBAL_DATA	4
#define SYM_LOCAL_DATA	5
#define SYM_ABS 	6
#define SYM_UNK 	9
#define SYM_KSYM 	10
#define SYM_KSYM_TEXT   11
#define SYM_KALLSYMS    12

#define SYM_MAP_ANY 		0
#define SYM_MAP_FILE 		1
#define SYM_MAP_KSYM 		2
#define SYM_MAP_MODULE 		3
#define SYM_MAP_KALLSYMS 	4

#define KL_KERNEL_MODULE  "kernel_module"
#define KL_S_BSS          ".bss.start"
#define KL_E_BSS          ".bss.end"
#define KL_S_DATA         ".data.start"
#define KL_E_DATA         ".data.end"
#define KL_S_RODATA       ".rodata.start"
#define KL_E_RODATA       ".rodata.end"
#define KL_S_TEXT         ".text.start"
#define KL_E_TEXT         ".text.end"
#define KL_SYM_END        "__end__"


#define KL_SYMBOL_NAME_LEN 256

/*
 * Struct containing symbol table information
 */
typedef struct symtab_s {
	int		  symcnt;	/* Number of symbols */
	int		  symaddrcnt;   /* Number of symbol addrs to track */
	syment_t	**symaddrs;	/* Table of symbols by address */
	btnode_t	 *symnames;	/* tree of symbols by name */
	syment_t	 *text_list;	/* Linked list of text symbols */
	syment_t	 *data_list;	/* Linked list of data symbols */
} symtab_t;


/* support of further mapfiles besides System.map */
typedef struct maplist_s {
	struct maplist_s   *next;
	int                maplist_type;  /* type of maplist */
	char               *mapfile;      /* name of mapfile */
	char              *modname;      /* set if map belongs to a module */
	symtab_t           *syminfo;
} maplist_t;


/* API Function prototypes
 */
int  kl_read_syminfo(maplist_t*);
int  kl_free_syminfo(char*);
void kl_free_symtab(symtab_t*);
void kl_free_syment_list(syment_t*);
void kl_free_maplist(maplist_t*);
syment_t *kl_get_similar_name(char*, char*, int*, int*);
syment_t *kl_lkup_symname(char*);
syment_t *_kl_lkup_symname(char*, int, size_t len);
#define KL_LKUP_SYMNAME(NAME, TYPE, LEN) _kl_lkup_symname(NAME, TYPE, LEN)
syment_t *kl_lkup_funcaddr(kaddr_t);
syment_t *kl_lkup_symaddr(kaddr_t);
syment_t *kl_lkup_symaddr_text(kaddr_t);
syment_t *_kl_lkup_symaddr(kaddr_t, int);
#define KL_LKUP_SYMADDR(KADDR, TYPE) _kl_lkup_symaddr(KADDR, TYPE)
kaddr_t kl_symaddr(char * 	/* symbol name */);
kaddr_t kl_symptr(char *	/* symbol name */);
kaddr_t kl_funcaddr(kaddr_t     /* pc value */);
char *kl_funcname(kaddr_t	/* pc value */);
int kl_funcsize(kaddr_t         /* pc value */);
int kl_symsize(syment_t*);
syment_t *kl_alloc_syment(kaddr_t, kaddr_t, int, const char*);
void kl_insert_symbols(symtab_t*, syment_t*);
int  kl_insert_artificial_symbols(symtab_t*, syment_t**, kl_modinfo_t*);
int  kl_convert_symbol(kaddr_t*, int*, char, kl_modinfo_t*);
int  kl_load_sym(char*);
int  kl_print_symtables(char*, char*, int, int);
void kl_print_symbol(kaddr_t, syment_t*, int);

/* flag for use by kl_print_symbol() and kl_print_syminfo()
 */
#define KL_SYMWOFFSET    (0x01)  /* with offset field */
#define KL_SYMFULL       (0x02)  /* print detailed syminfo */
#define KL_SYMBYNAME     (0x04)  /* print symbol sorted by name */

#endif /* __KL_SYM_H */
