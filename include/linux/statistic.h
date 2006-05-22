/*
 * include/linux/statistic.h
 *
 * Statistics facility
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef STATISTIC_H
#define STATISTIC_H

#define STATISTIC_H_REVISION "$Revision: 1.5 $"

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/sgrb.h>

#define STATISTIC_ROOT_DIR	"statistics"

#define STATISTIC_FILENAME_DATA	"data"
#define STATISTIC_FILENAME_DEF	"definition"
#define STATISTIC_FILENAME_DISCLAIMER "DISCLAIMER"

#define STATISTIC_NAME_SIZE	64

#define STATISTIC_RANGE_MIN	-0x7fffffffffffffffLL
#define STATISTIC_RANGE_MAX	 0x7ffffffffffffffeLL

enum {
	STATISTIC_DEF_NAME,
	STATISTIC_DEF_UNIT,
	STATISTIC_DEF_TYPE_VALUE,
	STATISTIC_DEF_TYPE_RANGE,
	STATISTIC_DEF_TYPE_ARRAY,
	STATISTIC_DEF_TYPE_LIST,
	STATISTIC_DEF_TYPE_RAW,
	STATISTIC_DEF_TYPE_HISTORY,
	STATISTIC_DEF_ON,
	STATISTIC_DEF_OFF,
	STATISTIC_DEF_STARTED,
	STATISTIC_DEF_STOPPED,
	STATISTIC_DEF_RANGEMIN,
	STATISTIC_DEF_RANGEMAX,
	STATISTIC_DEF_SCALE_LIN,
	STATISTIC_DEF_SCALE_LOG2,
	STATISTIC_DEF_ENTRIESMAX,
	STATISTIC_DEF_BASEINT,
	STATISTIC_DEF_HITSMISSED,
	STATISTIC_DEF_HITSOUT,
	STATISTIC_DEF_RESET,
	STATISTIC_DEF_MODE_INC,
	STATISTIC_DEF_MODE_PROD,
	STATISTIC_DEF_MODE_RANGE,
	STATISTIC_DEF_PERIOD,
	STATISTIC_DEF_VOID,
};

struct statistic;
struct statistic_file_private;

typedef void (statistic_release_fn) (struct statistic *);
typedef void (statistic_reset_fn) (struct statistic *);
typedef int (statistic_format_data_fn)
		(struct statistic *, struct statistic_file_private *);
typedef int (statistic_format_def_fn) (struct statistic *, char *);
typedef u64 (statistic_add_fn) (struct statistic *, s64, u64);

struct statistic_entry_list {
	struct list_head	list;
	s64			value;
	u64			hits;
};

struct statistic_entry_raw {
	u64 clock;
	u64 serial;
	s64 value;
	u64 incr;
};

struct statistic_entry_range {
	u32 res;
	u32 num;	/* FIXME: better 64 bit; do_div can't deal with it) */
	s64 acc;
	s64 min;
	s64 max;
};

struct statistic {
	struct list_head		list;
	struct statistic_interface	*interface;
	struct statistic		**stat_ptr;
	statistic_release_fn		*release;
	statistic_reset_fn		*reset;
	statistic_format_data_fn	*format_data;
	statistic_format_def_fn		*format_def;
	statistic_add_fn		*add;
	char	name[STATISTIC_NAME_SIZE];
	char	units[STATISTIC_NAME_SIZE];
	u8	type;
	u8	on;
	u64	started;
	u64	stopped;
	u64	age;
	s64 	range_min;
	s64 	range_max;
	u64	hits_out_of_range;
	union {
		struct {
			/* data */
			u64 hits;
			/* user-writeable */
			int mode;
		} value;
		struct {
			/* data */
			struct statistic_entry_range range;
		} range;
		struct {
			/* data */
			u64 *hits;
			/* user-writeable */
			u32 base_interval;
			u8 scale;
			/* internal */
			u32 entries;
		} array;
		struct {
			/* data */
			struct list_head entry_lh;
			/* user-writeable */
			u32 entries_max;
			/* informational for user */
			u64 hits_missed;
			/* internal */
			u32 entries;
		} list;
		struct {
			/* data */
			struct sgrb rb;
			/* user-writeable */
			u32 entries_max;
			/* internal */
			u64 next_serial;
		} raw;
		struct {
			/* data */
			struct sgrb rb;
			/* user-writeable */
			u32 entries_max;
			int mode;
			u64 period;
			/* internal */
			u64 checkpoint;
			u64 window;
			u8 entry_size;
		} history;
	} data;
};

struct statistic_interface {
	struct list_head	list;
	struct dentry		*debugfs_dir;
	struct dentry		*data_file;
	struct dentry		*def_file;
	struct list_head	statistic_lh;
	struct semaphore	sem;
	spinlock_t		lock;
};

struct statistic_file_private {
	struct list_head read_seg_lh;
	struct list_head write_seg_lh;
	size_t write_seg_total_size;
};

struct statistic_global_data {
	struct dentry		*root_dir;
	struct list_head	interface_lh;
	struct semaphore	sem;
	struct dentry		*disclaimer;
};

#ifdef CONFIG_STATISTICS

#define statistic_lock(interface, flags)	\
		spin_lock_irqsave(&(interface)->lock, flags)
#define statistic_unlock(interface, flags)	\
		spin_unlock_irqrestore(&(interface)->lock, flags)

extern int statistic_interface_create(struct statistic_interface **,
				      const char *);
extern int statistic_interface_remove(struct statistic_interface **);

extern int statistic_create(struct statistic **, struct statistic_interface *,
			    const char *, const char *);
extern int statistic_remove(struct statistic **);

extern int statistic_define_value(struct statistic *, s64, s64, int);
extern int statistic_define_range(struct statistic *, s64, s64);
extern int statistic_define_array(struct statistic *, s64, s64, u32, u8);
extern int statistic_define_list(struct statistic *, s64, s64, u32);
extern int statistic_define_raw(struct statistic *, s64, s64, u32);
extern int statistic_define_history(struct statistic *, s64, s64, u32, u64,
				    int);

extern int statistic_start(struct statistic *);
extern int statistic_stop(struct statistic *);
extern void statistic_reset(struct statistic *);

/**
 * statistic_add - update statistic with (discriminator, increment) pair
 * @stat: statistic
 * @value: discriminator
 * @incr: increment
 *
 * The actual processing of (discriminator, increment) is determined by the
 * the definition applied to the statistic. See the descriptions of the
 * statistic_define_*() routines for details.
 *
 * This variant grabs the lock and should be used when there is _no_ need
 * to make a bunch of updates to various statistics of an interface,
 * including the statistic this update is reported for, atomic
 * in order to be meaningful (get the next coherent state of several
 * statistics).
 *
 * On success, the return value is dependend on which type of accumulation
 * has been applied through the recent definition. Usually, returns the
 * updated total of increments reported for this discriminator, if the
 * defined type of accumulation does this kind of computation.
 *
 * If the struct statistic pointer provided by the caller
 * is NULL (unused), this routine fails, and 0 is returned.
 *
 * If some required memory could not be allocated this routine fails,
 * and 0 is returned.
 *
 * If the discriminator is not valid (out of range), this routine fails,
 * and 0 is returned.
 */
static inline u64 statistic_add(struct statistic *stat, s64 value, u64 incr)
{
	unsigned long flags;
	int retval;

	if (stat->on != STATISTIC_DEF_ON)
		return 0;

	statistic_lock(stat->interface, flags);
	retval = stat->add(stat, value, incr);
	statistic_unlock(stat->interface, flags);

	return retval;
}

/**
 * statistic_add_nolock - a statistic_add() variant
 * @stat: statistic
 * @value: discriminator
 * @incr: increment
 *
 * Same purpose and behavious as statistic_add(). See there for details.
 *
 * Only difference to statistic_add():
 * Lock management is up to the exploiter. Basically, we give exploiters
 * the option to ensure data consistency across all statistics attached
 * to a parent interface by adding several calls to this routine into one
 * critical section protected by stat->interface->lock,
 */
static inline u64 statistic_add_nolock(struct statistic *stat, s64 value,
				       u64 incr)
{
	if (stat->on != STATISTIC_DEF_ON)
		return 0;

#ifdef DEBUG
	assert_spin_locked(&stat->interface->lock);
#endif

	return stat->add(stat, value, incr);
}

/**
 * statistic_inc - a statistic_add() variant
 * @stat: statistic
 * @value: discriminator
 *
 * Same purpose and behaviour as statistic_add(). See there for details.
 * Difference: Increment defaults to 1.
 */
static inline u64 statistic_inc(struct statistic *stat, s64 value)
{
	unsigned long flags;
	int retval;

	if (stat->on != STATISTIC_DEF_ON)
		return 0;

	statistic_lock(stat->interface, flags);
	retval = stat->add(stat, value, 1);
	statistic_unlock(stat->interface, flags);

	return retval;
}

/**
 * statistic_inc_nolock - a statistic_add_nolock() variant
 * @stat: statistic
 * @value: discriminator
 *
 * Same purpose and behaviour as statistic_add_nolock(). See there for details.
 * Difference: Increment defaults to 1.
 */
static inline u64 statistic_inc_nolock(struct statistic *stat, s64 value)
{
	if (stat->on != STATISTIC_DEF_ON)
		return 0;

#ifdef DEBUG
	assert_spin_locked(&stat->interface->lock);
#endif

	return stat->add(stat, value, 1);
}

#else /* CONFIG_STATISTICS */

#define statistic_lock(interface, flags)	do { } while (0)
#define statistic_unlock(interface, flags)	do { } while (0)

static inline int statistic_interface_create(
				struct statistic_interface **interface_ptr,
				const char *name)
{
	return 0;
}

static inline int statistic_interface_remove(
				struct statistic_interface **interface_ptr)
{
	return 0;
}

static inline int statistic_create(struct statistic **stat_ptr,
				   struct statistic_interface *interface,
				   const char *name, const char *units)
{
	return 0;
}

static inline int statistic_remove(struct statistic **stat_ptr)
{
	return 0;
}


static inline int statistic_define_value(struct statistic *stat, s64 range_min,
					 s64 range_max, int mode)
{
	return 0;
}

static inline int statistic_define_range(struct statistic *stat, s64 range_min,
					 s64 range_max)
{
	return 0;
}

static inline int statistic_define_array(struct statistic *stat, s64 range_min,
					 s64 range_max, u32 base_interval,
					 u8 scale)
{
	return 0;
}

static inline int statistic_define_list(struct statistic *stat, s64 range_min,
					s64 range_max, u32 entries_max)
{
	return 0;
}

static inline int statistic_define_raw(struct statistic *stat, s64 range_min,
				       s64 range_max, u32 entries_max)
{
	return 0;
}

static inline int statistic_define_history(struct statistic *stat,
					   s64 range_min, s64 range_max,
					   u32 entries_max, u64 period,
					   int mode)
{
	return 0;
}


static inline int statistic_start(struct statistic *stat)
{
	return 0;
}

static inline int statistic_stop(struct statistic *stat)
{
	return 0;
}

static inline void statistic_reset(struct statistic *stat)
{
}

static inline u64 statistic_add(struct statistic *stat, s64 value, u64 incr)
{
	return 0;
}

static inline u64 statistic_add_nolock(struct statistic *stat, s64 value,
				       u64 incr)
{
	return 0;
}

static inline u64 statistic_inc(struct statistic *stat, s64 value)
{
	return 0;
}

static inline u64 statistic_inc_nolock(struct statistic *stat, s64 value)
{
	return 0;
}

#endif /* CONFIG_STATISTICS */

#endif /* STATISTIC_H */
