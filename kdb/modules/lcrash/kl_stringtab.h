/*
 * $Id: kl_stringtab.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
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

#ifndef __KL_STRINGTAB_H
#define __KL_STRINGTAB_H

/* The string table structure
 *
 * String space is allocated from 4K blocks which are allocated
 * as needed. The first four bytes of each block are reserved so
 * that the blocks can be chained together (to make it easy to free
 * them when the string table is no longer necessary).
 */
typedef struct string_table_s {
	int             num_strings;
	void	       *block_list;
} string_table_t;

#define NO_STRINGTAB    0
#define USE_STRINGTAB   1

/**
 ** Function prototypes
 **/

/* Initialize a string table. Depending on the value of the flag
 * parameter, either temporary or permenent blocks will be used.
 * Upon success, a pointer to a string table will be returned.
 * Otherwise, a NULL pointer will be returned.
 */
string_table_t *kl_init_string_table(
	int				/* flag (K_TEMP/K_PERM)*/);

/* Free all memory blocks allocated for a particular string table
 * and then free the table itself.
 */
void kl_free_string_table(
	string_table_t*	/* pointer to string table */);

/* Search for a string in a string table. If the string does not
 * exist, allocate space from the string table and add the string.
 * In either event, a pointer to the string (from the table) will
 * be returned.
 */
char *kl_get_string(
	string_table_t*	/* pointer to string table */,
	char*		/* string to get/add from/to string table */,
	int		/* flag (K_TEMP/K_PERM)*/);

#endif /* __KL_STRINGTAB_H */
