/* ckrm_tsk.h - No. of tasks resource controller for CKRM
 *
 * Copyright (C) Chandra Seetharaman, IBM Corp. 2003
 * 
 * Provides No. of tasks resource controller for CKRM
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 31 Mar 2004
 *    Created.
 */

#ifndef _LINUX_CKRM_TSK_H
#define _LINUX_CKRM_TSK_H

#include <linux/ckrm_rc.h>

typedef int (*get_ref_t) (void *, int);
typedef void (*put_ref_t) (void *);

extern int numtasks_get_ref(void *, int);
extern void numtasks_put_ref(void *);
extern void ckrm_numtasks_register(get_ref_t, put_ref_t);

#endif // _LINUX_CKRM_RES_H
