/*
 * $Id: kl_libutil.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
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

#ifndef __KL_LIBUTIL_H
#define __KL_LIBUTIL_H

/* cpw: change all these from the < > form to the " " form: */
#include "kl_alloc.h"
#include "kl_btnode.h"
#include "kl_copt.h"
#include "kl_htnode.h"
#include "kl_queue.h"
#include "kl_stringtab.h"

int kl_shift_value(uint64_t );
int kl_string_compare(char *, char *);
int kl_string_match(char *, char *);
uint64_t kl_strtoull(char *, char **, int);
time_t kl_str_to_ctime(char *);
void *kl_get_ra(void);

#endif /* __KL_LIBUTIL_H */
