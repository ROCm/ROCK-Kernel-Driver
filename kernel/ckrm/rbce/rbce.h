/* Rule-based Classification Engine (RBCE) module
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Chandra Seetharaman, IBM Corp. 2003
 * 
 * Module for loading of classification policies and providing
 * a user API for Class-based Kernel Resource Management (CKRM)
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
 * 25 Mar 2004
 *        Integrate RBCE and CRBE into a single module
 */

#ifndef RBCE_H
#define RBCE_H

// data types defined in main rbcemod.c 
struct rbce_private_data;           
struct rbce_class;
struct ckrm_core_class;

#ifndef RBCE_EXTENSION

/****************************************************************************
 *
 *   RBCE STANDALONE VERSION, NO CHOICE FOR DATA COLLECTION
 *
 ****************************************************************************/

#ifdef RBCE_SHOW_INCL
#warning " ... RBCE .."
#endif

#define RBCE_MOD_DESCR "Rule Based Classification Engine Module for CKRM"
#define RBCE_MOD_NAME  "rbce"

/* extension to private data: NONE */
struct rbce_ext_private_data {
	/* empty data */
};
static inline void init_ext_private_data(struct rbce_private_data *dst)                                { }

/* sending notification to user: NONE */

static void notify_class_action(struct rbce_class *cls, int action)   { } 
static inline void send_fork_notification(struct task_struct *tsk, struct ckrm_core_class *cls) { }
static inline void send_exit_notification(struct task_struct *tsk)   { }
static inline void send_manual_notification(struct task_struct *tsk) { }

/* extension initialization and destruction at module init and exit */
static inline int  init_rbce_ext_pre(void)  { return 0; }
static inline int  init_rbce_ext_post(void) { return 0; }
static inline void exit_rbce_ext(void)      {  }


#else

/***************************************************************************
 *
 *   RBCE with User Level Notification
 *
 ***************************************************************************/

#ifdef RBCE_SHOW_INCL
#warning " ... CRBCE .."
#ifdef RBCE_DO_SAMPLE
#warning " ... CRBCE doing sampling ..."
#endif
#ifdef RBCE_DO_DELAY
#warning " ... CRBCE doing delay ..."
#endif
#endif

#define RBCE_MOD_DESCR "Rule Based Classification Engine Module with Data Sampling/Delivery for CKRM"
#define RBCE_MOD_NAME  "crbce"

#include "crbce.h"

struct rbce_ext_private_data {
	struct task_sample_info  sample;
};

static void  notify_class_action(struct rbce_class *cls, int action);
#if 0
static void  send_fork_notification(struct task_struct *tsk, struct ckrm_core_class *cls);
static void  send_exit_notification(struct task_struct *tsk);  
static void  send_manual_notification(struct task_struct *tsk);
#endif

#endif 

#endif // RBCE_H
