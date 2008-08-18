/*
 * $Id: kl_module.h 1122 2004-12-21 23:26:23Z tjm $
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

#ifndef __KL_MODULE_H
#define __KL_MODULE_H

/*
 * insmod generates ksymoops
 *
 */

typedef struct kl_modinfo_s {
	char *modname;         /* name of module as loaded in dump */
	/* store ksym info for all modules in a linked list */
	struct kl_modinfo_s *next;
	char *object_file;     /* name of file that module was loaded from*/
	                       /* ? possibly store modtime and version here ? */
	uint64_t header;       /* address of module header */
	uint64_t mtime;        /* time of last modification of object_file */
	uint32_t version;      /* kernel version that module was compiled for */
	uint64_t text_sec;     /* address of text section */
	uint64_t text_len;     /* length of text section */
	uint64_t data_sec;     /* address of data section */
	uint64_t data_len;     /* length of data section */
	uint64_t rodata_sec;   /* address of rodata section */
	uint64_t rodata_len;   /* length of rodata section */
	uint64_t bss_sec;      /* address of rodata section */
	uint64_t bss_len;      /* length of rodata section */
	char *ksym_object;     /* ksym for object */
	char *ksym_text_sec;   /* ksym for its text section */
	char *ksym_data_sec;   /* ksym for its data section */
	char *ksym_rodata_sec; /* ksym for its rodata section */
	char *ksym_bss_sec;    /* ksym for its bss sectio */
} kl_modinfo_t;

int  kl_get_module(char*, kaddr_t*, void**);
int  kl_get_module_2_6(char*, kaddr_t*, void**);
int  kl_get_modname(char**, void*);
int  kl_new_get_modname(char**, void*);
void kl_free_modinfo(kl_modinfo_t**);
int  kl_new_modinfo(kl_modinfo_t**, void*);
int  kl_set_modinfo(kaddr_t, char*, kl_modinfo_t*);
int  kl_complete_modinfo(kl_modinfo_t*);
int  kl_load_ksyms(int);
int  kl_load_ksyms_2_6(int);
int  kl_unload_ksyms(void);
int  kl_load_module_sym(char*, char*, char*);
int  kl_unload_module_sym(char*);
int  kl_autoload_module_info(char*);
kl_modinfo_t * kl_lkup_modinfo(char*);

#endif /* __KL_MODULE_H */
