/* ckrm_tasks_stub.c - Stub file for ckrm_tasks modules
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2004
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
 * 16 May 2004: Created
 * 
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/ckrm_tsk.h>

static spinlock_t stub_lock = SPIN_LOCK_UNLOCKED;

static get_ref_t real_get_ref = NULL;
static put_ref_t real_put_ref = NULL;

void
ckrm_numtasks_register(get_ref_t gr, put_ref_t pr)
{
	spin_lock(&stub_lock);
	real_get_ref = gr;
	real_put_ref = pr;
	spin_unlock(&stub_lock);
}

int
numtasks_get_ref(void *arg, int force)
{
	int ret = 1;
	spin_lock(&stub_lock);
	if (real_get_ref) {
		ret = (*real_get_ref) (arg, force);
	}
	spin_unlock(&stub_lock);
	return ret;
}

void
numtasks_put_ref(void *arg)
{
	spin_lock(&stub_lock);
	if (real_put_ref) {
		(*real_put_ref) (arg);
	}
	spin_unlock(&stub_lock);
}


EXPORT_SYMBOL(ckrm_numtasks_register);
EXPORT_SYMBOL(numtasks_get_ref);
EXPORT_SYMBOL(numtasks_put_ref);
