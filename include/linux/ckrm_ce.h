/* ckrm_ce.h - Header file to be used by Classification Engine of CKRM
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Shailabh Nagar,  IBM Corp. 2003
 *           (C) Chandra Seetharaman, IBM Corp. 2003
 * 
 * Provides data structures, macros and kernel API of CKRM for 
 * classification engine.
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
 * 12 Nov 2003
 *        Created.
 */

#ifndef _LINUX_CKRM_CE_H
#define _LINUX_CKRM_CE_H

#ifdef CONFIG_CKRM

// Max engine name length
#define CKRM_MAX_ENG_NAME 128

/* Action parameters identifying the cause of a task<->class notify callback 
 * these can perculate up to user daemon consuming records send by the classification
 * engine
 */

enum {
	CKRM_ACTION_RECLASSIFY,
	CKRM_ACTION_MANUAL,
	CKRM_ACTION_FORK,
	CKRM_ACTION_EXEC,
	CKRM_ACTION_GID,
	CKRM_ACTION_UID,
	CKRM_ACTION_LISTEN,

	CKRM_ACTION_LAST  /* always the last entry */
};

#ifdef __KERNEL__

typedef struct ckrm_eng_callback {
	/* general state information */
	char ckrm_eng_name[CKRM_MAX_ENG_NAME];
	int  always_callback;  /* set if CE should always be called back regardless of numclasses */

	/* callbacks which are called without holding locks */

	void * (*fork)      (struct task_struct*); // on fork
	void * (*exec)      (struct task_struct*,const char *filename); // on exec

	void * (*reclassify)(struct task_struct *); // on need
	void * (*uid)       (struct task_struct*);  // on uid change
	void * (*gid)       (struct task_struct*);  // on gid change
	void * (*listen)    (void *n); // listen callback

	void   (*manual)    (struct task_struct *);  /* mark manual */

	void   (*class_add) (const char *name, void *core);   /* class added */
	void   (*class_delete)(const char *name, void *core); /* class deleted */


	/* callba which are called while holding task_lock(tsk) */
	void (*notify)(struct task_struct *tsk, void *core, int action); /* notify on class switch */
	void (*exit)  (struct task_struct *tsk);                         /* on exit */

	/* and more to come */

} ckrm_eng_callback_t;

typedef struct rbce_eng_callback {
	int (*mkdir)(struct inode *, struct dentry *, int); // mkdir
	int (*rmdir)(struct inode *, struct dentry *); // rmdir
} rbce_eng_callback_t;

extern int ckrm_register_engine(ckrm_eng_callback_t *);
extern int ckrm_unregister_engine(ckrm_eng_callback_t *);
extern void *ckrm_classobj(char *);
extern int get_exe_path_name(struct task_struct *t, char *filename, int max_size);

extern int rcfs_register_engine(rbce_eng_callback_t *);
extern int rcfs_unregister_engine(rbce_eng_callback_t *);

extern int ckrm_reclassify(int pid);

extern void ckrm_core_grab(void *);
extern void ckrm_core_drop(void *);

#endif // CONFIG_CKRM

#endif // __KERNEL__

#endif // _LINUX_CKRM_CE_H
