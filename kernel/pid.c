/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002 William Irwin, IBM
 * (C) 2002 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/hash.h>

#define pid_hashfn(nr) hash_long((unsigned long)nr, pidhash_shift)
static struct list_head *pid_hash[PIDTYPE_MAX];
static int pidhash_shift;

int pid_max = PID_MAX_DEFAULT;
int last_pid;

#define RESERVED_PIDS		300

#define PIDMAP_ENTRIES		(PID_MAX_LIMIT/PAGE_SIZE/8)
#define BITS_PER_PAGE		(PAGE_SIZE*8)
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
typedef struct pidmap {
	atomic_t nr_free;
	void *page;
} pidmap_t;

static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static pidmap_t *map_limit = pidmap_array + PIDMAP_ENTRIES;

static spinlock_t pidmap_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

fastcall void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

/*
 * Here we search for the next map that has free bits left.
 * Normally the next map has free PIDs.
 */
static inline pidmap_t *next_free_map(pidmap_t *map, int *max_steps)
{
	while (--*max_steps) {
		if (++map == map_limit)
			map = pidmap_array;
		if (unlikely(!map->page)) {
			unsigned long page = get_zeroed_page(GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			spin_lock(&pidmap_lock);
			if (map->page)
				free_page(page);
			else
				map->page = (void *)page;
			spin_unlock(&pidmap_lock);

			if (!map->page)
				break;
		}
		if (atomic_read(&map->nr_free))
			return map;
	}
	return NULL;
}

int alloc_pidmap(void)
{
	int pid, offset, max_steps = PIDMAP_ENTRIES + 1;
	pidmap_t *map;

	pid = last_pid + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;

	offset = pid & BITS_PER_PAGE_MASK;
	map = pidmap_array + pid / BITS_PER_PAGE;

	if (likely(map->page && !test_and_set_bit(offset, map->page))) {
		/*
		 * There is a small window for last_pid updates to race,
		 * but in that case the next allocation will go into the
		 * slowpath and that fixes things up.
		 */
return_pid:
		atomic_dec(&map->nr_free);
		last_pid = pid;
		return pid;
	}
	
	if (!offset || !atomic_read(&map->nr_free)) {
next_map:
		map = next_free_map(map, &max_steps);
		if (!map)
			goto failure;
		offset = 0;
	}
	/*
	 * Find the next zero bit:
	 */
scan_more:
	offset = find_next_zero_bit(map->page, BITS_PER_PAGE, offset);
	if (offset >= BITS_PER_PAGE)
		goto next_map;
	if (test_and_set_bit(offset, map->page))
		goto scan_more;

	/* we got the PID: */
	pid = (map - pidmap_array) * BITS_PER_PAGE + offset;
	goto return_pid;

failure:
	return -1;
}

fastcall struct pid *find_pid(enum pid_type type, int nr)
{
	struct list_head *elem, *bucket = &pid_hash[type][pid_hashfn(nr)];
	struct pid *pid;

	__list_for_each(elem, bucket) {
		pid = list_entry(elem, struct pid, hash_chain);
		if (pid->nr == nr)
			return pid;
	}
	return NULL;
}

void fastcall link_pid(task_t *task, struct pid_link *link, struct pid *pid)
{
	atomic_inc(&pid->count);
	list_add_tail(&link->pid_chain, &pid->task_list);
	link->pidptr = pid;
}

int fastcall attach_pid(task_t *task, enum pid_type type, int nr)
{
	struct pid *pid = find_pid(type, nr);

	if (pid)
		atomic_inc(&pid->count);
	else {
		pid = &task->pids[type].pid;
		pid->nr = nr;
		atomic_set(&pid->count, 1);
		INIT_LIST_HEAD(&pid->task_list);
		pid->task = task;
		get_task_struct(task);
		list_add(&pid->hash_chain, &pid_hash[type][pid_hashfn(nr)]);
	}
	list_add_tail(&task->pids[type].pid_chain, &pid->task_list);
	task->pids[type].pidptr = pid;

	return 0;
}

static inline int __detach_pid(task_t *task, enum pid_type type)
{
	struct pid_link *link = task->pids + type;
	struct pid *pid = link->pidptr;
	int nr;

	list_del(&link->pid_chain);
	if (!atomic_dec_and_test(&pid->count))
		return 0;

	nr = pid->nr;
	list_del(&pid->hash_chain);
	put_task_struct(pid->task);

	return nr;
}

static void _detach_pid(task_t *task, enum pid_type type)
{
	__detach_pid(task, type);
}

void fastcall detach_pid(task_t *task, enum pid_type type)
{
	int nr = __detach_pid(task, type);

	if (!nr)
		return;

	for (type = 0; type < PIDTYPE_MAX; ++type)
		if (find_pid(type, nr))
			return;
	free_pidmap(nr);
}

task_t *find_task_by_pid(int nr)
{
	struct pid *pid = find_pid(PIDTYPE_PID, nr);

	if (!pid)
		return NULL;
	return pid_task(pid->task_list.next, PIDTYPE_PID);
}

EXPORT_SYMBOL(find_task_by_pid);

/*
 * This function switches the PIDs if a non-leader thread calls
 * sys_execve() - this must be done without releasing the PID.
 * (which a detach_pid() would eventually do.)
 */
void switch_exec_pids(task_t *leader, task_t *thread)
{
	_detach_pid(leader, PIDTYPE_PID);
	_detach_pid(leader, PIDTYPE_TGID);
	_detach_pid(leader, PIDTYPE_PGID);
	_detach_pid(leader, PIDTYPE_SID);

	_detach_pid(thread, PIDTYPE_PID);
	_detach_pid(thread, PIDTYPE_TGID);

	leader->pid = leader->tgid = thread->pid;
	thread->pid = thread->tgid;

	attach_pid(thread, PIDTYPE_PID, thread->pid);
	attach_pid(thread, PIDTYPE_TGID, thread->tgid);
	attach_pid(thread, PIDTYPE_PGID, thread->signal->pgrp);
	attach_pid(thread, PIDTYPE_SID, thread->signal->session);
	list_add_tail(&thread->tasks, &init_task.tasks);

	attach_pid(leader, PIDTYPE_PID, leader->pid);
	attach_pid(leader, PIDTYPE_TGID, leader->tgid);
	attach_pid(leader, PIDTYPE_PGID, leader->signal->pgrp);
	attach_pid(leader, PIDTYPE_SID, leader->signal->session);
}

/*
 * The pid hash table is scaled according to the amount of memory in the
 * machine.  From a minimum of 16 slots up to 4096 slots at one gigabyte or
 * more.
 */
void __init pidhash_init(void)
{
	int i, j, pidhash_size;
	unsigned long megabytes = max_pfn >> (20 - PAGE_SHIFT);

	pidhash_shift = max(4, fls(megabytes * 4));
	pidhash_shift = min(12, pidhash_shift);
	pidhash_size = 1 << pidhash_shift;

	printk("PID hash table entries: %d (order %d: %Zd bytes)\n",
		pidhash_size, pidhash_shift,
		pidhash_size * sizeof(struct list_head));

	for (i = 0; i < PIDTYPE_MAX; i++) {
		pid_hash[i] = alloc_bootmem(pidhash_size *
					sizeof(struct list_head));
		if (!pid_hash[i])
			panic("Could not alloc pidhash!\n");
		for (j = 0; j < pidhash_size; j++)
			INIT_LIST_HEAD(&pid_hash[i][j]);
	}
}

void __init pidmap_init(void)
{
	int i;

	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	set_bit(0, pidmap_array->page);
	atomic_dec(&pidmap_array->nr_free);

	/*
	 * Allocate PID 0, and hash it via all PID types:
	 */

	for (i = 0; i < PIDTYPE_MAX; i++)
		attach_pid(current, i, 0);
}
