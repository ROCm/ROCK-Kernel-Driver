/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* definitions of common constants used by reiser4 */

#if !defined( __REISER4_H__ )
#define __REISER4_H__

#include <linux/config.h>
#include <asm/param.h>		/* for HZ */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/hardirq.h>
#include <linux/sched.h>

#if defined(CONFIG_4KSTACKS)
#error "Please turn 4k stack off"
#endif

/*
 * reiser4 compilation options.
 */

#if defined(CONFIG_REISER4_DEBUG)
/* turn on assertion checks */
#define REISER4_DEBUG (1)
#else
#define REISER4_DEBUG (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_MODIFY)
/*
 * Turn on "znode modification checks". In this mode znode check-sum is
 * maintained in special field added to znode. Check-sum is updated during
 * znode_make_dirty() (or, during zload()). It is checked that check-sum is
 * only if ever updated between acquiring write lock on znode and calling
 * znode_make_dirty(). This significantly slows down testing, but we should
 * run our test-suite through with this every once in a while.  */
#define REISER4_DEBUG_MODIFY (1)
#else
#define REISER4_DEBUG_MODIFY (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_MEMCPY)
/* Provide our own memcpy/memmove to profile shifts. Reiser4 code uses
 * xmem{cpy,move,set}() functions in stead of mem{cpy,move,set}(). When
 * REISER4_DEBUG_MEMCPY is on, care is taken to uninline xmem* functions so
 * that they show up in CPU profiling (/proc/profile, for example) separately
 * from calling functions. This is done to estimate CPU consumption of memory
 * shifts. When this mode is off, xmem* functions are preprocessed into their
 * mem* analogs. */
#define REISER4_DEBUG_MEMCPY (1)
#else
#define REISER4_DEBUG_MEMCPY (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_NODE)
/*
 * Check consistency of internal node structures. When this mode is on, node
 * consistency check (implemented by plugin/node/node.c:node_check() function)
 * are invoked in many places (including start and end of most node plugin
 * methods). node_check() includes a lot of checks, see it for details.
 *
 * Node consistency checking (which is off by default) has to be activated by
 * setting REISER4_CHECK_NODE bit in ->debug_flags field of
 * reiser4_super_info_data. This can be done with debug_flags mount option.
 */
#define REISER4_DEBUG_NODE (1)
#else
#define REISER4_DEBUG_NODE (0)
#endif

#if defined(CONFIG_REISER4_ZERO_NEW_NODE)
/* If this is non-zero, clear content of new node, otherwise leave whatever
   may happen to be here */
#define REISER4_ZERO_NEW_NODE (1)
#else
#define REISER4_ZERO_NEW_NODE (0)
#endif

#if defined(CONFIG_REISER4_TRACE)
/* tracing facility. When this is on, {ON,IF}_TRACE statements are
 * activated. Thy print (through printk()) information about control flow
 * (what function is called, with what arguments, etc.). {ON,IF}_TRACE
 * statements contain "trace mask" and output is done only when this mask
 * matches current trace mask, calculated by get_current_trace_flags()
 * function. Current trace mask is combined from per-thread context mask
 * (stored in reiser4_context), and per-super-block mask (stored in
 * ->trace_flags field of reiser4_super_info_data). Per-super-block trace mask
 * can be adjusted through:
 *
 *     1. mount option "trace_flags"
 *
 *     2. /sys/fs/reiser4/<dev>/trace_flags file.
 *
 */
#define REISER4_TRACE (1)
#else
#define REISER4_TRACE (0)
#endif

#if defined(CONFIG_REISER4_EVENT_LOG)
/*
 * Collect event logs. When this is on, logging macros/functions declared in
 * fs/reiser4/log.h are activated. Event-logging facility is designed to cope
 * with large amount of output data. To this end, event descriptions are
 * buffered in the internal buffer (of REISER4_TRACE_BUF_SIZE bytes) and then
 * written into user-visible log file. Log file is specified through log_file
 * mount option.
 *
 * Events which are logged are specified through log_flags mount option (or
 * /sys/fs/reiser4/<dev>/log_flags file). See
 * fs/reiser4/debug.h:reiser4_log_flags for possible values.
 *
 * Note that event-logging is for gathering statistics (as opposed to tracing,
 * which is for debugging).
 *
 * When running experiments with event-logging on, it's important to minimize
 * an impact of event-logging to the system. It was found that one of the most
 * disturbing effects of event-logging is continuous generation of dirty
 * memory that triggers premature write-back and, generally, affects system
 * behavior in various ways. To work around this set log file to named pipe,
 * and use netcat(1) to dump log through over network.
 *
 */
#define REISER4_LOG (1)
#else
#define REISER4_LOG (0)
#endif

#if defined(CONFIG_REISER4_STATS)
/*
 * Collect statistics. In this mode reiser4 collects a lot of statistical
 * information in the form if "stat-counters". There are global counters
 * (per-super-block) and per-level counters collected separately for each
 * level of the internal reiser4 tree. See fs/reiser4/stats.[ch] for the list
 * of counters. Counters are exported under /sys/fs/reiser4/<dev>/stats/
 *
 * Note: this option consumes quite a bit of kernel memory.
 */
#define REISER4_STATS (1)
#else
#define REISER4_STATS (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_OUTPUT)
/*
 * In this mode various "debugging output" functions are compiled in. These
 * functions output human readable representation of various reiser4 kernel
 * data-structures (keys, tree nodes, items, etc.), which are used in error
 * messages.
 */
#define REISER4_DEBUG_OUTPUT (1)
#else
#define REISER4_DEBUG_OUTPUT (0)
#endif

#if defined(CONFIG_REISER4_COPY_ON_CAPTURE)
/*
 * Turns on copy-on-capture (COC) optimization. See
 * http://www.namesys.com/v4/v4.html#cp_on_capture
 */
#define REISER4_COPY_ON_CAPTURE (1)
#else
#define REISER4_COPY_ON_CAPTURE (0)
#endif

#if defined(CONFIG_REISER4_LOCKPROF)
/*
 * Turns on lock profiling mode. In this mode reiser4 spin-locks are
 * instrumented to collect information about their contention and
 * utilization. See fs/reiser4/spinprof.[ch] for details.
 *
 * Lock profiling results are exported as /sys/profregion/
 */
#define REISER4_LOCKPROF (1)
#else
#define REISER4_LOCKPROF (0)
#endif

#if defined(CONFIG_REISER4_LARGE_KEY)
/*
 * Turn on large keys mode. In his mode (which is default), reiser4 key has 4
 * 8-byte components. In the old "small key" mode, it's 3 8-byte
 * components. Additional component, referred to as "ordering" is used to
 * order items from which given object is composed of. As such, ordering is
 * placed between locality and objectid. For directory item ordering contains
 * initial prefix of the file name this item is for. This sorts all directory
 * items within given directory lexicographically (but see
 * fibration.[ch]). For file body and stat-data, ordering contains initial
 * prefix of the name file was initially created with. In the common case
 * (files with single name) this allows to order file bodies and stat-datas in
 * the same order as their respective directory entries, thus speeding up
 * readdir.
 *
 * Note, that kernel can only mount file system with the same key size as one
 * it is compiled for, so flipping this option may render your data
 * inaccessible.
 */
#define REISER4_LARGE_KEY (1)
#else
#define REISER4_LARGE_KEY (0)
#endif

#if defined(CONFIG_REISER4_ALL_IN_ONE)
/*
 * Turn on all-on-one compilation mode. In this mode reiser4 is compiled as
 * one single source file all-in-one.c that includes all other sources. This
 * is supposed to result in better code, because compiler is free to perform
 * all optimizations within the same compilation unit. To achieve this,
 * (almost) all reiser4 functions are prefixed with reiser4_internal
 * specifier. In normal compilation mode it expands to nothing, in all-in-one
 * mode, it expands to "static", thus telling compiler that function is only
 * used in this compilation unit (that is, in whole reiser4).
 *
 * Note-1: compilation in this mode would result in large number of warnings,
 * because header files weren't updated.
 *
 * Note-2: in addition to generating better code this mode can be used to
 * detect declared but not used functions, or declarations without definition.
 *
 * Note-3: this should be tried with -funit-at-a-time option of gcc 3.4
 */
#define REISER4_ALL_IN_ONE (1)
#else
#define REISER4_ALL_IN_ONE (0)
#endif

#if defined (CONFIG_REISER4_DEBUG_NODE_INVARIANT)
/*
 * In this mode [zj]node invariants are checked. This mode is not usually on,
 * because it consumes a lot of CPU. See [zj]node_invariant() and
 * doc/lock-ordering for description of invariants checked.
 */
#define REISER4_DEBUG_NODE_INVARIANT (1)
#else
#define REISER4_DEBUG_NODE_INVARIANT (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_SPIN_LOCKS) && defined(CONFIG_REISER4_DEBUG)
/*
 * Turns on spin-lock debugging. Many (but not all) spin-locks used by reiser4
 * are accessed through special wrapper macros defined in spin_macros.h. These
 * macros allow, among other things, to specify for a given spin-lock type its
 * "lock ordering predicate" that specifies what other locks may or may not be
 * held simultaneously with this one. Spin-lock debugging checks for these
 * ordering constraints along with trivial checks for proper lock/unlock
 * nesting, etc. Note, that spin_macros.h also support spin-lock profiling
 * described above (CONFIG_REISER4_LOCKPROF).
 *
 * Note: this is not available through fs/Kconfig. Adjust manually.
 */
#define REISER4_DEBUG_SPIN_LOCKS (1)
#else
#define REISER4_DEBUG_SPIN_LOCKS (0)
#endif

#define CONFIG_REISER4_DEBUG_CONTEXTS y
#if defined(CONFIG_REISER4_DEBUG_CONTEXTS) && defined(CONFIG_REISER4_DEBUG)
/*
 * In this mode reiser4_context debugging is activated. reiser4_context is a
 * data-structure created on stack at the beginning of reiser4 entry. In this
 * mode, list of all "active" contexts is maintained, and periodically
 * checked. This is to catch various hard-to-debug bugs like exiting without
 * destroying context, or stack overflowing.
 *
 * Note: this is not available through fs/Kconfig. Adjust manually.
 */
#define REISER4_DEBUG_CONTEXTS (1)
#else
#define REISER4_DEBUG_CONTEXTS (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_SIBLING_LIST) && defined(CONFIG_REISER4_DEBUG)
/*
 * Turn on sibling-list debugging. In this mode consistency of sibling lists
 * of reiser4 internal tree is checked.
 *
 * Note: this is not available through fs/Kconfig. Adjust manually.
 */
#define REISER4_DEBUG_SIBLING_LIST (1)
#else
#define REISER4_DEBUG_SIBLING_LIST (0)
#endif

#if defined(CONFIG_CRYPTO_DEFLATE)
#define REISER4_GZIP_TFM (1)
#else
#define REISER4_GZIP_TFM (0)
#endif

/*
 * PLEASE update fs/reiser4/kattr.c:show_options() when adding new compilation
 * option
 */


extern const char *REISER4_SUPER_MAGIC_STRING;
extern const int REISER4_MAGIC_OFFSET;	/* offset to magic string from the
					 * beginning of device */

/* here go tunable parameters that are not worth special entry in kernel
   configuration */

/* default number of slots in coord-by-key caches */
#define CBK_CACHE_SLOTS    (16)
/* how many elementary tree operation to carry on the next level */
#define CARRIES_POOL_SIZE        (5)
/* size of pool of preallocated nodes for carry process. */
#define NODES_LOCKED_POOL_SIZE   (5)

#define REISER4_NEW_NODE_FLAGS (COPI_LOAD_LEFT | COPI_LOAD_RIGHT | COPI_GO_LEFT)
#define REISER4_NEW_EXTENT_FLAGS (COPI_LOAD_LEFT | COPI_LOAD_RIGHT | COPI_GO_LEFT)
#define REISER4_PASTE_FLAGS (COPI_GO_LEFT)
#define REISER4_INSERT_FLAGS (COPI_GO_LEFT)

/* we are supporting reservation of disk space on uid basis */
#define REISER4_SUPPORT_UID_SPACE_RESERVATION (0)
/* we are supporting reservation of disk space for groups */
#define REISER4_SUPPORT_GID_SPACE_RESERVATION (0)
/* we are supporting reservation of disk space for root */
#define REISER4_SUPPORT_ROOT_SPACE_RESERVATION (0)
/* we use rapid flush mode, see flush.c for comments.  */
#define REISER4_USE_RAPID_FLUSH (1)

/*
 * set this to 0 if you don't want to use wait-for-flush in ->writepage().
 */
#define REISER4_USE_ENTD (1)

/* Using of emergency flush is an option. */
#define REISER4_USE_EFLUSH (1)

/* key allocation is Plan-A */
#define REISER4_PLANA_KEY_ALLOCATION (1)
/* key allocation follows good old 3.x scheme */
#define REISER4_3_5_KEY_ALLOCATION (0)

/* size of hash-table for znodes */
#define REISER4_ZNODE_HASH_TABLE_SIZE (1 << 13)

/* number of buckets in lnode hash-table */
#define LNODE_HTABLE_BUCKETS (1024)

/* some ridiculously high maximal limit on height of znode tree. This
    is used in declaration of various per level arrays and
    to allocate stattistics gathering array for per-level stats. */
#define REISER4_MAX_ZTREE_HEIGHT     (8)

#define REISER4_PANIC_MSG_BUFFER_SIZE (1024)

/* If array contains less than REISER4_SEQ_SEARCH_BREAK elements then,
   sequential search is on average faster than binary. This is because
   of better optimization and because sequential search is more CPU
   cache friendly. This number (25) was found by experiments on dual AMD
   Athlon(tm), 1400MHz.

   NOTE: testing in kernel has shown that binary search is more effective than
   implied by results of the user level benchmarking. Probably because in the
   node keys are separated by other data. So value was adjusted after few
   tests. More thorough tuning is needed.
*/
#define REISER4_SEQ_SEARCH_BREAK      (3)

/* don't allow tree to be lower than this */
#define REISER4_MIN_TREE_HEIGHT       (TWIG_LEVEL)

/* NOTE NIKITA this is no longer used: maximal atom size is auto-adjusted to
 * available memory. */
/* Default value of maximal atom size. Can be ovewritten by
   tmgr.atom_max_size mount option. By default infinity. */
#define REISER4_ATOM_MAX_SIZE         ((unsigned)(~0))

/* Default value of maximal atom age (in jiffies). After reaching this age
   atom will be forced to commit, either synchronously or asynchronously. Can
   be overwritten by tmgr.atom_max_age mount option. */
#define REISER4_ATOM_MAX_AGE          (600 * HZ)

/* sleeping period for ktxnmrgd */
#define REISER4_TXNMGR_TIMEOUT  (5 * HZ)

/* timeout to wait for ent thread in writepage. Default: 3 milliseconds. */
#define REISER4_ENTD_TIMEOUT (3 * HZ / 1000)

/* start complaining after that many restarts in coord_by_key().

   This either means incredibly heavy contention for this part of a tree, or
   some corruption or bug.
*/
#define REISER4_CBK_ITERATIONS_LIMIT  (100)

/* return -EIO after that many iterations in coord_by_key().

   I have witnessed more than 800 iterations (in 30 thread test) before cbk
   finished. --nikita
*/
#define REISER4_MAX_CBK_ITERATIONS    ((unsigned)~0)

/* put a per-inode limit on maximal number of directory entries with identical
   keys in hashed directory.

   Disable this until inheritance interfaces stabilize: we need some way to
   set per directory limit.
*/
#define REISER4_USE_COLLISION_LIMIT    (0)

/* If flush finds more than FLUSH_RELOCATE_THRESHOLD adjacent dirty leaf-level blocks it
   will force them to be relocated. */
#define FLUSH_RELOCATE_THRESHOLD 64
/* If flush finds can find a block allocation closer than at most FLUSH_RELOCATE_DISTANCE
   from the preceder it will relocate to that position. */
#define FLUSH_RELOCATE_DISTANCE  64

/* If we have written this much or more blocks before encountering busy jnode
   in flush list - abort flushing hoping that next time we get called
   this jnode will be clean already, and we will save some seeks. */
#define FLUSH_WRITTEN_THRESHOLD 50

/* The maximum number of nodes to scan left on a level during flush. */
#define FLUSH_SCAN_MAXNODES 10000

/* default tracing buffer size */
#define REISER4_TRACE_BUF_SIZE (1 << 15)

/* what size units of IO we would like cp, etc., to use, in writing to
   reiser4. In bytes.

   Can be overwritten by optimal_io_size mount option.
*/
#define REISER4_OPTIMAL_IO_SIZE (64 * 1024)

/* see comments in inode.c:oid_to_uino() */
#define REISER4_UINO_SHIFT (1 << 30)

/* Mark function argument as unused to avoid compiler warnings. */
#define UNUSED_ARG __attribute__((unused))

#if ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 3)
#define NONNULL __attribute__((nonnull))
#else
#define NONNULL
#endif

/* master super block offset in bytes.*/
#define REISER4_MASTER_OFFSET 65536

/* size of VFS block */
#define VFS_BLKSIZE 512
/* number of bits in size of VFS block (512==2^9) */
#define VFS_BLKSIZE_BITS 9

#define REISER4_I reiser4_inode_data

/* implication */
#define ergo( antecedent, consequent ) ( !( antecedent ) || ( consequent ) )
/* logical equivalence */
#define equi( p1, p2 ) ( ergo( ( p1 ), ( p2 ) ) && ergo( ( p2 ), ( p1 ) ) )

#define sizeof_array(x) ((int) (sizeof(x) / sizeof(x[0])))


#define NOT_YET                       (0)

/** Reiser4 specific error codes **/

#define REISER4_ERROR_CODE_BASE 500

/* Neighbor is not available (side neighbor or parent) */
#define E_NO_NEIGHBOR  (REISER4_ERROR_CODE_BASE)

/* Node was not found in cache */
#define E_NOT_IN_CACHE (REISER4_ERROR_CODE_BASE + 1)

/* node has no free space enough for completion of balancing operation */
#define E_NODE_FULL    (REISER4_ERROR_CODE_BASE + 2)

/* repeat operation */
#define E_REPEAT       (REISER4_ERROR_CODE_BASE + 3)

/* deadlock happens */
#define E_DEADLOCK     (REISER4_ERROR_CODE_BASE + 4)

/* operation cannot be performed, because it would block and non-blocking mode
 * was requested. */
#define E_BLOCK        (REISER4_ERROR_CODE_BASE + 5)

/* wait some event (depends on context), then repeat */
#define E_WAIT         (REISER4_ERROR_CODE_BASE + 6)

#endif				/* __REISER4_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
