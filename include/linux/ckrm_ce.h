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

#ifdef __KERNEL__

#ifdef CONFIG_CKRM

//#include <linux/ckrm.h>

// Max engine name length
#define CKRM_MAX_ENG_NAME 128

typedef struct ckrm_eng_callback {
	char ckrm_eng_name[CKRM_MAX_ENG_NAME];
	void * (*fork)(struct task_struct*); // on fork
	void * (*exec)(struct task_struct*,const char *filename); // on exec

	void * (*reclassify)(struct task_struct *); // on need
	void * (*uid)(struct task_struct*);  // on uid change
	void * (*gid)(struct task_struct*);  // on gid change

	void (*manual)(struct task_struct *);  /* manual reclassification */
	void (*exit)(struct task_struct *);  /* on exit - just notification */

	void (*class_add)(const char *name, void *core);   /* class added */
	void (*class_delete)(const char *name, void *core);      /* class deleted */

	void * (*listen_cb)(void *n); // listen callback
	int always_callback;
	/* and more to come */

	/* Hubertus.. this should be removed in the final version when API 
	   transition was done */
	int  (*engine_ctl)(unsigned int op, void *data); /* user level ctl api */
} ckrm_eng_callback_t;

typedef struct rbce_eng_callback {
	int (*mkdir)(struct inode *, struct dentry *, int); // mkdir
	int (*rmdir)(struct inode *, struct dentry *); // rmdir
} rbce_eng_callback_t;

extern int ckrm_register_engine(ckrm_eng_callback_t *);
extern int ckrm_unregister_engine(ckrm_eng_callback_t *);
extern void *ckrm_classobj(char *);
extern void ckrm_reclassify(int);
extern int get_exe_path_name(struct task_struct *t, char *filename, int max_size);

extern int rcfs_register_engine(rbce_eng_callback_t *);
extern int rcfs_unregister_engine(rbce_eng_callback_t *);


#endif // CONFIG_CKRM

#endif // __KERNEL__

#endif // _LINUX_CKRM_CE_H
