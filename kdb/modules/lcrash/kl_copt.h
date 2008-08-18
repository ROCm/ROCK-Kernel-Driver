/*
 * $Id: kl_copt.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
 *
 * Created by Silicon Graphics, Inc.
 *
 * Copyright (C) 2003, 2004 Silicon Graphics, Inc. All rights reserved.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */
#ifndef __KL_COPT_H
#define __KL_COPT_H

extern int copt_ind;
extern char *copt_arg;
extern int copt_error;

void reset_copt(void);
int is_copt(char *);
int get_copt(int, char **, const char *, char **);

#endif /* __KL_COPT_H */
