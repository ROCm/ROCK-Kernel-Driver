/* ckrm.h - Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003,2004
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
 * 19 Nov 2004
 *        New Event callback structure
 */

#ifndef _LINUX_CKRM_H
#define _LINUX_CKRM_H

#ifdef CONFIG_CKRM

// Data structure and function to get the list of registered 
// resource controllers.

// #include <linux/sched.h>

/* CKRM defines a set of events at particular points in the kernel
 * at which callbacks registered by various class types are called
 */

enum ckrm_event {
	/* we distinguish various events types
         *
	 * (a) CKRM_LATCHABLE_EVENTS
         *      events can be latched for event callbacks by classtypes
         *
	 * (b) CKRM_NONLATACHBLE_EVENTS
         *     events can not be latched but can be used to call classification
         * 
	 * (c) event that are used for notification purposes
	 *     range: [ CKRM_EVENT_CANNOT_CLASSIFY .. )
         */

	/* events (a) */

	CKRM_LATCHABLE_EVENTS,

	CKRM_EVENT_NEWTASK = CKRM_LATCHABLE_EVENTS,
	CKRM_EVENT_FORK,
	CKRM_EVENT_EXIT,
	CKRM_EVENT_EXEC,
	CKRM_EVENT_UID,
	CKRM_EVENT_GID,
	CKRM_EVENT_LOGIN,
	CKRM_EVENT_USERADD,
	CKRM_EVENT_USERDEL,
	CKRM_EVENT_LISTEN_START,
	CKRM_EVENT_LISTEN_STOP,
	CKRM_EVENT_APPTAG,

	/* events (b) */

	CKRM_NONLATCHABLE_EVENTS,

	CKRM_EVENT_RECLASSIFY = CKRM_NONLATCHABLE_EVENTS,

	/* events (c) */
	CKRM_NOTCLASSIFY_EVENTS,

	CKRM_EVENT_MANUAL = CKRM_NOTCLASSIFY_EVENTS,
	
	CKRM_NUM_EVENTS
};
#endif

#ifdef __KERNEL__
#ifdef CONFIG_CKRM

extern void ckrm_invoke_event_cb_chain(enum ckrm_event ev, void *arg);

typedef void (*ckrm_event_cb)(void *arg);

struct ckrm_hook_cb {
	ckrm_event_cb fct;
	struct ckrm_hook_cb *next;
};

#define CKRM_DEF_CB(EV,fct)					\
static inline void ckrm_cb_##fct(void)				\
{								\
         ckrm_invoke_event_cb_chain(CKRM_EVENT_##EV,NULL);      \
}

#define CKRM_DEF_CB_ARG(EV,fct,argtp)					\
static inline void ckrm_cb_##fct(argtp arg)				\
{									\
         ckrm_invoke_event_cb_chain(CKRM_EVENT_##EV,(void*)arg);	\
}

#else // !CONFIG_CKRM

#define CKRM_DEF_CB(EV,fct)			\
static inline void ckrm_cb_##fct(void)  { }

#define CKRM_DEF_CB_ARG(EV,fct,argtp)		\
static inline void ckrm_cb_##fct(argtp arg) { }

#endif // CONFIG_CKRM

/*-----------------------------------------------------------------
 *   define the CKRM event functions 
 *               EVENT          FCT           ARG         
 *-----------------------------------------------------------------*/

// types we refer at 
struct task_struct;
struct sock;
struct user_struct;

CKRM_DEF_CB_ARG( FORK         , fork,         struct task_struct *);
CKRM_DEF_CB_ARG( EXEC         , exec,         const char*         );
CKRM_DEF_CB    ( UID          , uid                               );
CKRM_DEF_CB    ( GID          , gid                               );
CKRM_DEF_CB    ( APPTAG       , apptag                            );
CKRM_DEF_CB    ( LOGIN        , login                             );
CKRM_DEF_CB_ARG( USERADD      , useradd,      struct user_struct *);
CKRM_DEF_CB_ARG( USERDEL      , userdel,      struct user_struct *);
CKRM_DEF_CB_ARG( LISTEN_START , listen_start, struct sock *       );
CKRM_DEF_CB_ARG( LISTEN_STOP  , listen_stop,  struct sock *       );

// and a few special one's
void ckrm_cb_newtask(struct task_struct *);
void ckrm_cb_exit(struct task_struct *);

// some other functions required
extern void ckrm_init(void);
extern int get_exe_path_name(struct task_struct *, char *, int);

#endif // __KERNEL__

#endif // _LINUX_CKRM_H
