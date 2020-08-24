/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *   For kvmalloc/kvzalloc
 */
#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>

#ifndef untagged_addr
/* Copied from include/linux/mm.h */
#define untagged_addr(addr) (addr)
#endif

#endif /* AMDKCL_MM_H */
