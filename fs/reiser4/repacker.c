/* Copyright 2003 by Hans Reiser */

/*
   The reiser4 repacker.

   It walks the reiser4 tree and marks all nodes (reads them if it is
   necessary) for repacking by setting JNODE_REPACK bit. Also, all nodes which
   have no JNODE_REPACK bit set nodes added to a transaction and marked dirty.
*/

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/suspend.h>

#include <asm/atomic.h>

#include "reiser4.h"
#include "kattr.h"
#include "super.h"
#include "tree.h"
#include "tree_walk.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"

#include "plugin/item/extent.h"

#include <linux/spinlock.h>
#include "kcond.h"

#include "repacker.h"

/* The reiser4 repacker process nodes by chunks of REPACKER_CHUNK_SIZE
 * size. */
#define REPACKER_DEFAULT_CHUNK_SIZE 512

enum repacker_state_bits {
	REPACKER_RUNNING       = 0x1,
	REPACKER_STOP          = 0x2,
	REPACKER_DESTROY       = 0x4,
	REPACKER_GOES_BACKWARD = 0x8
};

/* Per super block repacker structure for  */
struct repacker {
	/* Back reference to a super block. */
	struct super_block * super;
	/* Repacker thread state */
	enum repacker_state_bits  state;
	/* A spin lock to protect state */
	spinlock_t guard;
	/* A conditional variable to wait repacker state change. */
	kcond_t    cond;
#if REISER4_USE_SYSFS
	/* An object (kobject), externally visible through SysFS. */
	struct kobject kobj;
#endif
	struct {
		reiser4_key start_key;
		reiser4_block_nr chunk_size;
		reiser4_block_nr count;
	} params;
};

/* A thread-safe repacker check state bit routine.  */
static inline int check_repacker_state_bit(struct repacker *repacker, enum repacker_state_bits bits)
{
	int result;

	spin_lock(&repacker->guard);
	result = !!(repacker->state & bits);
	spin_unlock(&repacker->guard);

	return result;
}

static int check_repacker_state (struct repacker * repacker)
{
	if (check_repacker_state_bit(
		    get_current_super_private()->repacker, REPACKER_STOP))
		return -EINTR;
	if (current->flags & PF_FREEZE)
		return -E_REPEAT;
	if (current_atom_should_commit())
		return -E_REPEAT;
	return 0;
}

static void repacker_cursor_init (struct repacker_cursor * cursor, struct repacker * repacker)
{
	int backward = check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD);

	xmemset(cursor, 0, sizeof (struct repacker_cursor));

	blocknr_hint_init(&cursor->hint);
	cursor->hint.backward = backward;

	if (backward)
		cursor->hint.blk = get_current_super_private()->block_count - 1;
	else
		cursor->hint.blk = 0;
}

static void repacker_cursor_done (struct repacker_cursor * cursor)
{
	blocknr_hint_done(&cursor->hint);
}

/* routines for closing current transaction and beginning new one */

static int end_work (void)
{
	reiser4_context * ctx = get_current_context();

	txn_end(ctx);
	return 0;
}
static void begin_work (void)
{
	reiser4_context * ctx = get_current_context();
	preempt_point();
	txn_begin(ctx);
}

/* Processing of a formatted node when the repacker goes forward. */
static int process_znode_forward (tap_t * tap, void * arg)
{
	struct repacker_cursor * cursor = arg;
	znode * node = tap->lh->node;
	int ret;

	assert("zam-954", cursor->count > 0);

	ret = check_repacker_state(get_current_super_private()->repacker);
	if (ret)
		return ret;

	if (ZF_ISSET(node, JNODE_REPACK))
		return 0;

	if (current_atom_should_commit())
		return -E_REPEAT;

	znode_make_dirty(node);
	ZF_SET(node, JNODE_REPACK);

	cursor->stats.znodes_dirtied ++;

	if (-- cursor->count <= 0)
		return -E_REPEAT;
	return 0;
}

/* Processing of unformatted nodes (of one extent unit) when the repacker goes
 * forward. */
static int process_extent_forward (tap_t *tap, void * arg)
{
	int ret;
	struct repacker_cursor * cursor = arg;

	ret = check_repacker_state(get_current_super_private()->repacker);
	if (ret)
		return ret;

	ret = mark_extent_for_repacking(tap, cursor->count);
	if (ret > 0) {
		cursor->stats.jnodes_dirtied += ret;
		cursor->count -= ret;
		if (cursor->count <= 0)
			     return -E_REPEAT;
		return 0;
	}

	return ret;
}


/* It is for calling by tree walker before taking any locks. */
static int prepare_repacking_session (void * arg)
{
	struct repacker_cursor * cursor = arg;
	int ret;

	assert("zam-951", schedulable());

	all_grabbed2free();
	ret = end_work();
	if (ret)
		return ret;

	if (current->flags & PF_FREEZE)
		refrigerator(PF_FREEZE);

	begin_work();
	balance_dirty_pages_ratelimited(get_current_super_private()->fake->i_mapping);
	cursor->count = get_current_super_private()->repacker->params.chunk_size;
	return  reiser4_grab_space((__u64)cursor->count,
				   BA_CAN_COMMIT | BA_FORCE);
}

/* When the repacker goes backward (from the rightmost key to the leftmost
 * one), it does relocation of all processed nodes to the end of disk.  Thus
 * repacker does what usually the reiser4 flush does but in backward direction
 * and node squeezing is not supported. */
static int process_znode_backward (tap_t * tap, void * arg)
{
	lock_handle parent_lock;
	load_count parent_load;
	znode * child = tap->lh->node;
	struct repacker_cursor * cursor = arg;
	__u64 new_blocknr;
	int ret;

	assert("zam-977", (unsigned)(cursor->count) <= get_current_context()->grabbed_blocks);

	/* Add node to current transaction like in processing forward. */
	ret = process_znode_forward(tap, arg);
	if (ret)
		return ret;

	init_lh(&parent_lock);
	ret = reiser4_get_parent(&parent_lock, child, ZNODE_WRITE_LOCK, 0);
	if (ret)
		goto out;

	init_load_count(&parent_load);

	/* Do not relocate nodes which were processed by flush already. */
	if (ZF_ISSET(child, JNODE_RELOC) || ZF_ISSET(child, JNODE_OVRWR))
		goto out;

	if (ZF_ISSET(child, JNODE_CREATED)) {
		assert("zam-962", blocknr_is_fake(znode_get_block(child)));
		cursor->hint.block_stage = BLOCK_UNALLOCATED;
	} else {
		if (znode_get_level(child) == LEAF_LEVEL)
			cursor->hint.block_stage = BLOCK_FLUSH_RESERVED;
		else {
			ret = reiser4_grab_space((__u64)1,
						 BA_FORCE |
						 BA_RESERVED |
						 BA_PERMANENT |
						 BA_FORMATTED);
			if (ret)
				goto out;

			cursor->hint.block_stage = BLOCK_GRABBED;
		}
	}

	{
		__u64 len = 1UL;

		ret = reiser4_alloc_blocks(&cursor->hint, &new_blocknr, &len,
					   BA_PERMANENT | BA_FORMATTED);
		if (ret)
			goto out;

		cursor->hint.blk = new_blocknr;
	}

	if (!ZF_ISSET(child, JNODE_CREATED)) {
		ret = reiser4_dealloc_block(znode_get_block(child), 0,
				    BA_DEFER | BA_PERMANENT | BA_FORMATTED);
		if (ret)
			goto out;
	}

	/* Flush doesn't process nodes twice, it will not discard this block
	 * relocation. */
	ZF_SET(child, JNODE_RELOC);

	/* Update parent reference. */
	if (unlikely(znode_above_root(parent_lock.node))) {
		reiser4_tree * tree = current_tree;
		UNDER_RW_VOID(tree, tree, write, tree->root_block = new_blocknr);
	} else {
		coord_t parent_coord;
		item_plugin *iplug;

		ret = incr_load_count_znode(&parent_load, parent_lock.node);
		if (ret)
			goto out;

		ret = find_child_ptr(parent_lock.node, child, &parent_coord);
		if (ret)
			goto out;

		assert ("zam-960", item_is_internal(&parent_coord));
		assert ("zam-961", znode_is_loaded(child));
		iplug = item_plugin_by_coord(&parent_coord);
		assert("zam-964", iplug->f.update != NULL);
		iplug->f.update(&parent_coord, &new_blocknr);
	}

	znode_make_dirty(parent_lock.node);
	ret = znode_rehash(child, &new_blocknr);

 out:
	done_load_count(&parent_load);
	done_lh(&parent_lock);
	assert("zam-982", (unsigned)(cursor->count) <= get_current_context()->grabbed_blocks);
	return ret;
}

/* Processing of unformatted nodes when the repacker goes backward. */
static int process_extent_backward (tap_t * tap, void * arg)
{
	struct repacker_cursor * cursor = arg;
	int ret;

	assert("zam-978", (unsigned)(cursor->count) <= get_current_context()->grabbed_blocks);

	ret = check_repacker_state(get_current_super_private()->repacker);
	if (ret)
		return ret;

	ret = process_extent_backward_for_repacking(tap, cursor);
	if (ret)
		return ret;
	if (cursor->count <= 0)
		return -E_REPEAT;

	return 0;
}
/* A set of functions to be called by tree_walk in repacker forward pass. */
static struct tree_walk_actor forward_actor = {
	.process_znode  = process_znode_forward,
	.process_extent = process_extent_forward,
	.before         = prepare_repacking_session
};

/* A set of functions to be called by tree_walk in repacker backward pass. */
static struct tree_walk_actor backward_actor = {
	.process_znode  = process_znode_backward,
	.process_extent = process_extent_backward,
	.before         = prepare_repacking_session
};


reiser4_internal int reiser4_repacker (struct repacker * repacker)
{
	struct repacker_cursor cursor;
	int backward;
	struct tree_walk_actor * actor;
	int ret;

	repacker_cursor_init(&cursor, repacker);

	backward = check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD);
	actor = backward ? &backward_actor : &forward_actor;
	ret = tree_walk(NULL, backward, actor, &cursor);
	printk(KERN_INFO "reiser4 repacker: "
	       "%lu formatted node(s) processed, %lu unformatted node(s) processed, ret = %d\n",
	       cursor.stats.znodes_dirtied, cursor.stats.jnodes_dirtied, ret);

	repacker_cursor_done(&cursor);
	return ret;
}

/* The repacker kernel thread code. */
reiser4_internal int repacker_d(void *arg)
{
	struct repacker * repacker = arg;
	struct task_struct * me = current;
	int ret;

	reiser4_context ctx;

	daemonize("k_reiser4_repacker_d");

	/* block all signals */
	spin_lock_irq(&me->sighand->siglock);
	siginitsetinv(&me->blocked, 0);
	recalc_sigpending();
	spin_unlock_irq(&me->sighand->siglock);

	/* zeroing the fs_context copied form parent process' task struct. */
	me->journal_info = NULL;

	printk(KERN_INFO "Repacker: I am alive, pid = %u\n", me->pid);
	ret = init_context(&ctx, repacker->super);
	if (!ret) {
		ret = reiser4_repacker(repacker);
		reiser4_exit_context(&ctx);
	}

	spin_lock(&repacker->guard);
	repacker->state &= ~REPACKER_RUNNING;
	kcond_broadcast(&repacker->cond);
	spin_unlock(&repacker->guard);

	return ret;
}

static void wait_repacker_completion(struct repacker * repacker)
{
	if (repacker->state & REPACKER_RUNNING) {
		kcond_wait(&repacker->cond, &repacker->guard, 0);
		assert("zam-956", !(repacker->state & REPACKER_RUNNING));
	}
}

#if REISER4_USE_SYSFS

static int start_repacker(struct repacker * repacker)
{
	spin_lock(&repacker->guard);
	if (!(repacker->state & REPACKER_DESTROY)) {
		repacker->state &= ~REPACKER_STOP;
		if (!(repacker->state & REPACKER_RUNNING)) {
			repacker->state |= REPACKER_RUNNING;
			spin_unlock(&repacker->guard);
			kernel_thread(repacker_d, repacker, CLONE_VM | CLONE_FS | CLONE_FILES);
			return 0;
		}
	}
	spin_unlock(&repacker->guard);
	return 0;
}

static void stop_repacker(struct repacker * repacker)
{
	spin_lock(&repacker->guard);
	repacker->state |= REPACKER_STOP;
	spin_unlock(&repacker->guard);
}

struct repacker_attr {
	struct attribute attr;
	ssize_t (*show)(struct repacker *, char * buf);
	ssize_t (*store)(struct repacker *, const char * buf, size_t size);
};

static ssize_t start_attr_show (struct repacker * repacker, char * buf)
{
	return snprintf(buf, PAGE_SIZE , "%d", check_repacker_state_bit(repacker, REPACKER_RUNNING));
}

static ssize_t start_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	int start_stop = 0;

	sscanf(buf, "%d", &start_stop);
	if (start_stop)
		start_repacker(repacker);
	else
		stop_repacker(repacker);

	return size;
}

static ssize_t direction_attr_show (struct repacker * repacker, char * buf)
{
	return snprintf(buf, PAGE_SIZE , "%d", check_repacker_state_bit(repacker, REPACKER_GOES_BACKWARD));
}

static ssize_t direction_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	int go_left = 0;

	sscanf(buf, "%d", &go_left);

	spin_lock(&repacker->guard);
	if (!(repacker->state & REPACKER_RUNNING)) {
		if (go_left)
			repacker->state |= REPACKER_GOES_BACKWARD;
		else
			repacker->state &= ~REPACKER_GOES_BACKWARD;
	}
	spin_unlock(&repacker->guard);
	return size;
}

static ssize_t start_key_attr_show (struct repacker * repacker, char * buf)
{
	spin_lock(&repacker->guard);
	spin_unlock(&repacker->guard);

	return 0;
}

static ssize_t start_key_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	spin_lock(&repacker->guard);
	spin_unlock(&repacker->guard);

	return (ssize_t)size;
}

static ssize_t count_attr_show (struct repacker * repacker, char * buf)
{
	__u64 count;

	spin_lock(&repacker->guard);
	count = repacker->params.count;
	spin_unlock(&repacker->guard);

	return snprintf(buf, PAGE_SIZE, "%llu", (unsigned long long)count);
}

static ssize_t count_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	unsigned long long count;

	sscanf(buf, "%Lu", &count);

	spin_lock(&repacker->guard);
	repacker->params.count = (__u64)count;
	spin_unlock(&repacker->guard);

	return (ssize_t)size;
}

static ssize_t chunk_size_attr_show (struct repacker * repacker, char * buf)
{
	__u64 chunk_size;

	spin_lock(&repacker->guard);
	chunk_size = repacker->params.chunk_size;
	spin_unlock(&repacker->guard);

	return snprintf(buf, PAGE_SIZE, "%Lu", (unsigned long long)chunk_size);
}

static ssize_t chunk_size_attr_store (struct repacker * repacker,  const char *buf, size_t size)
{
	unsigned long long chunk_size;

	sscanf(buf, "%Lu", &chunk_size);

	spin_lock(&repacker->guard);
	repacker->params.chunk_size = (__u64)chunk_size;
	spin_unlock(&repacker->guard);

	return (ssize_t)size;
}

#define REPACKER_ATTR(attr_name, perm)			\
static struct repacker_attr attr_name ## _attr = {	\
	.attr = {					\
		.name = # attr_name,			\
		.mode = perm				\
	},						\
	.show = attr_name ## _attr_show,		\
	.store = attr_name ## _attr_store,		\
}

REPACKER_ATTR(start, 0644);
REPACKER_ATTR(direction, 0644);
REPACKER_ATTR(start_key, 0644);
REPACKER_ATTR(count, 0644);
REPACKER_ATTR(chunk_size, 0644);

static struct attribute * repacker_def_attrs[] = {
	&start_attr.attr,
	&direction_attr.attr,
	&start_key_attr.attr,
	&count_attr.attr,
	&chunk_size_attr.attr,
	NULL
};

static ssize_t repacker_attr_show (struct kobject *kobj, struct attribute *attr,  char *buf)
{
	struct repacker_attr * r_attr = container_of(attr, struct repacker_attr, attr);
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);

	return r_attr->show(repacker, buf);
}

static ssize_t repacker_attr_store (struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	struct repacker_attr * r_attr = container_of(attr, struct repacker_attr, attr);
	struct repacker * repacker = container_of(kobj, struct repacker, kobj);

	return r_attr->store(repacker, buf, size);
}

static struct sysfs_ops repacker_sysfs_ops = {
	.show  = repacker_attr_show,
	.store = repacker_attr_store
};

static struct kobj_type repacker_ktype = {
	.sysfs_ops     = &repacker_sysfs_ops,
	.default_attrs = repacker_def_attrs,
	.release       = NULL
};

static int init_repacker_sysfs_interface (struct super_block * s)
{
	int ret = 0;
	reiser4_super_info_data * sinfo = get_super_private(s);
	struct kobject * root = &sinfo->kobj.kobj;
	struct repacker * repacker = sinfo->repacker;

	assert("zam-947", repacker != NULL);

	snprintf(repacker->kobj.name, KOBJ_NAME_LEN, "repacker");
	repacker->kobj.parent = kobject_get(root);
	repacker->kobj.ktype = &repacker_ktype;
	ret = kobject_register(&repacker->kobj);

	return ret;
}

static void done_repacker_sysfs_interface (struct super_block * s)
{
	reiser4_super_info_data * sinfo = get_super_private(s);

	kobject_unregister(&sinfo->repacker->kobj);
}

#else  /* REISER4_USE_SYSFS */

#define init_repacker_sysfs_interface(s) (0)
#define done_repacker_sysfs_interface(s) do{}while(0)

#endif /* REISER4_USE_SYSFS */

reiser4_internal int init_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);

	assert ("zam-946", sinfo->repacker == NULL);
	sinfo->repacker = kmalloc(sizeof (struct repacker), GFP_KERNEL);
	if (sinfo->repacker == NULL)
		return -ENOMEM;
	xmemset(sinfo->repacker, 0, sizeof(struct repacker));
	sinfo->repacker->super = super;

	/* set repacker parameters by default values */
	sinfo->repacker->params.chunk_size = REPACKER_DEFAULT_CHUNK_SIZE;

	spin_lock_init(&sinfo->repacker->guard);
	kcond_init(&sinfo->repacker->cond);

	return init_repacker_sysfs_interface(super);
}

reiser4_internal void done_reiser4_repacker (struct super_block *super)
{
	reiser4_super_info_data * sinfo = get_super_private(super);
	struct repacker * repacker;

	repacker = sinfo->repacker;
	assert("zam-945", repacker != NULL);
	done_repacker_sysfs_interface(super);

	spin_lock(&repacker->guard);
	repacker->state |= (REPACKER_STOP | REPACKER_DESTROY);
	wait_repacker_completion(repacker);
	spin_unlock(&repacker->guard);

	kfree(repacker);
	sinfo->repacker = NULL;
}
