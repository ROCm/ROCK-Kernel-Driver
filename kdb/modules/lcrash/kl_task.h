/*
 * $Id: kl_task.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2002, 2004 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_TASK_H
#define __KL_TASK_H

extern kaddr_t deftask;

/* Function prototypes
 */
k_error_t kl_set_deftask(kaddr_t);
int kl_parent_pid(void *);
kaddr_t kl_pid_to_task(kaddr_t);
k_error_t kl_get_task_struct(kaddr_t, int, void *);
kaddr_t kl_kernelstack(kaddr_t);
kaddr_t kl_first_task(void);
kaddr_t kl_next_task(void *);
kaddr_t kl_prev_task(void *);
kaddr_t kl_pid_to_task(kaddr_t);
int kl_task_size(kaddr_t);

#endif /* __KL_TASK_H */
