/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* profiling facilities. */

/*
 * This code is used to collect statistics about how many times particular
 * function (or part of function) was called, and how long average call
 * took. In addition (or, in the first place, depending on one's needs), it
 * also keep track of through what call-chain profiled piece of code was
 * entered. Latter is done by having a list of call-chains. Call-chains are
 * obtained by series of calls to __builtin_return_address() (hence, this
 * functionality requires kernel to be compiled with frame pointers). Whenever
 * profiled region is just about to be left, call-chain is constructed and
 * then compared against all chains already in the list. If match is found
 * (cache hit!), its statistics are updated, otherwise (cache miss), entry
 * with smallest hit count is selected and re-used to new call-chain.
 *
 * NOTE: this replacement policy has obvious deficiencies: after some time
 * entries in the list accumulate high hit counts and will effectively prevent
 * any new call-chain from finding a place in the list, even is this
 * call-chain is frequently activated. Probably LRU should be used instead
 * (this is not that hard, /proc/<pid>/sleep patch does this), but nobody
 * complained so far.
 *
 */


#include "kattr.h"
#include "reiser4.h"
#include "context.h"
#include "super.h"
#include "prof.h"

#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>

#if REISER4_PROF

#ifdef CONFIG_FRAME_POINTER
static void
update_prof_trace(reiser4_prof_cnt *cnt, int depth, int shift)
{
	int i;
	int minind;
	__u64 minhit;
	unsigned long hash;
	backtrace_path bt;

	fill_backtrace(&bt, depth, shift);

	for (i = 0, hash = 0 ; i < REISER4_BACKTRACE_DEPTH ; ++ i) {
		hash += (unsigned long)bt.trace[i];
	}
	minhit = ~0ull;
	minind = 0;
	for (i = 0 ; i < REISER4_PROF_TRACE_NUM ; ++ i) {
		if (hash == cnt->bt[i].hash) {
			++ cnt->bt[i].hits;
			return;
		}
		if (cnt->bt[i].hits < minhit) {
			minhit = cnt->bt[i].hits;
			minind = i;
		}
	}
	cnt->bt[minind].path = bt;
	cnt->bt[minind].hash = hash;
	cnt->bt[minind].hits = 1;
}
#else
#define update_prof_trace(cnt, depth, shift) noop
#endif

void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now,
		     unsigned long swtch_mark, __u64 start_jif,
		     int depth, int shift)
{
	__u64 delta;

	delta = now - then;
	cnt->nr ++;
	cnt->total += delta;
	cnt->max = max(cnt->max, delta);
	if (swtch_mark == nr_context_switches()) {
		cnt->noswtch_nr ++;
		cnt->noswtch_total += delta;
		cnt->noswtch_max = max(cnt->noswtch_max, delta);
	}
	update_prof_trace(cnt, depth, shift);
}

struct prof_attr_entry {
	struct attribute attr;
	char name[10];
};

static struct prof_attr_entry prof_attr[REISER4_PROF_TRACE_NUM];

static ssize_t
show_prof_attr(struct kobject *kobj, struct attribute *attr, char *buf)
{
	char *p;
	reiser4_prof_entry *entry;
	reiser4_prof_cnt   *val;
#ifdef CONFIG_FRAME_POINTER
	int pos;
	int j;

	pos = ((struct prof_attr_entry *)attr) - prof_attr;
#endif
	entry = container_of(kobj, reiser4_prof_entry, kobj);
	val = &entry->cnt;
	p = buf;
	KATTR_PRINT(p, buf, "%llu %llu %llu %llu %llu %llu\n",
		    val->nr, val->total, val->max,
		    val->noswtch_nr, val->noswtch_total, val->noswtch_max);
#ifdef CONFIG_FRAME_POINTER
	if (val->bt[pos].hash != 0) {
		KATTR_PRINT(p, buf, "\t%llu: ", val->bt[pos].hits);
		for (j = 0 ; j < REISER4_BACKTRACE_DEPTH ; ++ j) {
			char         *module;
			const char   *name;
			char          namebuf[128];
			unsigned long address;
			unsigned long offset;
			unsigned long size;

			address = (unsigned long) val->bt[pos].path.trace[j];
			name = kallsyms_lookup(address, &size,
					       &offset, &module, namebuf);
			KATTR_PRINT(p, buf, "\n\t\t%#lx ", address);
			if (name != NULL)
				KATTR_PRINT(p, buf, "%s+%#lx/%#lx",
					    name, offset, size);
		}
		KATTR_PRINT(p, buf, "\n");
	}
#endif
	return (p - buf);
}

/* zero a prof entry corresponding to @attr */
static ssize_t
store_prof_attr(struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	reiser4_prof_entry *entry;

	entry = container_of(kobj, reiser4_prof_entry, kobj);
	memset(&entry->cnt, 0, sizeof(reiser4_prof_cnt));
	return sizeof(reiser4_prof_cnt);
}

static struct sysfs_ops prof_attr_ops = {
	.show = show_prof_attr,
	.store = store_prof_attr
};

static struct kobj_type ktype_reiser4_prof = {
	.sysfs_ops	= &prof_attr_ops,
	.default_attrs	= NULL
};

static decl_subsys(prof, &ktype_reiser4_prof, NULL);

static struct kobject cpu_prof;

#define DEFINE_PROF_ENTRY_0(attr_name,field_name)	\
	.field_name = {					\
		.kobj = {	       			\
			.name = attr_name	\
		}					\
	}


#define DEFINE_PROF_ENTRY(name)				\
 	DEFINE_PROF_ENTRY_0(#name,name)

reiser4_prof reiser4_prof_defs = {
	DEFINE_PROF_ENTRY(fuse_wait),
#if 0
	DEFINE_PROF_ENTRY(cbk),
	DEFINE_PROF_ENTRY(init_context),
	DEFINE_PROF_ENTRY(jlook),
	DEFINE_PROF_ENTRY(writepage),
	DEFINE_PROF_ENTRY(jload),
	DEFINE_PROF_ENTRY(jrelse),
	DEFINE_PROF_ENTRY(flush_alloc),
	DEFINE_PROF_ENTRY(forward_squalloc),
	DEFINE_PROF_ENTRY(atom_wait_event),
	DEFINE_PROF_ENTRY(zget),
	/* write profiling */
	DEFINE_PROF_ENTRY(extent_write),
	/* read profiling */
	DEFINE_PROF_ENTRY(file_read)
#endif
};

void calibrate_prof(void)
{
	__u64 start;
	__u64 end;

	rdtscll(start);
	schedule_timeout(HZ/100);
	rdtscll(end);
	warning("nikita-2923", "1 sec. == %llu rdtsc.", (end - start) * 100);
}


int init_prof_kobject(void)
{
	int result;
	int i;
	reiser4_prof_entry *array;

	for (i = 0; i < REISER4_PROF_TRACE_NUM; ++ i) {
		sprintf(prof_attr[i].name, "%i", i);
		prof_attr[i].attr.name = prof_attr[i].name;
		prof_attr[i].attr.mode = 0644;
	}

	result = subsystem_register(&prof_subsys);
	if (result != 0)
		return result;

	cpu_prof.kset = &prof_subsys.kset;
	snprintf(cpu_prof.name, KOBJ_NAME_LEN, "cpu_prof");
	result = kobject_register(&cpu_prof);
	if (result != 0)
		return result;

	/* populate */
	array = (reiser4_prof_entry *)&reiser4_prof_defs;
	for(i = 0 ; i < sizeof(reiser4_prof_defs)/sizeof(reiser4_prof_entry);
	    ++ i) {
		struct kobject *kobj;
		int j;

		kobj = &array[i].kobj;
		kobj->ktype = &ktype_reiser4_prof;
		kobj->parent = kobject_get(&cpu_prof);

		result = kobject_register(kobj);
		if (result != 0)
			break;

		for (j = 0; j < REISER4_PROF_TRACE_NUM; ++ j) {
			result = sysfs_create_file(kobj, &prof_attr[j].attr);
			if (result != 0)
				break;
		}
	}
	if (result != 0)
		kobject_unregister(&cpu_prof);
	return result;
}

void done_prof_kobject(void)
{
	kobject_unregister(&cpu_prof);
	subsystem_unregister(&prof_subsys);
}

/* REISER4_PROF */
#else

/* REISER4_PROF */
#endif
