/* ckrm.h - Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Shailabh Nagar,  IBM Corp. 2003
 *           (C) Chandra Seetharaman, IBM Corp. 2003
 * 
 * 
 * Provides a base header file including macros and basic data structures.
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
 * 28 Aug 2003
 *        Created.
 * 06 Nov 2003
 *        Made modifications to suit the new RBCE module.
 * 10 Nov 2003
 *        Added callbacks_active and surrounding logic. Added task paramter
 *        for all CE callbacks.
 */

#ifndef _LINUX_CKRM_H
#define _LINUX_CKRM_H

#ifdef __KERNEL__
#ifdef CONFIG_CKRM

// Data structure and function to get the list of registered 
// resource controllers.

#include <linux/sched.h>

extern void ckrm_init(void);

// Interfaces used from classification points
extern void ckrm_cb_exec(const char *);
extern void ckrm_cb_fork(struct task_struct *);
extern void ckrm_cb_exit(struct task_struct *);
extern void ckrm_cb_uid(void);
extern void ckrm_cb_gid(void);
extern void ckrm_cb_apptag(void);
extern void ckrm_cb_login(void);
extern void ckrm_cb_useradd(struct user_struct *);
extern void ckrm_cb_userdel(struct user_struct *);
extern void ckrm_new_task(struct task_struct *);

// Utility functions.
extern int get_exe_path_name(struct task_struct *, char *, int);

#else // !CONFIG_CKRM

#define ckrm_init()
#define ckrm_cb_exec(cmd)
#define ckrm_cb_fork(t)
#define ckrm_cb_exit(t)
#define ckrm_cb_uid()
#define ckrm_cb_gid()
#define ckrm_cb_apptag()
#define ckrm_cb_login()
#define ckrm_cb_useradd(x)
#define ckrm_cb_userdel(x)
#define ckrm_new_task(x)

#endif // CONFIG_CKRM

#endif // __KERNEL__

#endif // _LINUX_CKRM_H
