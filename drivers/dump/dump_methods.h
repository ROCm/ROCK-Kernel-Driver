/*
 * Generic interfaces for flexible system dump
 *
 * Started: Oct 2002 -  Suparna Bhattacharya (suparna@in.ibm.com)
 *
 * Copyright (C) 2002 International Business Machines Corp.
 *
 * This code is released under version 2 of the GNU GPL.
 */

#ifndef _LINUX_DUMP_METHODS_H
#define _LINUX_DUMP_METHODS_H

/*
 * Inspired by Matt Robinson's suggestion of introducing dump
 * methods as a way to enable different crash dump facilities to
 * coexist where each employs its own scheme or dumping policy.
 *
 * The code here creates a framework for flexible dump by defining
 * a set of methods and providing associated helpers that differentiate
 * between the underlying mechanism (how to dump), overall scheme
 * (sequencing of stages and data dumped and associated quiescing),
 * output format (what the dump output looks like), target type
 * (where to save the dump; see dumpdev.h), and selection policy
 * (state/data to dump).
 *
 * These sets of interfaces can be mixed and matched to build a
 * dumper suitable for a given situation, allowing for
 * flexibility as well appropriate degree of code reuse.
 * For example all features and options of lkcd (including
 * granular selective dumping in the near future) should be
 * available even when say, the 2 stage soft-boot based mechanism
 * is used for taking disruptive dumps.
 *
 * Todo: Additionally modules or drivers may supply their own
 * custom dumpers which extend dump with module specific
 * information or hardware state, and can even tweak the
 * mechanism when it comes to saving state relevant to
 * them.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/dumpdev.h>
#include <asm/page.h>	/* get_order */

#define MAX_PASSES 	6
#define MAX_DEVS	4
#define MAX_MBANKS_PER_NODE	8
#define MAX_MBANKS	(MAX_NUMNODES * MAX_MBANKS_PER_NODE)


/* To customise selection of pages to be dumped in a given pass/group */
struct dump_data_filter{
	char name[32];
	int (*selector)(int, unsigned long, unsigned long);
	ulong level_mask; /* dump level(s) for which this filter applies */
	loff_t start[MAX_MBANKS], end[MAX_MBANKS]; /* location range applicable */
	ulong num_mbanks;  /* Number of memory banks. Greater than one for discontig memory (NUMA) */
};


/*
 * Determined by the kind of dump mechanism and appropriate
 * overall scheme
 */
struct dump_scheme_ops {
	/* sets aside memory, inits data structures etc */
	int (*configure)(const char *devid);
	/* releases  resources */
	int (*unconfigure)(void);

	/* ordering of passes, invoking iterator */
	int (*sequencer)(void);
        /* iterates over system data, selects and acts on data to dump */
	int (*iterator)(int, int (*)(unsigned long, unsigned long),
		struct dump_data_filter *);
        /* action when data is selected for dump */
	int (*save_data)(unsigned long, unsigned long);
        /* action when data is to be excluded from dump */
	int (*skip_data)(unsigned long, unsigned long);
	/* policies for space, multiple dump devices etc */
	int (*write_buffer)(void *, unsigned long);
};

struct dump_scheme {
	/* the name serves as an anchor to locate the scheme after reboot */
	char name[32];
	struct dump_scheme_ops *ops;
	struct list_head list;
};

/* Quiescing/Silence levels (controls IPI callback behaviour) */
extern enum dump_silence_levels {
	DUMP_SOFT_SPIN_CPUS	= 1,
	DUMP_HARD_SPIN_CPUS	= 2,
	DUMP_HALT_CPUS		= 3,
} dump_silence_level;

/* determined by the dump (file) format */
struct dump_fmt_ops {
	/* build header */
	int (*configure_header)(const char *, const struct pt_regs *);
	int (*update_header)(void); /* update header and write it out */
	/* save curr context  */
	void (*save_context)(int, const struct pt_regs *,
		struct task_struct *);
	/* typically called by the save_data action */
	/* add formatted data to the dump buffer */
	int (*add_data)(unsigned long, unsigned long);
	int (*update_end_marker)(void);
};

struct dump_fmt {
	unsigned long magic;
	char name[32];  /* lcrash, crash, elf-core etc */
	struct dump_fmt_ops *ops;
	struct list_head list;
};

/*
 * Modules will be able add their own data capture schemes by
 * registering their own dumpers. Typically they would use the
 * primary dumper as a template and tune it with their routines.
 * Still Todo.
 */

/* The combined dumper profile (mechanism, scheme, dev, fmt) */
struct dumper {
	char name[32]; /* singlestage, overlay (stg1), passthru(stg2), pull */
	struct dump_scheme *scheme;
	struct dump_fmt *fmt;
	struct __dump_compress *compress;
	struct dump_data_filter *filter;
	struct dump_dev *dev;
	/* state valid only for active dumper(s) - per instance */
	/* run time state/context */
	int curr_pass;
	unsigned long count;
	loff_t curr_offset; /* current logical offset into dump device */
	loff_t curr_loc; /* current memory location */
	void *curr_buf; /* current position in the dump buffer */
	void *dump_buf; /* starting addr of dump buffer */
	int header_dirty; /* whether the header needs to be written out */
	int header_len;
	struct list_head dumper_list; /* links to other dumpers */
};

/* Starting point to get to the current configured state */
struct dump_config {
	ulong level;
	int polling;
	int reboot;
	int comp_flag;
	int comp_val;
	struct dumper *dumper;
	char *dump_device;
	unsigned long dump_addr; /* relevant only for in-memory dumps */
	struct list_head dump_dev_list;
};

extern struct dump_config dump_config;

/* Wrappers that invoke the methods for the current (active) dumper */

/* Scheme operations */

static inline int dump_sequencer(void)
{
	return dump_config.dumper->scheme->ops->sequencer();
}

static inline int dump_iterator(int pass, int (*action)(unsigned long,
	unsigned long), struct dump_data_filter *filter)
{
	return dump_config.dumper->scheme->ops->iterator(pass, action, filter);
}

#define dump_save_data dump_config.dumper->scheme->ops->save_data
#define dump_skip_data dump_config.dumper->scheme->ops->skip_data

static inline int dump_write_buffer(void *buf, unsigned long len)
{
	return dump_config.dumper->scheme->ops->write_buffer(buf, len);
}

static inline int dump_configure(const char *devid)
{
	return dump_config.dumper->scheme->ops->configure(devid);
}

static inline int dump_unconfigure(void)
{
	return dump_config.dumper->scheme->ops->unconfigure();
}

/* Format operations */

static inline int dump_configure_header(const char *panic_str,
	const struct pt_regs *regs)
{
	return dump_config.dumper->fmt->ops->configure_header(panic_str, regs);
}

static inline void dump_save_context(int cpu, const struct pt_regs *regs,
		struct task_struct *tsk)
{
	dump_config.dumper->fmt->ops->save_context(cpu, regs, tsk);
}

static inline int dump_save_this_cpu(const struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	dump_save_context(cpu, regs, current);
	return 1;
}

static inline int dump_update_header(void)
{
	return dump_config.dumper->fmt->ops->update_header();
}

static inline int dump_update_end_marker(void)
{
	return dump_config.dumper->fmt->ops->update_end_marker();
}

static inline int dump_add_data(unsigned long loc, unsigned long sz)
{
	return dump_config.dumper->fmt->ops->add_data(loc, sz);
}

/* Compression operation */
static inline int dump_compress_data(char *src, int slen, char *dst,
		unsigned long loc)
{
	return dump_config.dumper->compress->compress_func(src, slen,
		dst, DUMP_DPC_PAGE_SIZE, loc);
}


/* Prototypes of some default implementations of dump methods */

extern struct __dump_compress dump_none_compression;

/* Default scheme methods (dump_scheme.c) */

extern int dump_generic_sequencer(void);
extern int dump_page_iterator(int pass, int (*action)(unsigned long, unsigned
	long), struct dump_data_filter *filter);
extern int dump_generic_save_data(unsigned long loc, unsigned long sz);
extern int dump_generic_skip_data(unsigned long loc, unsigned long sz);
extern int dump_generic_write_buffer(void *buf, unsigned long len);
extern int dump_generic_configure(const char *);
extern int dump_generic_unconfigure(void);
#ifdef CONFIG_DISCONTIGMEM
extern void dump_reconfigure_mbanks(void);
#endif

/* Default scheme template */
extern struct dump_scheme dump_scheme_singlestage;

/* Default dump format methods */

extern int dump_lcrash_configure_header(const char *panic_str,
	const struct pt_regs *regs);
extern void dump_lcrash_save_context(int  cpu, const struct pt_regs *regs,
	struct task_struct *tsk);
extern int dump_generic_update_header(void);
extern int dump_lcrash_add_data(unsigned long loc, unsigned long sz);
extern int dump_lcrash_update_end_marker(void);

/* Default format (lcrash) template */
extern struct dump_fmt dump_fmt_lcrash;

/* Default dump selection filter table */

/*
 * Entries listed in order of importance and correspond to passes
 * The last entry (with a level_mask of zero) typically reflects data that
 * won't be dumped  -- this may for example be used to identify data
 * that will be skipped for certain so the corresponding memory areas can be
 * utilized as scratch space.
 */
extern struct dump_data_filter dump_filter_table[];

/* Some pre-defined dumpers */
extern struct dumper dumper_singlestage;
extern struct dumper dumper_stage1;
extern struct dumper dumper_stage2;

/* These are temporary */
#define DUMP_MASK_HEADER	DUMP_LEVEL_HEADER
#define DUMP_MASK_KERN		DUMP_LEVEL_KERN
#define DUMP_MASK_USED		DUMP_LEVEL_USED
#define DUMP_MASK_UNUSED	DUMP_LEVEL_ALL_RAM
#define DUMP_MASK_REST		0 /* dummy for now */

/* Helpers - move these to dump.h later ? */

int dump_generic_execute(const char *panic_str, const struct pt_regs *regs);
extern int dump_ll_write(void *buf, unsigned long len);

static inline void dumper_reset(void)
{
	dump_config.dumper->curr_buf = dump_config.dumper->dump_buf;
	dump_config.dumper->curr_loc = 0;
	dump_config.dumper->curr_offset = 0;
	dump_config.dumper->count = 0;
	dump_config.dumper->curr_pass = 0;
}

/*
 * May later be moulded to perform boot-time allocations so we can dump
 * earlier during bootup
 */
static inline void *dump_alloc_mem(unsigned long size)
{
	return (void *) __get_free_pages(GFP_KERNEL, get_order(size));
}

static inline void dump_free_mem(void *buf)
{
	struct page *page;

	/* ignore reserved pages (e.g. post soft boot stage) */
	if (buf && (page = virt_to_page(buf))) {
		if (PageReserved(page))
			return;
	}
	/*
	 * Allocated using __get_free_pages().
	 */
	free_pages((unsigned long)buf,
		get_order(DUMP_BUFFER_SIZE + 3 * DUMP_PAGE_SIZE));
}


#endif /*  _LINUX_DUMP_METHODS_H */
