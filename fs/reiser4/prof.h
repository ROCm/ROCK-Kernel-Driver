/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* profiling. This is i386, rdtsc-based profiling. See prof.c for comments. */

#if !defined( __REISER4_PROF_H__ )
#define __REISER4_PROF_H__

#include "kattr.h"

#if (defined(__i386__) || defined(CONFIG_USERMODE)) && defined(CONFIG_REISER4_PROF)
#define REISER4_PROF (1)
#else
#define REISER4_PROF (0)
#endif

#if REISER4_PROF

#include <asm-i386/msr.h>

#define REISER4_PROF_TRACE_NUM (30)

/* data structure to keep call trace */
typedef struct {
	/* hash of trace---used for fast comparison */
	unsigned long hash;
	/* call trace proper---return addresses collected by
	 * __builtin_return_address() */
	backtrace_path path;
	/* number of times profiled code was entered through this call
	 * chain */
	__u64 hits;
} reiser4_trace;

/* statistics for profiled region of code */
typedef struct {
	/* number of times region was entered */
	__u64 nr;
	/* total time spent in this region */
	__u64 total;
	/* maximal time per enter */
	__u64 max;
	/* number of times region was executed without context switch */
	__u64 noswtch_nr;
	/* total time spent in executions without context switch */
	__u64 noswtch_total;
	/* maximal time of execution without context switch */
	__u64 noswtch_max;
	/* array of back traces */
	reiser4_trace bt[REISER4_PROF_TRACE_NUM];
} reiser4_prof_cnt;

/* profiler entry. */
typedef struct {
	/* sysfs placeholder */
	struct kobject kobj;
	/* statistics, see above */
	reiser4_prof_cnt cnt;
} reiser4_prof_entry;

typedef struct {
	reiser4_prof_entry fuse_wait;
#if 0
	reiser4_prof_entry cbk;
	reiser4_prof_entry init_context;
	reiser4_prof_entry jlook;
	reiser4_prof_entry writepage;
	reiser4_prof_entry jload;
	reiser4_prof_entry jrelse;
	reiser4_prof_entry flush_alloc;
	reiser4_prof_entry forward_squalloc;
	reiser4_prof_entry atom_wait_event;
	reiser4_prof_entry zget;
	/* write profiling */
	reiser4_prof_entry extent_write;
	/* read profiling */
	reiser4_prof_entry file_read;
#endif
} reiser4_prof;

extern reiser4_prof reiser4_prof_defs;

extern unsigned long nr_context_switches(void);
void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now,
		     unsigned long swtch_mark, __u64 start_jif,
		     int delta, int shift);
void calibrate_prof(void);

#define PROF_BEGIN(aname)							\
	unsigned long __swtch_mark__ ## aname = nr_context_switches();		\
        __u64 __prof_jiffies ## aname = jiffies;				\
	__u64 __prof_cnt__ ## aname = ({ __u64 __tmp_prof ;			\
			      		rdtscll(__tmp_prof) ; __tmp_prof; })

#define PROF_END(aname) __PROF_END(aname, REISER4_BACKTRACE_DEPTH, 0)

#define __PROF_END(aname, depth, shift)			\
({							\
	__u64 __prof_end;				\
							\
	rdtscll(__prof_end);				\
	update_prof_cnt(&reiser4_prof_defs.aname.cnt, 	\
			__prof_cnt__ ## aname,		\
			__prof_end,			\
			__swtch_mark__ ## aname, 	\
			__prof_jiffies ## aname, 	\
			depth, shift );			\
})

extern int init_prof_kobject(void);
extern void done_prof_kobject(void);

/* REISER4_PROF */
#else

typedef struct reiser4_prof_cnt {} reiser4_prof_cnt;
typedef struct reiser4_prof {} reiser4_prof;

#define PROF_BEGIN(aname) noop
#define PROF_END(aname) noop
#define __PROF_END(aname, depth, shift) noop
#define calibrate_prof() noop

#define init_prof_kobject() (0)
#define done_prof_kobject() noop

#endif

/* __REISER4_PROF_H__ */
#endif
