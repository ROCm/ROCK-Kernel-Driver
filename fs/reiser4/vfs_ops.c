/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Interface to VFS. Reiser4 {super|export|dentry}_operations are defined
   here. */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/dir/dir.h"
#include "plugin/plugin.h"
#include "plugin/plugin_set.h"
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "log.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"
#include "prof.h"
#include "repacker.h"
#include "init_super.h"
#include "status_flags.h"
#include "flush.h"
#include "dscale.h"

#include <linux/profile.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/quotaops.h>
#include <linux/security.h>
#include <linux/reboot.h>
#include <linux/rcupdate.h>

/* super operations */

static struct inode *reiser4_alloc_inode(struct super_block *super);
static void reiser4_destroy_inode(struct inode *inode);
static void reiser4_drop_inode(struct inode *);
static void reiser4_delete_inode(struct inode *);
static void reiser4_write_super(struct super_block *);
static int reiser4_statfs(struct super_block *, struct kstatfs *);
static int reiser4_show_options(struct seq_file *m, struct vfsmount *mnt);
static void reiser4_sync_inodes(struct super_block *s, struct writeback_control * wbc);

extern struct dentry_operations reiser4_dentry_operation;

struct file_system_type reiser4_fs_type;

/* ->statfs() VFS method in reiser4 super_operations */
static int
reiser4_statfs(struct super_block *super	/* super block of file
						 * system in queried */ ,
	       struct kstatfs *statfs	/* buffer to fill with
					 * statistics */ )
{
	sector_t total;
	sector_t reserved;
	sector_t free;
	sector_t forroot;
	sector_t deleted;
	reiser4_context ctx;

	assert("nikita-408", super != NULL);
	assert("nikita-409", statfs != NULL);

	init_context(&ctx, super);
	reiser4_stat_inc(vfs_calls.statfs);

	statfs->f_type = statfs_type(super);
	statfs->f_bsize = super->s_blocksize;

	/*
	 * 5% of total block space is reserved. This is needed for flush and
	 * for truncates (so that we are able to perform truncate/unlink even
	 * on the otherwise completely full file system). If this reservation
	 * is hidden from statfs(2), users will mistakenly guess that they
	 * have enough free space to complete some operation, which is
	 * frustrating.
	 *
	 * Another possible solution is to subtract ->blocks_reserved from
	 * ->f_bfree, but changing available space seems less intrusive than
	 * letting user to see 5% of disk space to be used directly after
	 * mkfs.
	 */
	total    = reiser4_block_count(super);
	reserved = get_super_private(super)->blocks_reserved;
	deleted  = txnmgr_count_deleted_blocks();
	free     = reiser4_free_blocks(super) + deleted;
	forroot  = reiser4_reserved_blocks(super, 0, 0);

	/* These counters may be in inconsistent state because we take the
	 * values without keeping any global spinlock.  Here we do a sanity
	 * check that free block counter does not exceed the number of all
	 * blocks.  */
	if (free > total)
		free = total;
	statfs->f_blocks = total - reserved;
	/* make sure statfs->f_bfree is never larger than statfs->f_blocks */
	if (free > reserved)
		free -= reserved;
	else
		free = 0;
	statfs->f_bfree = free;

	if (free > forroot)
		free -= forroot;
	else
		free = 0;
	statfs->f_bavail = free;

/* FIXME: Seems that various df implementations are way unhappy by such big numbers.
   So we will leave those as zeroes.
	statfs->f_files = oids_used(super) + oids_free(super);
	statfs->f_ffree = oids_free(super);
*/

	/* maximal acceptable name length depends on directory plugin. */
	assert("nikita-3351", super->s_root->d_inode != NULL);
	statfs->f_namelen = reiser4_max_filename_len(super->s_root->d_inode);
	reiser4_exit_context(&ctx);
	return 0;
}

/* this is called whenever mark_inode_dirty is to be called. Stat-data are
 * updated in the tree. */
reiser4_internal int
reiser4_mark_inode_dirty(struct inode *inode)
{
	assert("vs-1207", is_in_reiser4_context());
	return reiser4_update_sd(inode);
}

/* update inode stat-data by calling plugin */
reiser4_internal int
reiser4_update_sd(struct inode *object)
{
        file_plugin *fplug;

	assert("nikita-2338", object != NULL);
	/* check for read-only file system. */
	if (IS_RDONLY(object))
		return 0;

	fplug = inode_file_plugin(object);
	assert("nikita-2339", fplug != NULL);
	return fplug->write_sd_by_inode(object);
}

/* helper function: increase inode nlink count and call plugin method to save
   updated stat-data.

   Used by link/create and during creation of dot and dotdot in mkdir
*/
reiser4_internal int
reiser4_add_nlink(struct inode *object /* object to which link is added */ ,
		  struct inode *parent /* parent where new entry will be */ ,
		  int write_sd_p	/* true if stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1351", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1445", fplug != NULL);

	/* ask plugin whether it can add yet another link to this
	   object */
	if (!fplug->can_add_link(object))
		return RETERR(-EMLINK);

	assert("nikita-2211", fplug->add_link != NULL);
	/* call plugin to do actual addition of link */
	result = fplug->add_link(object, parent);

	mark_inode_update(object, write_sd_p);

	/* optionally update stat data */
	if (result == 0 && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/* helper function: decrease inode nlink count and call plugin method to save
   updated stat-data.

   Used by unlink/create
*/
reiser4_internal int
reiser4_del_nlink(struct inode *object	/* object from which link is
					 * removed */ ,
		  struct inode *parent /* parent where entry was */ ,
		  int write_sd_p	/* true is stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1349", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1350", fplug != NULL);
	assert("nikita-1446", object->i_nlink > 0);
	assert("nikita-2210", fplug->rem_link != NULL);

	/* call plugin to do actual deletion of link */
	result = fplug->rem_link(object, parent);
	mark_inode_update(object, write_sd_p);
	/* optionally update stat data */
	if (result == 0 && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/* slab for reiser4_dentry_fsdata */
static kmem_cache_t *dentry_fsdata_slab;

/*
 * initializer for dentry_fsdata_slab called during boot or module load.
 */
reiser4_internal int init_dentry_fsdata(void)
{
	dentry_fsdata_slab = kmem_cache_create("dentry_fsdata",
					       sizeof (reiser4_dentry_fsdata),
					       0,
					       SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					       NULL,
					       NULL);
	return (dentry_fsdata_slab == NULL) ? RETERR(-ENOMEM) : 0;
}

/*
 * dual to init_dentry_fsdata(). Called on module unload.
 */
reiser4_internal void done_dentry_fsdata(void)
{
	kmem_cache_destroy(dentry_fsdata_slab);
}


/* Return and lazily allocate if necessary per-dentry data that we
   attach to each dentry. */
reiser4_internal reiser4_dentry_fsdata *
reiser4_get_dentry_fsdata(struct dentry *dentry	/* dentry
						 * queried */ )
{
	assert("nikita-1365", dentry != NULL);

	if (dentry->d_fsdata == NULL) {
		reiser4_stat_inc(vfs_calls.private_data_alloc);
		dentry->d_fsdata = kmem_cache_alloc(dentry_fsdata_slab,
						    GFP_KERNEL);
		if (dentry->d_fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
		xmemset(dentry->d_fsdata, 0, sizeof (reiser4_dentry_fsdata));
	}
	return dentry->d_fsdata;
}

/* opposite to reiser4_get_dentry_fsdata(), returns per-dentry data into slab
 * allocator */
reiser4_internal void
reiser4_free_dentry_fsdata(struct dentry *dentry /* dentry released */ )
{
	if (dentry->d_fsdata != NULL) {
		kmem_cache_free(dentry_fsdata_slab, dentry->d_fsdata);
		dentry->d_fsdata = NULL;
	}
}

/* Release reiser4 dentry. This is d_op->d_release() method. */
static void
reiser4_d_release(struct dentry *dentry /* dentry released */ )
{
	reiser4_free_dentry_fsdata(dentry);
}

/* slab for reiser4_dentry_fsdata */
static kmem_cache_t *file_fsdata_slab;

/*
 * initialize file_fsdata_slab. This is called during boot or module load.
 */
reiser4_internal int init_file_fsdata(void)
{
	file_fsdata_slab = kmem_cache_create("file_fsdata",
					     sizeof (reiser4_file_fsdata),
					     0,
					     SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					     NULL,
					     NULL);
	return (file_fsdata_slab == NULL) ? RETERR(-ENOMEM) : 0;
}

/*
 * dual to init_file_fsdata(). Called during module unload.
 */
reiser4_internal void done_file_fsdata(void)
{
	kmem_cache_destroy(file_fsdata_slab);
}

/*
 * Create reiser4 specific per-file data: reiser4_file_fsdata.
 */
reiser4_internal reiser4_file_fsdata *
create_fsdata(struct file *file, int gfp)
{
	reiser4_file_fsdata *fsdata;

	fsdata = kmem_cache_alloc(file_fsdata_slab, gfp);
	if (fsdata != NULL) {
		xmemset(fsdata, 0, sizeof *fsdata);
		fsdata->ra.max_window_size = VM_MAX_READAHEAD * 1024;
		fsdata->back = file;
		readdir_list_clean(fsdata);
	}
	return fsdata;
}

/* Return and lazily allocate if necessary per-file data that we attach
   to each struct file. */
reiser4_internal reiser4_file_fsdata *
reiser4_get_file_fsdata(struct file *f	/* file
					 * queried */ )
{
	assert("nikita-1603", f != NULL);

	if (f->private_data == NULL) {
		reiser4_file_fsdata *fsdata;
		struct inode *inode;

		reiser4_stat_inc(vfs_calls.private_data_alloc);
		fsdata = create_fsdata(f, GFP_KERNEL);
		if (fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));

		inode = f->f_dentry->d_inode;
		spin_lock_inode(inode);
		if (f->private_data == NULL) {
			f->private_data = fsdata;
			fsdata = NULL;
		}
		spin_unlock_inode(inode);
		if (fsdata != NULL)
			/* other thread initialized ->fsdata */
			kmem_cache_free(file_fsdata_slab, fsdata);
	}
	assert("nikita-2665", f->private_data != NULL);
	return f->private_data;
}

/*
 * Dual to create_fsdata(). Free reiser4_file_fsdata.
 */
reiser4_internal void
reiser4_free_fsdata(reiser4_file_fsdata *fsdata)
{
	if (fsdata != NULL)
		kmem_cache_free(file_fsdata_slab, fsdata);
}

/*
 * Dual to reiser4_get_file_fsdata().
 */
reiser4_internal void
reiser4_free_file_fsdata(struct file *f)
{
	reiser4_file_fsdata *fsdata;
	fsdata = f->private_data;
	if (fsdata != NULL) {
		readdir_list_remove_clean(fsdata);
		if (fsdata->cursor == NULL)
			reiser4_free_fsdata(fsdata);
	}
	f->private_data = NULL;
}

/* our ->read_inode() is no-op. Reiser4 inodes should be loaded
    through fs/reiser4/inode.c:reiser4_iget() */
static void
noop_read_inode(struct inode *inode UNUSED_ARG)
{
}

/* initialization and shutdown */

/* slab cache for inodes */
static kmem_cache_t *inode_cache;

/* initalisation function passed to the kmem_cache_create() to init new pages
   grabbed by our inodecache. */
static void
init_once(void *obj /* pointer to new inode */ ,
	  kmem_cache_t * cache UNUSED_ARG /* slab cache */ ,
	  unsigned long flags /* cache flags */ )
{
	reiser4_inode_object *info;

	info = obj;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR) {
		/* NOTE-NIKITA add here initializations for locks, list heads,
		   etc. that will be added to our private inode part. */
		inode_init_once(&info->vfs_inode);
		readdir_list_init(get_readdir_list(&info->vfs_inode));
		init_rwsem(&info->p.coc_sem);
		sema_init(&info->p.loading, 1);
		ON_DEBUG(info->p.nr_jnodes = 0);
		INIT_RADIX_TREE(jnode_tree_by_reiser4_inode(&info->p), GFP_ATOMIC);
		ON_DEBUG(info->p.captured_eflushed = 0);
		ON_DEBUG(info->p.anonymous_eflushed = 0);
		ON_DEBUG(inode_jnodes_list_init(&info->p.jnodes_list));
	}
}

/* initialize slab cache where reiser4 inodes will live */
reiser4_internal int
init_inodecache(void)
{
	inode_cache = kmem_cache_create("reiser4_inode",
					sizeof (reiser4_inode_object),
					0,
					SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					init_once,
					NULL);
	return (inode_cache != NULL) ? 0 : RETERR(-ENOMEM);
}

/* initialize slab cache where reiser4 inodes lived */
static void
destroy_inodecache(void)
{
	if (kmem_cache_destroy(inode_cache) != 0)
		warning("nikita-1695", "not all inodes were freed");
}

/* ->alloc_inode() super operation: allocate new inode */
static struct inode *
reiser4_alloc_inode(struct super_block *super UNUSED_ARG	/* super block new
								 * inode is
								 * allocated for */ )
{
	reiser4_inode_object *obj;

	assert("nikita-1696", super != NULL);
	reiser4_stat_inc_at(super, vfs_calls.alloc_inode);
	obj = kmem_cache_alloc(inode_cache, SLAB_KERNEL);
	if (obj != NULL) {
		reiser4_inode *info;

		info = &obj->p;

		info->hset = info->pset = plugin_set_get_empty();
		info->extmask = 0;
		info->locality_id = 0ull;
		info->plugin_mask = 0;
#if !REISER4_INO_IS_OID
		info->oid_hi = 0;
#endif
		seal_init(&info->sd_seal, NULL, NULL);
		coord_init_invalid(&info->sd_coord, NULL);
		info->cluster_shift = 0;
		info->crypt = NULL;
		info->flags = 0;
		spin_inode_object_init(info);

		/* initizalize inode's jnode */
		/*jnode_init(&info->inode_jnode, current_tree, JNODE_INODE);
		  atomic_set(&info->inode_jnode.x_count, 1);*/
		info->vroot = UBER_TREE_ADDR;
		return &obj->vfs_inode;
	} else
		return NULL;
}

/* ->destroy_inode() super operation: recycle inode */
static void
reiser4_destroy_inode(struct inode *inode /* inode being destroyed */)
{
	reiser4_inode *info;

	reiser4_stat_inc_at(inode->i_sb, vfs_calls.destroy_inode);

	info = reiser4_inode_data(inode);

	assert("vs-1220", jnode_tree_by_reiser4_inode(info)->rnode == NULL);
	assert("vs-1222", info->captured_eflushed == 0);
	assert("vs-1428", info->anonymous_eflushed == 0);
	assert("zam-1050", info->nr_jnodes == 0);

	if (!is_bad_inode(inode) && is_inode_loaded(inode)) {
		file_plugin * fplug = inode_file_plugin(inode);
		if (fplug->destroy_inode != NULL)
			fplug->destroy_inode(inode);
	}
	dispose_cursors(inode);
	if (info->pset)
		plugin_set_put(info->pset);

	/* FIXME: assert that info's page radix tree is empty */
	/*assert("nikita-2872", list_empty(&info->moved_pages));*/

	/* cannot add similar assertion about ->i_list as prune_icache return
	 * inode into slab with dangling ->list.{next,prev}. This is safe,
	 * because they are re-initialized in the new_inode(). */
	assert("nikita-2895", list_empty(&inode->i_dentry));
	assert("nikita-2896", hlist_unhashed(&inode->i_hash));
	assert("nikita-2898", readdir_list_empty(get_readdir_list(inode)));
	kmem_cache_free(inode_cache, container_of(info, reiser4_inode_object, p));
}

/* our ->drop_inode() method. This is called by iput_final() when last
 * reference on inode is released */
static void
reiser4_drop_inode(struct inode *object)
{
	file_plugin *fplug;

	assert("nikita-2643", object != NULL);

	/* -not- creating context in this method, because it is frequently
	   called and all existing ->not_linked() methods are one liners. */

	fplug = inode_file_plugin(object);
	/* fplug is NULL for fake inode */
	if (fplug != NULL) {
		assert("nikita-3251", fplug->drop != NULL);
		fplug->drop(object);
	} else
		generic_forget_inode(object);
}

/*
 * Called by reiser4_sync_inodes(), during speculative write-back (through
 * pdflush, or balance_dirty_pages()).
 */
static void
writeout(struct super_block *sb, struct writeback_control *wbc)
{
	long written = 0;
	int repeats = 0;

	/*
	 * Performs early flushing, trying to free some memory. If there is
	 * nothing to flush, commits some atoms.
	 */

	/* reiser4 has its own means of periodical write-out */
	if (wbc->for_kupdate)
		return;

	/* Commit all atoms if reiser4_writepages() is called from sys_sync() or
	   sys_fsync(). */
	if (wbc->sync_mode != WB_SYNC_NONE) {
		txnmgr_force_commit_all(sb, 1);
		return;
	}

	do {
		long nr_submitted = 0;
		struct inode *fake;

		fake = get_super_fake(sb);
		if (fake != NULL) {
			struct address_space *mapping;

			mapping = fake->i_mapping;
			/* do not put more requests to overload write queue */
			if (wbc->nonblocking &&
			    bdi_write_congested(mapping->backing_dev_info)) {

				blk_run_address_space(mapping);
				/*blk_run_queues();*/
				wbc->encountered_congestion = 1;
				break;
			}
		}
		repeats ++;
		flush_some_atom(&nr_submitted, wbc, JNODE_FLUSH_WRITE_BLOCKS);
		if (!nr_submitted)
			break;

		wbc->nr_to_write -= nr_submitted;

		written += nr_submitted;

	} while (wbc->nr_to_write > 0);

}

/* ->sync_inodes() method. This is called by pdflush, and synchronous
 * writeback (throttling by balance_dirty_pages()). */
static void
reiser4_sync_inodes(struct super_block * sb, struct writeback_control * wbc)
{
	reiser4_context ctx;

	init_context(&ctx, sb);
	wbc->older_than_this = NULL;

	/*
	 * What we are trying to do here is to capture all "anonymous" pages.
	 */
	capture_reiser4_inodes(sb, wbc);
	spin_unlock(&inode_lock);
	writeout(sb, wbc);

	/* avoid recursive calls to ->sync_inodes */
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	spin_lock(&inode_lock);
}

void reiser4_throttle_write(struct inode * inode)
{
	txn_restart_current();
	balance_dirty_pages_ratelimited(inode->i_mapping);
}

/* ->delete_inode() super operation */
static void
reiser4_delete_inode(struct inode *object)
{
	reiser4_context ctx;

	init_context(&ctx, object->i_sb);
	reiser4_stat_inc(vfs_calls.delete_inode);
	if (is_inode_loaded(object)) {
		file_plugin *fplug;

		fplug = inode_file_plugin(object);
		if (fplug != NULL && fplug->delete != NULL)
			fplug->delete(object);
	}

	object->i_blocks = 0;
	clear_inode(object);
	reiser4_exit_context(&ctx);
}

/* ->delete_inode() super operation */
static void
reiser4_clear_inode(struct inode *object)
{
	reiser4_inode *r4_inode;

	r4_inode = reiser4_inode_data(object);
	assert("vs-1688", (r4_inode->anonymous_eflushed == 0 &&
			   r4_inode->captured_eflushed == 0 &&
			   r4_inode->nr_jnodes == 0));
}

const char *REISER4_SUPER_MAGIC_STRING = "ReIsEr4";
const int REISER4_MAGIC_OFFSET = 16 * 4096;	/* offset to magic string from the
						 * beginning of device */

/* type of option parseable by parse_option() */
typedef enum {
	/* value of option is arbitrary string */
	OPT_STRING,
	/* option specifies bit in a bitmask */
	OPT_BIT,
	/* value of option should conform to sprintf() format */
	OPT_FORMAT,
	/* option can take one of predefined values */
	OPT_ONEOF,
} opt_type_t;

typedef struct opt_bitmask_bit {
	const char *bit_name;
	int bit_nr;
} opt_bitmask_bit;

/* description of option parseable by parse_option() */
typedef struct opt_desc {
	/* option name.

	   parsed portion of string has a form "name=value".
	*/
	const char *name;
	/* type of option */
	opt_type_t type;
	union {
		/* where to store value of string option (type == OPT_STRING) */
		char **string;
		/* description of bits for bit option (type == OPT_BIT) */
		struct {
			int nr;
			void *addr;
		} bit;
		/* description of format and targets for format option (type
		   == OPT_FORMAT) */
		struct {
			const char *format;
			int nr_args;
			void *arg1;
			void *arg2;
			void *arg3;
			void *arg4;
		} f;
		struct {
			int *result;
			const char *list[10];
		} oneof;
		struct {
			void *addr;
			int nr_bits;
			opt_bitmask_bit *bits;
		} bitmask;
	} u;
} opt_desc_t;

/* parse one option */
static int
parse_option(char *opt_string /* starting point of parsing */ ,
	     opt_desc_t * opt /* option description */ )
{
	/* foo=bar,
	   ^   ^  ^
	   |   |  +-- replaced to '\0'
	   |   +-- val_start
	   +-- opt_string
	*/
	char *val_start;
	int result;
	const char *err_msg;

	/* NOTE-NIKITA think about using lib/cmdline.c functions here. */

	val_start = strchr(opt_string, '=');
	if (val_start != NULL) {
		*val_start = '\0';
		++val_start;
	}

	err_msg = NULL;
	result = 0;
	switch (opt->type) {
	case OPT_STRING:
		if (val_start == NULL) {
			err_msg = "String arg missing";
			result = RETERR(-EINVAL);
		} else
			*opt->u.string = val_start;
		break;
	case OPT_BIT:
		if (val_start != NULL)
			err_msg = "Value ignored";
		else
			set_bit(opt->u.bit.nr, opt->u.bit.addr);
		break;
	case OPT_FORMAT:
		if (val_start == NULL) {
			err_msg = "Formatted arg missing";
			result = RETERR(-EINVAL);
			break;
		}
		if (sscanf(val_start, opt->u.f.format,
			   opt->u.f.arg1, opt->u.f.arg2, opt->u.f.arg3, opt->u.f.arg4) != opt->u.f.nr_args) {
			err_msg = "Wrong conversion";
			result = RETERR(-EINVAL);
		}
		break;
	case OPT_ONEOF:{
			int i = 0;
			err_msg = "Wrong option value";
			result = RETERR(-EINVAL);
			while ( opt->u.oneof.list[i] ) {
				if ( !strcmp(opt->u.oneof.list[i], val_start) ) {
					result = 0;
					*opt->u.oneof.result = i;
printk("%s choice is %d\n",opt->name, i);
					break;
				}
				i++;
			}
		        break;
		       }
	default:
		wrong_return_value("nikita-2100", "opt -> type");
		break;
	}
	if (err_msg != NULL) {
		warning("nikita-2496", "%s when parsing option \"%s%s%s\"",
			err_msg, opt->name, val_start ? "=" : "", val_start ? : "");
	}
	return result;
}

/* parse options */
static int
parse_options(char *opt_string /* starting point */ ,
	      opt_desc_t * opts /* array with option description */ ,
	      int nr_opts /* number of elements in @opts */ )
{
	int result;

	result = 0;
	while ((result == 0) && opt_string && *opt_string) {
		int j;
		char *next;

		next = strchr(opt_string, ',');
		if (next != NULL) {
			*next = '\0';
			++next;
		}
		for (j = 0; j < nr_opts; ++j) {
			if (!strncmp(opt_string, opts[j].name, strlen(opts[j].name))) {
				result = parse_option(opt_string, &opts[j]);
				break;
			}
		}
		if (j == nr_opts) {
			warning("nikita-2307", "Unrecognized option: \"%s\"", opt_string);
			/* traditionally, -EINVAL is returned on wrong mount
			   option */
			result = RETERR(-EINVAL);
		}
		opt_string = next;
	}
	return result;
}

#define NUM_OPT( label, fmt, addr )				\
		{						\
			.name = ( label ),			\
			.type = OPT_FORMAT,			\
			.u = {					\
				.f = {				\
					.format  = ( fmt ),	\
					.nr_args = 1,		\
					.arg1 = ( addr ),	\
					.arg2 = NULL,		\
					.arg3 = NULL,		\
					.arg4 = NULL		\
				}				\
			}					\
		}

#define SB_FIELD_OPT( field, fmt ) NUM_OPT( #field, fmt, &sbinfo -> field )

#define BIT_OPT(label, bitnr)					\
	{							\
		.name = label,					\
		.type = OPT_BIT,				\
		.u = {						\
			.bit = {				\
				.nr = bitnr,			\
				.addr = &sbinfo->fs_flags	\
			}					\
		}						\
	}

/* parse options during mount */
reiser4_internal int
reiser4_parse_options(struct super_block *s, char *opt_string)
{
	int result;
	reiser4_super_info_data *sbinfo = get_super_private(s);
	char *log_file_name;

	opt_desc_t opts[] = {
		/* trace_flags=N

		   set trace flags to be N for this mount. N can be C numeric
		   literal recognized by %i scanf specifier.  It is treated as
		   bitfield filled by values of debug.h:reiser4_trace_flags
		   enum
		*/
		SB_FIELD_OPT(trace_flags, "%i"),
		/* log_flags=N

		   set log flags to be N for this mount. N can be C numeric
		   literal recognized by %i scanf specifier.  It is treated as
		   bitfield filled by values of debug.h:reiser4_log_flags
		   enum
		*/
		SB_FIELD_OPT(log_flags, "%i"),
		/* debug_flags=N

		   set debug flags to be N for this mount. N can be C numeric
		   literal recognized by %i scanf specifier.  It is treated as
		   bitfield filled by values of debug.h:reiser4_debug_flags
		   enum
		*/
		SB_FIELD_OPT(debug_flags, "%i"),
		/* tmgr.atom_max_size=N

		   Atoms containing more than N blocks will be forced to
		   commit. N is decimal.
		*/
		SB_FIELD_OPT(tmgr.atom_max_size, "%u"),
		/* tmgr.atom_max_age=N

		   Atoms older than N seconds will be forced to commit. N is
		   decimal.
		*/
		SB_FIELD_OPT(tmgr.atom_max_age, "%u"),
		/* tmgr.atom_max_flushers=N

		   limit of concurrent flushers for one atom. 0 means no limit.
		 */
		SB_FIELD_OPT(tmgr.atom_max_flushers, "%u"),
		/* tree.cbk_cache_slots=N

		   Number of slots in the cbk cache.
		*/
		SB_FIELD_OPT(tree.cbk_cache.nr_slots, "%u"),

		/* If flush finds more than FLUSH_RELOCATE_THRESHOLD adjacent
		   dirty leaf-level blocks it will force them to be
		   relocated. */
		SB_FIELD_OPT(flush.relocate_threshold, "%u"),
		/* If flush finds can find a block allocation closer than at
		   most FLUSH_RELOCATE_DISTANCE from the preceder it will
		   relocate to that position. */
		SB_FIELD_OPT(flush.relocate_distance, "%u"),
		/* If we have written this much or more blocks before
		   encountering busy jnode in flush list - abort flushing
		   hoping that next time we get called this jnode will be
		   clean already, and we will save some seeks. */
		SB_FIELD_OPT(flush.written_threshold, "%u"),
		/* The maximum number of nodes to scan left on a level during
		   flush. */
		SB_FIELD_OPT(flush.scan_maxnodes, "%u"),

		/* preferred IO size */
		SB_FIELD_OPT(optimal_io_size, "%u"),

		/* carry flags used for insertion of new nodes */
		SB_FIELD_OPT(tree.carry.new_node_flags, "%u"),
		/* carry flags used for insertion of new extents */
		SB_FIELD_OPT(tree.carry.new_extent_flags, "%u"),
		/* carry flags used for paste operations */
		SB_FIELD_OPT(tree.carry.paste_flags, "%u"),
		/* carry flags used for insert operations */
		SB_FIELD_OPT(tree.carry.insert_flags, "%u"),

#ifdef CONFIG_REISER4_BADBLOCKS
		/* Alternative master superblock location in case if it's original
		   location is not writeable/accessable. This is offset in BYTES. */
		SB_FIELD_OPT(altsuper, "%lu"),
#endif

		/* turn on BSD-style gid assignment */
		BIT_OPT("bsdgroups", REISER4_BSD_GID),
		/* turn on 32 bit times */
		BIT_OPT("32bittimes", REISER4_32_BIT_TIMES),
		/* turn off concurrent flushing */
		BIT_OPT("mtflush", REISER4_MTFLUSH),
		/* disable pseudo files support */
		BIT_OPT("nopseudo", REISER4_NO_PSEUDO),
		/* Don't load all bitmap blocks at mount time, it is useful
		   for machines with tiny RAM and large disks. */
		BIT_OPT("dont_load_bitmap", REISER4_DONT_LOAD_BITMAP),

		{
			/* tree traversal readahead parameters:
			   -o readahead:MAXNUM:FLAGS
			   MAXNUM - max number fo nodes to request readahead for: -1UL will set it to max_sane_readahead()
			   FLAGS - combination of bits: RA_ADJCENT_ONLY, RA_ALL_LEVELS, CONTINUE_ON_PRESENT
			*/
			.name = "readahead",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%u:%u",
					.nr_args = 2,
					.arg1 = &sbinfo->ra_params.max,
					.arg2 = &sbinfo->ra_params.flags,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		},
		/* What to do in case of fs error */
		{
                        .name = "onerror",
                        .type = OPT_ONEOF,
                        .u = {
                                .oneof = {
                                        .result = &sbinfo->onerror,
                                        .list = {"panic", "remount-ro", "reboot", NULL},
                                }
                        }
                },

#if REISER4_LOG
		{
			.name = "log_file",
			.type = OPT_STRING,
			.u = {
				.string = &log_file_name
			}
		},
#endif
	};

	sbinfo->tmgr.atom_max_size = txnmgr_get_max_atom_size(s);
	sbinfo->tmgr.atom_max_age = REISER4_ATOM_MAX_AGE / HZ;

	sbinfo->tree.cbk_cache.nr_slots = CBK_CACHE_SLOTS;

	sbinfo->flush.relocate_threshold = FLUSH_RELOCATE_THRESHOLD;
	sbinfo->flush.relocate_distance = FLUSH_RELOCATE_DISTANCE;
	sbinfo->flush.written_threshold = FLUSH_WRITTEN_THRESHOLD;
	sbinfo->flush.scan_maxnodes = FLUSH_SCAN_MAXNODES;


	sbinfo->optimal_io_size = REISER4_OPTIMAL_IO_SIZE;

	sbinfo->tree.carry.new_node_flags = REISER4_NEW_NODE_FLAGS;
	sbinfo->tree.carry.new_extent_flags = REISER4_NEW_EXTENT_FLAGS;
	sbinfo->tree.carry.paste_flags = REISER4_PASTE_FLAGS;
	sbinfo->tree.carry.insert_flags = REISER4_INSERT_FLAGS;

	log_file_name = NULL;

	/*
	  init default readahead params
	*/
	sbinfo->ra_params.max = num_physpages / 4;
	sbinfo->ra_params.flags = 0;

	result = parse_options(opt_string, opts, sizeof_array(opts));
	if (result != 0)
		return result;

	sbinfo->tmgr.atom_max_age *= HZ;
	if (sbinfo->tmgr.atom_max_age <= 0)
		/* overflow */
		sbinfo->tmgr.atom_max_age = REISER4_ATOM_MAX_AGE;

	/* round optimal io size up to 512 bytes */
	sbinfo->optimal_io_size >>= VFS_BLKSIZE_BITS;
	sbinfo->optimal_io_size <<= VFS_BLKSIZE_BITS;
	if (sbinfo->optimal_io_size == 0) {
		warning("nikita-2497", "optimal_io_size is too small");
		return RETERR(-EINVAL);
	}
#if REISER4_LOG
	if (log_file_name != NULL)
		result = open_log_file(s, log_file_name, REISER4_TRACE_BUF_SIZE, &sbinfo->log_file);
	else
		sbinfo->log_file.type = log_to_bucket;
#endif

	/* disable single-threaded flush as it leads to deadlock */
	sbinfo->fs_flags |= (1 << REISER4_MTFLUSH);
	return result;
}

/* show mount options in /proc/mounts */
static int
reiser4_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct super_block *super;
	reiser4_super_info_data *sbinfo;

	super = mnt->mnt_sb;
	sbinfo = get_super_private(super);

	seq_printf(m, ",trace=0x%x", sbinfo->trace_flags);
	seq_printf(m, ",log=0x%x", sbinfo->log_flags);
	seq_printf(m, ",debug=0x%x", sbinfo->debug_flags);
	seq_printf(m, ",atom_max_size=0x%x", sbinfo->tmgr.atom_max_size);

	return 0;
}

/*
 * Lock profiling code.
 *
 * see spin_macros.h and spinprof.[ch]
 *
 */

/* defined profiling regions for spin lock types */
DEFINE_SPIN_PROFREGIONS(epoch);
DEFINE_SPIN_PROFREGIONS(jnode);
DEFINE_SPIN_PROFREGIONS(jload);
DEFINE_SPIN_PROFREGIONS(stack);
DEFINE_SPIN_PROFREGIONS(super);
DEFINE_SPIN_PROFREGIONS(atom);
DEFINE_SPIN_PROFREGIONS(txnh);
DEFINE_SPIN_PROFREGIONS(txnmgr);
DEFINE_SPIN_PROFREGIONS(ktxnmgrd);
DEFINE_SPIN_PROFREGIONS(inode_object);
DEFINE_SPIN_PROFREGIONS(fq);
DEFINE_SPIN_PROFREGIONS(super_eflush);

/* define profiling regions for read-write locks */
DEFINE_RW_PROFREGIONS(zlock);
DEFINE_RW_PROFREGIONS(dk);
DEFINE_RW_PROFREGIONS(tree);
DEFINE_RW_PROFREGIONS(cbk_cache);

/* register profiling regions defined above */
static int register_profregions(void)
{
	register_super_eflush_profregion();
	register_epoch_profregion();
	register_jnode_profregion();
	register_jload_profregion();
	register_stack_profregion();
	register_super_profregion();
	register_atom_profregion();
	register_txnh_profregion();
	register_txnmgr_profregion();
	register_ktxnmgrd_profregion();
	register_inode_object_profregion();
	register_fq_profregion();

	register_zlock_profregion();
	register_cbk_cache_profregion();
	register_dk_profregion();
	register_tree_profregion();

	return 0;
}

/* unregister profiling regions defined above */
static void unregister_profregions(void)
{
	unregister_super_eflush_profregion();
	unregister_epoch_profregion();
	unregister_jload_profregion();
	unregister_jnode_profregion();
	unregister_stack_profregion();
	unregister_super_profregion();
	unregister_atom_profregion();
	unregister_txnh_profregion();
	unregister_txnmgr_profregion();
	unregister_ktxnmgrd_profregion();
	unregister_inode_object_profregion();
	unregister_fq_profregion();

	unregister_zlock_profregion();
	unregister_cbk_cache_profregion();
	unregister_dk_profregion();
	unregister_tree_profregion();
}

/* ->write_super() method. Called by sync(2). */
static void
reiser4_write_super(struct super_block *s)
{
	int ret;
	reiser4_context ctx;

	assert("vs-1700", !rofs_super(s));

	init_context(&ctx, s);
	reiser4_stat_inc(vfs_calls.write_super);

	ret = capture_super_block(s);
	if (ret != 0)
		warning("vs-1701",
			"capture_super_block failed in write_super: %d", ret);
	ret = txnmgr_force_commit_all(s, 1);
	if (ret != 0)
		warning("jmacd-77113",
			"txn_force failed in write_super: %d", ret);

	s->s_dirt = 0;

	reiser4_exit_context(&ctx);
}

static void
reiser4_put_super(struct super_block *s)
{
	reiser4_super_info_data *sbinfo;
	reiser4_context context;

	sbinfo = get_super_private(s);
	assert("vs-1699", sbinfo);

	init_context(&context, s);
	done_reiser4_repacker(s);
	stop_ktxnmgrd(&sbinfo->tmgr);
	reiser4_sysfs_done(s);

	/* have disk format plugin to free its resources */
	if (get_super_private(s)->df_plug->release)
		get_super_private(s)->df_plug->release(s);

	done_ktxnmgrd_context(&sbinfo->tmgr);
	done_entd_context(s);

	check_block_counters(s);

	rcu_barrier();
	/* done_formatted_fake just has finished with last jnodes (bitmap
	 * ones) */
	done_tree(&sbinfo->tree);
	/* call finish_rcu(), because some znode were "released" in
	 * done_tree(). */
	rcu_barrier();
	done_formatted_fake(s);

	/* no assertions below this line */
	reiser4_exit_context(&context);

	reiser4_stat_done(&sbinfo->stats);

	kfree(sbinfo);
	s->s_fs_info = NULL;
}

/* ->get_sb() method of file_system operations. */
static struct super_block *
reiser4_get_sb(struct file_system_type *fs_type	/* file
						 * system
						 * type */ ,
	       int flags /* flags */ ,
	       const char *dev_name /* device name */ ,
	       void *data /* mount options */ )
{
	return get_sb_bdev(fs_type, flags, dev_name, data, reiser4_fill_super);
}

int d_cursor_init(void);
void d_cursor_done(void);

/*
 * Reiser4 initialization/shutdown.
 *
 * Code below performs global reiser4 initialization that is done either as
 * part of kernel initialization (when reiser4 is statically built-in), or
 * during reiser4 module load (when compiled as module).
 */

/*
 * Initialization stages for reiser4.
 *
 * These enumerate various things that have to be done during reiser4
 * startup. Initialization code (init_reiser4()) keeps track of what stage was
 * reached, so that proper undo can be done if error occurs during
 * initialization.
 */
typedef enum {
	INIT_NONE,               /* nothing is initialized yet */
	INIT_INODECACHE,         /* inode cache created */
	INIT_CONTEXT_MGR,        /* list of active contexts created */
	INIT_ZNODES,             /* znode slab created */
	INIT_PLUGINS,            /* plugins initialized */
	INIT_PLUGIN_SET,         /* psets initialized */
	INIT_TXN,                /* transaction manager initialized */
	INIT_FAKES,              /* fake inode initialized */
	INIT_JNODES,             /* jnode slab initialized */
	INIT_EFLUSH,             /* emergency flush initialized */
	INIT_SPINPROF,           /* spin lock profiling initialized */
	INIT_SYSFS,              /* sysfs exports initialized */
	INIT_LNODES,             /* lnodes initialized */
	INIT_FQS,                /* flush queues initialized */
	INIT_DENTRY_FSDATA,      /* dentry_fsdata slab initialized */
	INIT_FILE_FSDATA,        /* file_fsdata slab initialized */
	INIT_D_CURSOR,           /* d_cursor suport initialized */
	INIT_FS_REGISTERED,      /* reiser4 file system type registered */
} reiser4_init_stage;

static reiser4_init_stage init_stage;

/* finish with reiser4: this is called either at shutdown or at module unload. */
static void
shutdown_reiser4(void)
{
#define DONE_IF( stage, exp )			\
	if( init_stage == ( stage ) ) {		\
		exp;				\
		-- init_stage;			\
	}

	/*
	 * undo initializations already done by init_reiser4().
	 */

	DONE_IF(INIT_FS_REGISTERED, unregister_filesystem(&reiser4_fs_type));
	DONE_IF(INIT_D_CURSOR, d_cursor_done());
	DONE_IF(INIT_FILE_FSDATA, done_file_fsdata());
	DONE_IF(INIT_DENTRY_FSDATA, done_dentry_fsdata());
	DONE_IF(INIT_FQS, done_fqs());
	DONE_IF(INIT_LNODES, lnodes_done());
	DONE_IF(INIT_SYSFS, reiser4_sysfs_done_once());
	DONE_IF(INIT_SPINPROF, unregister_profregions());
	DONE_IF(INIT_EFLUSH, eflush_done());
	DONE_IF(INIT_JNODES, jnode_done_static());
	DONE_IF(INIT_FAKES,;);
	DONE_IF(INIT_TXN, txnmgr_done_static());
	DONE_IF(INIT_PLUGIN_SET,plugin_set_done());
	DONE_IF(INIT_PLUGINS,;);
	DONE_IF(INIT_ZNODES, znodes_done());
	DONE_IF(INIT_CONTEXT_MGR,;);
	DONE_IF(INIT_INODECACHE, destroy_inodecache());
	assert("nikita-2516", init_stage == INIT_NONE);

#undef DONE_IF
}

/* initialize reiser4: this is called either at bootup or at module load. */
static int __init
init_reiser4(void)
{
#define CHECK_INIT_RESULT( exp )		\
({						\
	result = exp;				\
	if( result == 0 )			\
		++ init_stage;			\
	else {					\
		shutdown_reiser4();		\
		return result;			\
	}					\
})

	int result;
	/*
	printk(KERN_INFO
	       "Loading Reiser4. "
	       "See www.namesys.com for a description of Reiser4.\n");
	*/
	init_stage = INIT_NONE;

	CHECK_INIT_RESULT(init_inodecache());
	CHECK_INIT_RESULT(init_context_mgr());
	CHECK_INIT_RESULT(znodes_init());
	CHECK_INIT_RESULT(init_plugins());
	CHECK_INIT_RESULT(plugin_set_init());
	CHECK_INIT_RESULT(txnmgr_init_static());
	CHECK_INIT_RESULT(init_fakes());
	CHECK_INIT_RESULT(jnode_init_static());
	CHECK_INIT_RESULT(eflush_init());
	CHECK_INIT_RESULT(register_profregions());
	CHECK_INIT_RESULT(reiser4_sysfs_init_once());
	CHECK_INIT_RESULT(lnodes_init());
	CHECK_INIT_RESULT(init_fqs());
	CHECK_INIT_RESULT(init_dentry_fsdata());
	CHECK_INIT_RESULT(init_file_fsdata());
	CHECK_INIT_RESULT(d_cursor_init());
	CHECK_INIT_RESULT(register_filesystem(&reiser4_fs_type));

	calibrate_prof();

	assert("nikita-2515", init_stage == INIT_FS_REGISTERED);
	return 0;
#undef CHECK_INIT_RESULT
}

static void __exit
done_reiser4(void)
{
	shutdown_reiser4();
}

reiser4_internal void reiser4_handle_error(void)
{
	struct super_block *sb = reiser4_get_current_sb();

	if ( !sb )
		return;
	reiser4_status_write(REISER4_STATUS_DAMAGED, 0, "Filesystem error occured");
	switch ( get_super_private(sb)->onerror ) {
	case 0:
		reiser4_panic("foobar-42", "Filesystem error occured\n");
	case 1:
		if ( sb->s_flags & MS_RDONLY )
			return;
		sb->s_flags |= MS_RDONLY;
		break;
	case 2:
		machine_restart(NULL);
	}
}

module_init(init_reiser4);
module_exit(done_reiser4);

MODULE_DESCRIPTION("Reiser4 filesystem");
MODULE_AUTHOR("Hans Reiser <Reiser@Namesys.COM>");

MODULE_LICENSE("GPL");

/* description of the reiser4 file system type in the VFS eyes. */
struct file_system_type reiser4_fs_type = {
	.owner = THIS_MODULE,
	.name = "reiser4",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = reiser4_get_sb,
	.kill_sb = kill_block_super,/*reiser4_kill_super,*/
	.next = NULL
};

struct super_operations reiser4_super_operations = {
	.alloc_inode = reiser4_alloc_inode,
	.destroy_inode = reiser4_destroy_inode,
	.read_inode = noop_read_inode,
	.dirty_inode = NULL,
 	.write_inode = NULL,
 	.put_inode = NULL,
	.drop_inode = reiser4_drop_inode,
	.delete_inode = reiser4_delete_inode,
	.put_super = reiser4_put_super,
	.write_super = reiser4_write_super,
	.sync_fs = NULL,
 	.write_super_lockfs = NULL,
 	.unlockfs           = NULL,
	.statfs = reiser4_statfs,
 	.remount_fs         = NULL,
	.clear_inode  = reiser4_clear_inode,
 	.umount_begin       = NULL,
	.sync_inodes = reiser4_sync_inodes,
	.show_options = reiser4_show_options
};

/*
 * Object serialization support.
 *
 * To support knfsd file system provides export_operations that are used to
 * construct and interpret NFS file handles. As a generalization of this,
 * reiser4 object plugins have serialization support: it provides methods to
 * create on-wire representation of identity of reiser4 object, and
 * re-create/locate object given its on-wire identity.
 *
 */

/*
 * return number of bytes that on-wire representation of @inode's identity
 * consumes.
 */
static int
encode_inode_size(struct inode *inode)
{
	assert("nikita-3514", inode != NULL);
	assert("nikita-3515", inode_file_plugin(inode) != NULL);
	assert("nikita-3516", inode_file_plugin(inode)->wire.size != NULL);

	return inode_file_plugin(inode)->wire.size(inode) + sizeof(d16);
}

/*
 * store on-wire representation of @inode's identity at the area beginning at
 * @start.
 */
static char *
encode_inode(struct inode *inode, char *start)
{
	assert("nikita-3517", inode != NULL);
	assert("nikita-3518", inode_file_plugin(inode) != NULL);
	assert("nikita-3519", inode_file_plugin(inode)->wire.write != NULL);

	/*
	 * first, store two-byte identifier of object plugin, then
	 */
	save_plugin_id(file_plugin_to_plugin(inode_file_plugin(inode)),
		       (d16 *)start);
	start += sizeof(d16);
	/*
	 * call plugin to serialize object's identity
	 */
	return inode_file_plugin(inode)->wire.write(inode, start);
}

/*
 * Supported file-handle types
 */
typedef enum {
	FH_WITH_PARENT    = 0x10,  /* file handle with parent */
	FH_WITHOUT_PARENT = 0x11   /* file handle without parent */
} reiser4_fhtype;

#define NFSERROR (255)

/* this returns number of 32 bit long numbers encoded in @lenp. 255 is
 * returned if file handle can not be stored */
static int
reiser4_encode_fh(struct dentry *dentry, __u32 *data, int *lenp, int need_parent)
{
	struct inode *inode;
	struct inode *parent;
	char *addr;
	int need;
	int delta;
	int result;
	reiser4_context context;

	/*
	 * knfsd asks as to serialize object in @dentry, and, optionally its
	 * parent (if need_parent != 0).
	 *
	 * encode_inode() and encode_inode_size() is used to build
	 * representation of object and its parent. All hard work is done by
	 * object plugins.
	 */

	inode = dentry->d_inode;
	parent = dentry->d_parent->d_inode;

	addr = (char *)data;

	need = encode_inode_size(inode);
	if (need < 0)
		return NFSERROR;
	if (need_parent) {
		delta = encode_inode_size(parent);
		if (delta < 0)
			return NFSERROR;
		need += delta;
	}

	init_context(&context, dentry->d_inode->i_sb);

	if (need <= sizeof(__u32) * (*lenp)) {
		addr = encode_inode(inode, addr);
		if (need_parent)
			addr = encode_inode(parent, addr);

		/* store in lenp number of 32bit words required for file
		 * handle. */
		*lenp = (need + sizeof(__u32) - 1) >> 2;
		result = need_parent ? FH_WITH_PARENT : FH_WITHOUT_PARENT;
	} else
		/* no enough space in file handle */
		result = NFSERROR;
	reiser4_exit_context(&context);
	return result;
}

/*
 * read serialized object identity from @addr and store information about
 * object in @obj. This is dual to encode_inode().
 */
static char *
decode_inode(struct super_block *s, char *addr, reiser4_object_on_wire *obj)
{
	file_plugin *fplug;

	/* identifier of object plugin is stored in the first two bytes,
	 * followed by... */
	fplug = file_plugin_by_disk_id(get_tree(s), (d16 *)addr);
	if (fplug != NULL) {
		addr += sizeof(d16);
		obj->plugin = fplug;
		assert("nikita-3520", fplug->wire.read != NULL);
		/* plugin specific encoding of object identity. */
		addr = fplug->wire.read(addr, obj);
	} else
		addr = ERR_PTR(RETERR(-EINVAL));
	return addr;
}

/* initialize place-holder for object */
static void
object_on_wire_init(reiser4_object_on_wire *o)
{
	o->plugin = NULL;
}

/* finish with @o */
static void
object_on_wire_done(reiser4_object_on_wire *o)
{
	if (o->plugin != NULL)
		o->plugin->wire.done(o);
}

/* decode knfsd file handle. This is dual to reiser4_encode_fh() */
static struct dentry *
reiser4_decode_fh(struct super_block *s, __u32 *data,
		  int len, int fhtype,
		  int (*acceptable)(void *context, struct dentry *de),
		  void *context)
{
	reiser4_context ctx;
	reiser4_object_on_wire object;
	reiser4_object_on_wire parent;
	char *addr;
	int   with_parent;

	init_context(&ctx, s);

	assert("vs-1482",
	       fhtype == FH_WITH_PARENT || fhtype == FH_WITHOUT_PARENT);

	with_parent = (fhtype == FH_WITH_PARENT);

	addr = (char *)data;

	object_on_wire_init(&object);
	object_on_wire_init(&parent);

	addr = decode_inode(s, addr, &object);
	if (!IS_ERR(addr)) {
		if (with_parent)
			addr = decode_inode(s, addr, &parent);
		if (!IS_ERR(addr)) {
			struct dentry *d;
			typeof(s->s_export_op->find_exported_dentry) fn;

			fn = s->s_export_op->find_exported_dentry;
			assert("nikita-3521", fn != NULL);
			d = fn(s, &object, with_parent ? &parent : NULL,
			       acceptable, context);
			if (d != NULL && !IS_ERR(d))
				/* FIXME check for -ENOMEM */
				reiser4_get_dentry_fsdata(d)->stateless = 1;
			addr = (char *)d;
		}
	}

	object_on_wire_done(&object);
	object_on_wire_done(&parent);

	reiser4_exit_context(&ctx);
	return (void *)addr;
}

static struct dentry *
reiser4_get_dentry(struct super_block *sb, void *data)
{
	reiser4_object_on_wire *o;

	assert("nikita-3522", sb != NULL);
	assert("nikita-3523", data != NULL);
	/*
	 * this is only supposed to be called by
	 *
	 *     reiser4_decode_fh->find_exported_dentry
	 *
	 * so, reiser4_context should be here already.
	 *
	 */
	assert("nikita-3526", is_in_reiser4_context());

	o = (reiser4_object_on_wire *)data;
	assert("nikita-3524", o->plugin != NULL);
	assert("nikita-3525", o->plugin->wire.get != NULL);

	return o->plugin->wire.get(sb, o);
}

static struct dentry *
reiser4_get_dentry_parent(struct dentry *child)
{
	struct inode *dir;
	dir_plugin *dplug;

	assert("nikita-3527", child != NULL);
	/* see comment in reiser4_get_dentry() about following assertion */
	assert("nikita-3528", is_in_reiser4_context());

	dir = child->d_inode;
	assert("nikita-3529", dir != NULL);
	dplug = inode_dir_plugin(dir);
	assert("nikita-3531", ergo(dplug != NULL, dplug->get_parent != NULL));
	if (dplug != NULL)
		return dplug->get_parent(dir);
	else
		return ERR_PTR(RETERR(-ENOTDIR));
}

struct export_operations reiser4_export_operations = {
	.encode_fh = reiser4_encode_fh,
	.decode_fh = reiser4_decode_fh,
	.get_parent = reiser4_get_dentry_parent,
	.get_dentry = reiser4_get_dentry
};

struct dentry_operations reiser4_dentry_operations = {
	.d_revalidate = NULL,
	.d_hash = NULL,
	.d_compare = NULL,
	.d_delete = NULL,
	.d_release = reiser4_d_release,
	.d_iput = NULL,
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
