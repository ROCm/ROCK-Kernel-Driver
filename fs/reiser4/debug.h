/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Declarations of debug macros. */

#if !defined( __FS_REISER4_DEBUG_H__ )
#define __FS_REISER4_DEBUG_H__

#include "forward.h"
#include "reiser4.h"


/* generic function to produce formatted output, decorating it with
   whatever standard prefixes/postfixes we want. "Fun" is a function
   that will be actually called, can be printk, panic etc.
   This is for use by other debugging macros, not by users. */
#define DCALL(lev, fun, reperr, label, format, ...)		\
({								\
	reiser4_print_prefix(lev, reperr, label, 		\
			     __FUNCTION__, __FILE__, __LINE__);	\
	fun(lev format "\n" , ## __VA_ARGS__);			\
})

/*
 * cause kernel to crash
 */
#define reiser4_panic(mid, format, ...)				\
	DCALL("", reiser4_do_panic, 1, mid, format , ## __VA_ARGS__)

/* print message with indication of current process, file, line and
   function */
#define reiser4_log(label, format, ...) 				\
	DCALL(KERN_DEBUG, printk, 0, label, format , ## __VA_ARGS__)

/* Assertion checked during compilation.
    If "cond" is false (0) we get duplicate case label in switch.
    Use this to check something like famous
       cassert (sizeof(struct reiserfs_journal_commit) == 4096) ;
    in 3.x journal.c. If cassertion fails you get compiler error,
    so no "maintainer-id".
*/
#define cassert(cond) ({ switch(-1) { case (cond): case 0: break; } })

#define noop   do {;} while(0)

#if REISER4_DEBUG
/* version of info that only actually prints anything when _d_ebugging
    is on */
#define dinfo(format, ...) printk(format , ## __VA_ARGS__)
/* macro to catch logical errors. Put it into `default' clause of
    switch() statement. */
#define impossible(label, format, ...) 			\
         reiser4_panic(label, "impossible: " format , ## __VA_ARGS__)
/* assert assures that @cond is true. If it is not, reiser4_panic() is
   called. Use this for checking logical consistency and _never_ call
   this to check correctness of external data: disk blocks and user-input . */
#define assert(label, cond)						\
({									\
	/* call_on_each_assert(); */					\
	if (cond) {						\
		/* put negated check to avoid using !(cond) that would lose \
		 * warnings for things like assert(a = b); */		\
		;							\
	} else {							\
		DEBUGON(1);						\
		reiser4_panic(label, "assertion failed: %s", #cond);	\
	}								\
})

/* like assertion, but @expr is evaluated even if REISER4_DEBUG is off. */
#define check_me( label, expr )	assert( label, ( expr ) )

#define ON_DEBUG( exp ) exp

extern int schedulable(void);
extern void call_on_each_assert(void);

#else

#define dinfo( format, args... ) noop
#define impossible( label, format, args... ) noop
#define assert( label, cond ) noop
#define check_me( label, expr )	( ( void ) ( expr ) )
#define ON_DEBUG( exp )
#define schedulable() might_sleep()

/* REISER4_DEBUG */
#endif

#if REISER4_DEBUG_SPIN_LOCKS
/* per-thread information about lock acquired by this thread. Used by lock
 * ordering checking in spin_macros.h */
typedef struct lock_counters_info {
	int rw_locked_tree;
	int read_locked_tree;
	int write_locked_tree;

	int rw_locked_dk;
	int read_locked_dk;
	int write_locked_dk;

	int rw_locked_cbk_cache;
	int read_locked_cbk_cache;
	int write_locked_cbk_cache;

	int rw_locked_zlock;
	int read_locked_zlock;
	int write_locked_zlock;

	int spin_locked_jnode;
	int spin_locked_jload;
	int spin_locked_txnh;
	int spin_locked_atom;
	int spin_locked_stack;
	int spin_locked_txnmgr;
	int spin_locked_ktxnmgrd;
	int spin_locked_fq;
	int spin_locked_super;
	int spin_locked_inode_object;
	int spin_locked_epoch;
	int spin_locked_super_eflush;
	int spin_locked;
	int long_term_locked_znode;

	int inode_sem_r;
	int inode_sem_w;

	int d_refs;
	int x_refs;
	int t_refs;
} lock_counters_info;

extern lock_counters_info *lock_counters(void);
#define IN_CONTEXT(a, b) (is_in_reiser4_context() ? (a) : (b))

/* increment lock-counter @counter, if present */
#define LOCK_CNT_INC(counter) IN_CONTEXT(++(lock_counters()->counter), 0)

/* decrement lock-counter @counter, if present */
#define LOCK_CNT_DEC(counter) IN_CONTEXT(--(lock_counters()->counter), 0)

/* check that lock-counter is zero. This is for use in assertions */
#define LOCK_CNT_NIL(counter) IN_CONTEXT(lock_counters()->counter == 0, 1)

/* check that lock-counter is greater than zero. This is for use in
 * assertions */
#define LOCK_CNT_GTZ(counter) IN_CONTEXT(lock_counters()->counter > 0, 1)

/* REISER4_DEBUG_SPIN_LOCKS */
#else

/* no-op versions on the above */

typedef struct lock_counters_info {
} lock_counters_info;
#define lock_counters() ((lock_counters_info *)NULL)
#define LOCK_CNT_INC(counter) noop
#define LOCK_CNT_DEC(counter) noop
#define LOCK_CNT_NIL(counter) (1)
#define LOCK_CNT_GTZ(counter) (1)
/* REISER4_DEBUG_SPIN_LOCKS */
#endif

/*
 * back-trace recording. In several places in reiser4 we want to record stack
 * back-trace for debugging purposes. This functionality is only supported
 * when kernel was configured with CONFIG_FRAME_POINTER option.
 */

#ifdef CONFIG_FRAME_POINTER

/*
 * how many stack frames to record in back-trace.
 *
 * update debug.c:fill_backtrace() if you change this
 */
#define REISER4_BACKTRACE_DEPTH (4)

/*
 * data type to store stack back-trace
 */
typedef struct {
	void *trace[REISER4_BACKTRACE_DEPTH];
} backtrace_path;

extern void fill_backtrace(backtrace_path *path, int depth, int shift);
#else

/* no-op versions on the above */

typedef struct {} backtrace_path;
#define fill_backtrace(path, depth, shift) noop

#endif


/* flags controlling debugging behavior. Are set through debug_flags=N mount
   option. */
typedef enum {
	/* print a lot of information during panic. When this is on all jnodes
	 * are listed. This can be *very* large output. Usually you don't want
	 * this. Especially over serial line. */
	REISER4_VERBOSE_PANIC = 0x00000001,
	/* print a lot of information during umount */
	REISER4_VERBOSE_UMOUNT = 0x00000002,
	/* print gathered statistics on umount */
	REISER4_STATS_ON_UMOUNT = 0x00000004,
	/* check node consistency */
	REISER4_CHECK_NODE = 0x00000008
} reiser4_debug_flags;

extern int reiser4_are_all_debugged(struct super_block *super, __u32 flags);
extern int reiser4_is_debugged(struct super_block *super, __u32 flag);

extern int is_in_reiser4_context(void);

/*
 * evaluate expression @e only if with reiser4 context
 */
#define ON_CONTEXT(e)	do {			\
	if(is_in_reiser4_context()) {		\
		e;				\
	} } while(0)

/*
 * evaluate expression @e only when within reiser4_context and debugging is
 * on.
 */
#define ON_DEBUG_CONTEXT( e ) ON_DEBUG( ON_CONTEXT( e ) )

#if REISER4_DEBUG_MODIFY
/*
 * evaluate expression @exp only if REISER4_DEBUG_MODIFY mode is on.
 */
#define ON_DEBUG_MODIFY( exp ) exp
#else
#define ON_DEBUG_MODIFY( exp )
#endif

/*
 * complain about unexpected function result and crash. Used in "default"
 * branches of switch statements and alike to assert that invalid results are
 * not silently ignored.
 */
#define wrong_return_value( label, function )				\
	impossible( label, "wrong return value from " function )

/* Issue warning message to the console */
#define warning( label, format, ... )					\
	DCALL( KERN_WARNING, 						\
	       printk, 1, label, "WARNING: " format , ## __VA_ARGS__ )

/* mark not yet implemented functionality */
#define not_yet( label, format, ... )				\
	reiser4_panic( label, "NOT YET IMPLEMENTED: " format , ## __VA_ARGS__ )

#if REISER4_TRACE
/* helper macro for tracing, see trace_stamp() below. */
#define IF_TRACE(flags, e) 							\
	if(get_current_trace_flags() & (flags)) e
#else
#define IF_TRACE(flags, e) noop
#endif

/* just print where we are: file, function, line */
#define trace_stamp( f )   IF_TRACE( f, reiser4_log( "trace", "" ) )
/* print value of "var" */
#define trace_var( f, format, var ) 				\
        IF_TRACE( f, reiser4_log( "trace", #var ": " format, var ) )
/* print output only if appropriate trace flag(s) is on */
#define ON_TRACE( f, ... )   IF_TRACE(f, printk(__VA_ARGS__))

/* tracing flags. */
typedef enum {
	/* trace nothing */
	NO_TRACE = 0,
	/* trace vfs interaction functions from vfs_ops.c */
	TRACE_VFS_OPS = (1 << 0),	/* 0x00000001 */
	/* trace plugin handling functions */
	TRACE_PLUGINS = (1 << 1),	/* 0x00000002 */
	/* trace tree traversals */
	TRACE_TREE = (1 << 2),	/* 0x00000004 */
	/* trace znode manipulation functions */
	TRACE_ZNODES = (1 << 3),	/* 0x00000008 */
	/* trace node layout functions */
	TRACE_NODES = (1 << 4),	/* 0x00000010 */
	/* trace directory functions */
	TRACE_DIR = (1 << 5),	/* 0x00000020 */
	/* trace flush code verbosely */
	TRACE_FLUSH_VERB = (1 << 6),	/* 0x00000040 */
	/* trace flush code */
	TRACE_FLUSH = (1 << 7),	/* 0x00000080 */
	/* trace carry */
	TRACE_CARRY = (1 << 8),	/* 0x00000100 */
	/* trace how tree (web) of znodes if maintained through tree
	   balancings. */
	TRACE_ZWEB = (1 << 9),	/* 0x00000200 */
	/* trace transactions. */
	TRACE_TXN = (1 << 10),	/* 0x00000400 */
	/* trace object id allocation/releasing */
	TRACE_OIDS = (1 << 11),	/* 0x00000800 */
	/* trace item shifts */
	TRACE_SHIFT = (1 << 12),	/* 0x00001000 */
	/* trace page cache */
	TRACE_PCACHE = (1 << 13),	/* 0x00002000 */
	/* trace extents */
	TRACE_EXTENTS = (1 << 14),	/* 0x00004000 */
	/* trace locks */
	TRACE_LOCKS = (1 << 15),	/* 0x00008000 */
	/* trace coords */
	TRACE_COORDS = (1 << 16),	/* 0x00010000 */
	/* trace read-IO functions */
	TRACE_IO_R = (1 << 17),	/* 0x00020000 */
	/* trace write-IO functions */
	TRACE_IO_W = (1 << 18),	/* 0x00040000 */

	/* trace log writing */
	TRACE_LOG = (1 << 19),	/* 0x00080000 */

	/* trace journal replaying */
	TRACE_REPLAY = (1 << 20),	/* 0x00100000 */

	/* trace space allocation */
	TRACE_ALLOC = (1 << 21),	/* 0x00200000 */

	/* trace space reservation */
	TRACE_RESERVE = (1 << 22),	/* 0x00400000 */

	/* trace emergency flush */
	TRACE_EFLUSH  = (1 << 23),	/* 0x00800000 */

	/* trace ctails */
	TRACE_CTAIL = (1 << 24),       /* 0x01000000 */

	TRACE_PARSE = (1 << 25),       /* 0x02000000 */

	TRACE_CAPTURE_COPY = (1 << 26), /* 0x04000000 */

	TRACE_EXTENT_ALLOC = (1 << 27),      /* 0x08000000 */

	TRACE_CAPTURE_ANONYMOUS = (1 << 28), /* 0x10000000 */

	/* vague section: used to trace bugs. Use it to issue optional prints
	   at arbitrary points of code. */
	TRACE_BUG = (1 << 31),	/* 0x80000000 */

	/* trace everything above */
	TRACE_ALL = 0xffffffffu
} reiser4_trace_flags;

#if REISER4_LOG
/* helper macro for tracing, see trace_stamp() below. */
#define IF_LOG(flags, e) 							\
	if(get_current_log_flags() & (flags)) e
#else
#define IF_LOG(flags, e) noop
#endif

/* log only if appropriate log flag(s) is on */
#define ON_LOG( f, ... )   IF_LOG(f, printk(__VA_ARGS__))

typedef enum {
	WRITE_NODE_LOG = (1 << 0),      /* log [zj]node operations */
	WRITE_PAGE_LOG = (1 << 1),	/* log make_extent calls */
	WRITE_IO_LOG = (1 << 2), 	/* log i/o requests */
	WRITE_TREE_LOG = (1 << 3), 	/* log internal tree operations */
	WRITE_SYSCALL_LOG = (1 << 4),   /* log system calls */
	READAHEAD_LOG = (1 << 5),       /* log read-ahead activity */
	ALLOC_EXTENT_LOG = (1 << 6),    /* log extent allocation */
	LOG_FILE_PAGE_EVENT = (1 << 7)	/* log events happened to certain file */
} reiser4_log_flags;


extern void reiser4_do_panic(const char *format, ...)
__attribute__ ((noreturn, format(printf, 1, 2)));

extern void reiser4_print_prefix(const char *level, int reperr, const char *mid,
				 const char *function,
				 const char *file, int lineno);

extern int preempt_point(void);
extern void reiser4_print_stats(void);

extern void *reiser4_kmalloc(size_t size, int gfp_flag);
extern void reiser4_kfree(void *area);
extern void reiser4_kfree_in_sb(void *area, struct super_block *sb);
extern __u32 get_current_trace_flags(void);
extern __u32 get_current_log_flags(void);
extern __u32 get_current_oid_to_log(void);

#if REISER4_DEBUG_OUTPUT && REISER4_DEBUG_SPIN_LOCKS
extern void print_lock_counters(const char *prefix,
				const lock_counters_info * info);
extern int no_counters_are_held(void);
extern int commit_check_locks(void);
#else
#define print_lock_counters(p, i) noop
#define no_counters_are_held() (1)
#define commit_check_locks() (1)
#endif

#define REISER4_STACK_ABORT          (8192 - sizeof(struct thread_info) - 30)
#define REISER4_STACK_GAP            (REISER4_STACK_ABORT - 100)

#if REISER4_DEBUG_MEMCPY
extern void *xmemcpy(void *dest, const void *src, size_t n);
extern void *xmemmove(void *dest, const void *src, size_t n);
extern void *xmemset(void *s, int c, size_t n);
#else
#define xmemcpy( d, s, n ) memcpy( ( d ), ( s ), ( n ) )
#define xmemmove( d, s, n ) memmove( ( d ), ( s ), ( n ) )
#define xmemset( s, c, n ) memset( ( s ), ( c ), ( n ) )
#endif

/* true if @i is power-of-two. Useful for rate-limited warnings, etc. */
#define IS_POW(i) 				\
({						\
	typeof(i) __i;				\
						\
	__i = (i);				\
	!(__i & (__i - 1));			\
})

#define KERNEL_DEBUGGER (1)

#if KERNEL_DEBUGGER
/*
 * Check condition @cond and drop into kernel debugger (kgdb) if it's true. If
 * kgdb is not compiled in, do nothing.
 */
#define DEBUGON(cond)				\
({						\
	extern void debugtrap(void);		\
						\
	if (unlikely(cond))			\
		debugtrap();			\
})
#else
#define DEBUGON(cond) noop
#endif

/*
 * Error code tracing facility. (Idea is borrowed from XFS code.)
 *
 * Suppose some strange and/or unexpected code is returned from some function
 * (for example, write(2) returns -EEXIST). It is possible to place a
 * breakpoint in the reiser4_write(), but it is too late here. How to find out
 * in what particular place -EEXIST was generated first?
 *
 * In reiser4 all places where actual error codes are produced (that is,
 * statements of the form
 *
 *     return -EFOO;        // (1), or
 *
 *     result = -EFOO;      // (2)
 *
 * are replaced with
 *
 *     return RETERR(-EFOO);        // (1a), and
 *
 *     result = RETERR(-EFOO);      // (2a) respectively
 *
 * RETERR() macro fills a backtrace in reiser4_context. This back-trace is
 * printed in error and warning messages. Moreover, it's possible to put a
 * conditional breakpoint in return_err (low-level function called by RETERR()
 * to do the actual work) to break into debugger immediately when particular
 * error happens.
 *
 */

#if REISER4_DEBUG

/*
 * data-type to store information about where error happened ("error site").
 */
typedef struct err_site {
	backtrace_path path; /* stack back trace of error */
	int            code; /* error code */
	const char    *file; /* source file, filled by __FILE__ */
	int            line; /* source file line, filled by __LINE__ */
} err_site;

extern void return_err(int code, const char *file, int line);
extern void report_err(void);

/*
 * fill &get_current_context()->err_site with error information.
 */
#define RETERR(code) 				\
({						\
	typeof(code) __code;			\
						\
	__code = (code);			\
	return_err(__code, __FILE__, __LINE__);	\
	__code;					\
})

#else

/*
 * no-op versions of the above
 */

typedef struct err_site {} err_site;
#define RETERR(code) code
#define report_err() noop
#endif

#if REISER4_LARGE_KEY
/*
 * conditionally compile arguments only if REISER4_LARGE_KEY is on.
 */
#define ON_LARGE_KEY(...) __VA_ARGS__
#else
#define ON_LARGE_KEY(...)
#endif

#if REISER4_ALL_IN_ONE
/*
 * declarator used by REISER4_ALL_IN_ONE mode. Every reiser4 function that is
 * not used externally (that is, not used by non-reiser4 code) should be
 * tagged with this. Normally it expands to nothing. In REISER4_ALL_IN_ONE
 * expands to statics allowing compiler to perform better optimization.
 */
#define reiser4_internal static
#else
#define reiser4_internal
#endif

/* operations to clog */
/* debugging re-enterance */

#define GET_USER_PAGES 0
#define PUT_USER_PAGES 1
#define EXTENT_WRITE_IN 2
#define EXTENT_WRITE_OUT 3
#define READPAGE_IN 4
#define READPAGE_OUT 5
#define EXTENT_WRITE_IN2 6
#define EXTENT_WRITE_OUT2 7
#define LINK_OBJECT 8
#define UNLINK_OBJECT 9

#define OP_NUM 10

void clog_op(int op, void *, void *);
void print_clog(void);

/* __FS_REISER4_DEBUG_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
