/* Copyright 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* spin lock profiling. See spinprof.c for comments. */

#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"
#include "spin_macros.h"
#include "statcnt.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#if REISER4_LOCKPROF

/* maximal number of profiling regions that can be active at the same time */
#define PROFREGION_MAX_DEPTH (12)

typedef struct percpu_counter scnt_t;

/* spin-locking code uses this to identify place in the code, where particular
 * call to locking function is made. */
typedef struct locksite {
	statcnt_t   hits;   /* number of times profiling region that is
			     * entered at this place of code was found active
			     * my clock interrupt handler. */
	const char *func;   /* function */
	int         line;   /* line in the source file */
} locksite;

/* macro to initialize locksite */
#define LOCKSITE_INIT(name)			\
	static locksite name = {		\
		.hits = STATCNT_INIT,		\
		.func = __FUNCTION__,		\
		.line = __LINE__		\
	}

/* profiling region */
struct profregion {
	/* how many times clock interrupt handler found this profiling region
	 * to be at the top of array of active regions. */
	statcnt_t      hits;
	/* how many times clock interrupt handler found this profiling region
	 * in active array */
	statcnt_t      busy;
	/* sysfs handle */
	struct kobject kobj;
	/* object that (so far) was observed to be locked/contended most
	 * through this region */
	void          *obj;
	/* number of times ->obj's lock was requested/held while in this
	 * region */
	int            objhit;
	/* place in code that (so far) was most active user of this
	 * profregion */
	locksite      *code;
	/* number of times clock interrupt handler observed that ->code was in
	 * this profregion */
	int            codehit;
	/*
	 * optional method called when ->obj is changed. Can be used to output
	 * information about most contended objects.
	 */
	void (*champion)(struct profregion * preg);
};

/*
 * slot in profregionstack used when profregion is activated (that is,
 * entered).
 */
struct pregactivation {
	/* profiling region */
	struct profregion *preg;
	/* pointer to hits counter, embedded into object */
	int               *objloc;
	/* current lock site */
	locksite          *codeloc;
};

/*
 * Stack recording currently active profregion activations. Strictly speaking
 * this is not a stack at all, because locks (and profregions) do not
 * necessary nest properly.
 */
struct profregionstack {
	/* index of next free slot */
	int top;
	/* array of slots for profregion activations */
	struct pregactivation stack[PROFREGION_MAX_DEPTH];
};

DECLARE_PER_CPU(struct profregionstack, inregion);

extern int  profregion_register(struct profregion *pregion);
extern void profregion_unregister(struct profregion *pregion);

extern void profregion_in(int cpu, struct profregion *pregion,
			  void *objloc, locksite *codeloc);
extern void profregion_ex(int cpu, struct profregion *pregion);
extern void profregion_replace(int cpu, struct profregion *pregion,
			       void *objloc, void *codeloc);

/* REISER4_LOCKPROF */
#else

struct profregionstack {};
#define profregion_register(pregion) (0)
#define profregion_unregister(pregion) noop

typedef struct locksite {} locksite;
#define LOCKSITE_INIT(name) extern locksite name

/* REISER4_LOCKPROF */
#endif

/* __SPINPROF_H__ */
#endif
/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
