/* Copyright by Hans Reiser, 2003 */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "tree.h"
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
#include "safe_link.h"
#include "plugin/dir/dir.h"

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/rcupdate.h>

#define _INIT_PARAM_LIST (struct super_block * s, reiser4_context * ctx, void * data, int silent)
#define _DONE_PARAM_LIST (struct super_block * s)

#define _INIT_(subsys) static int _init_##subsys _INIT_PARAM_LIST
#define _DONE_(subsys) static void _done_##subsys _DONE_PARAM_LIST

#define _DONE_EMPTY(subsys) _DONE_(subsys) {}

_INIT_(mount_flags_check)
{
/*	if (bdev_read_only(s->s_bdev) || (s->s_flags & MS_RDONLY)) {
		warning("nikita-3322", "Readonly reiser4 is not yet supported");
		return RETERR(-EROFS);
	}*/
	return 0;
}

_DONE_EMPTY(mount_flags_check)

_INIT_(sinfo)
{
	reiser4_super_info_data * sbinfo;

	sbinfo = kmalloc(sizeof (reiser4_super_info_data), GFP_KERNEL);
	if (!sbinfo)
		return RETERR(-ENOMEM);

	s->s_fs_info = sbinfo;
	s->s_op = NULL;
	xmemset(sbinfo, 0, sizeof (*sbinfo));

	ON_DEBUG(INIT_LIST_HEAD(&sbinfo->all_jnodes));
	ON_DEBUG(spin_lock_init(&sbinfo->all_guard));

	sema_init(&sbinfo->delete_sema, 1);
	sema_init(&sbinfo->flush_sema, 1);
	spin_super_init(sbinfo);
	spin_super_eflush_init(sbinfo);

	return 0;
}

_DONE_(sinfo)
{
	assert("zam-990", s->s_fs_info != NULL);
	rcu_barrier();
	kfree(s->s_fs_info);
	s->s_fs_info = NULL;
}

_INIT_(stat)
{
	return reiser4_stat_init(&get_super_private(s)->stats);
}

_DONE_(stat)
{
	reiser4_stat_done(&get_super_private(s)->stats);
}

_INIT_(context)
{
	return init_context(ctx, s);
}

_DONE_(context)
{
	reiser4_super_info_data * sbinfo;

	sbinfo = get_super_private(s);

	close_log_file(&sbinfo->log_file);

	if (reiser4_is_debugged(s, REISER4_STATS_ON_UMOUNT))
		reiser4_print_stats();

	/* we don't want ->write_super to be called any more. */
	if (s->s_op)
		s->s_op->write_super = NULL;
#if REISER4_DEBUG
	{
		struct list_head *scan;

		/* print jnodes that survived umount. */
		list_for_each(scan, &sbinfo->all_jnodes) {
			jnode *busy;

			busy = list_entry(scan, jnode, jnodes);
			info_jnode("\nafter umount", busy);
		}
	}
	if (sbinfo->kmalloc_allocated > 0)
		warning("nikita-2622",
			"%i bytes still allocated", sbinfo->kmalloc_allocated);
#endif

	get_current_context()->trans = NULL;
	done_context(get_current_context());
}

_INIT_(parse_options)
{
	return reiser4_parse_options(s, data);
}

_DONE_(parse_options)
{
	close_log_file(&get_super_private(s)->log_file);
}

_INIT_(object_ops)
{
	build_object_ops(s, &get_super_private(s)->ops);
	return 0;
}

_DONE_EMPTY(object_ops)

_INIT_(read_super)
{
	struct buffer_head *super_bh;
	struct reiser4_master_sb *master_sb;
	int plugin_id;
	reiser4_super_info_data * sbinfo = get_super_private(s);
	unsigned long blocksize;

 read_super_block:
#ifdef CONFIG_REISER4_BADBLOCKS
	if ( sbinfo->altsuper )
		super_bh = sb_bread(s, (sector_t) (sbinfo->altsuper >> s->s_blocksize_bits));
	else
#endif
		/* look for reiser4 magic at hardcoded place */
		super_bh = sb_bread(s, (sector_t) (REISER4_MAGIC_OFFSET / s->s_blocksize));

	if (!super_bh)
		return RETERR(-EIO);

	master_sb = (struct reiser4_master_sb *) super_bh->b_data;
	/* check reiser4 magic string */
	if (!strncmp(master_sb->magic, REISER4_SUPER_MAGIC_STRING, sizeof(REISER4_SUPER_MAGIC_STRING))) {
		/* reset block size if it is not a right one FIXME-VS: better comment is needed */
		blocksize = d16tocpu(&master_sb->blocksize);

		if (blocksize != PAGE_CACHE_SIZE) {
			if (!silent)
				warning("nikita-2609", "%s: wrong block size %ld\n", s->s_id, blocksize);
			brelse(super_bh);
			return RETERR(-EINVAL);
		}
		if (blocksize != s->s_blocksize) {
			brelse(super_bh);
			if (!sb_set_blocksize(s, (int) blocksize)) {
				return RETERR(-EINVAL);
			}
			goto read_super_block;
		}

		plugin_id = d16tocpu(&master_sb->disk_plugin_id);
		/* only two plugins are available for now */
		assert("vs-476", plugin_id == FORMAT40_ID);
		sbinfo->df_plug = disk_format_plugin_by_id(plugin_id);
		sbinfo->diskmap_block = d64tocpu(&master_sb->diskmap);
		brelse(super_bh);
	} else {
		if (!silent) {
			warning("nikita-2608", "Wrong master super block magic.");
		}

		/* no standard reiser4 super block found */
		brelse(super_bh);
		/* FIXME-VS: call guess method for all available layout
		   plugins */
		/* umka (2002.06.12) Is it possible when format-specific super
		   block exists but there no master super block? */
		return RETERR(-EINVAL);
	}
	return 0;
}

_DONE_EMPTY(read_super)

_INIT_(tree0)
{
	reiser4_super_info_data * sbinfo = get_super_private(s);

	init_tree_0(&sbinfo->tree);
	sbinfo->tree.super = s;
	return 0;
}

_DONE_EMPTY(tree0)

_INIT_(txnmgr)
{
	txnmgr_init(&get_super_private(s)->tmgr);
	return 0;
}

_DONE_(txnmgr)
{
	txnmgr_done(&get_super_private(s)->tmgr);
}

_INIT_(ktxnmgrd_context)
{
	return init_ktxnmgrd_context(&get_super_private(s)->tmgr);
}

_DONE_(ktxnmgrd_context)
{
	done_ktxnmgrd_context(&get_super_private(s)->tmgr);
}

_INIT_(ktxnmgrd)
{
	return start_ktxnmgrd(&get_super_private(s)->tmgr);
}

_DONE_(ktxnmgrd)
{
	stop_ktxnmgrd(&get_super_private(s)->tmgr);
}

_INIT_(formatted_fake)
{
	return init_formatted_fake(s);
}

_DONE_(formatted_fake)
{
	reiser4_super_info_data * sbinfo;

	sbinfo = get_super_private(s);

	rcu_barrier();

	/* done_formatted_fake just has finished with last jnodes (bitmap
	 * ones) */
	done_tree(&sbinfo->tree);
	/* call finish_rcu(), because some znode were "released" in
	 * done_tree(). */
	rcu_barrier();
	done_formatted_fake(s);
}

_INIT_(entd)
{
	init_entd_context(s);
	return 0;
}

_DONE_(entd)
{
	done_entd_context(s);
}

_DONE_(disk_format);

_INIT_(disk_format)
{
	return get_super_private(s)->df_plug->get_ready(s, data);
}

_DONE_(disk_format)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);

	sbinfo->df_plug->release(s);
}

_INIT_(sb_counters)
{
	/* There are some 'committed' versions of reiser4 super block
	   counters, which correspond to reiser4 on-disk state. These counters
	   are initialized here */
	reiser4_super_info_data *sbinfo = get_super_private(s);

	sbinfo->blocks_free_committed = sbinfo->blocks_free;
	sbinfo->nr_files_committed = oids_used(s);

	return 0;
}

_DONE_EMPTY(sb_counters)

_INIT_(d_cursor)
{
	/* this should be done before reading inode of root directory, because
	 * reiser4_iget() used load_cursors(). */
	return d_cursor_init_at(s);
}

_DONE_(d_cursor)
{
	d_cursor_done_at(s);
}

static struct {
	reiser4_plugin_type type;
	reiser4_plugin_id   id;
} default_plugins[PSET_LAST] = {
	[PSET_FILE] = {
		.type = REISER4_FILE_PLUGIN_TYPE,
		.id   = UNIX_FILE_PLUGIN_ID
	},
	[PSET_DIR] = {
		.type = REISER4_DIR_PLUGIN_TYPE,
		.id   = HASHED_DIR_PLUGIN_ID
	},
	[PSET_HASH] = {
		.type = REISER4_HASH_PLUGIN_TYPE,
		.id   = R5_HASH_ID
	},
	[PSET_FIBRATION] = {
		.type = REISER4_FIBRATION_PLUGIN_TYPE,
		.id   = FIBRATION_DOT_O
	},
	[PSET_PERM] = {
		.type = REISER4_PERM_PLUGIN_TYPE,
		.id   = RWX_PERM_ID
	},
	[PSET_FORMATTING] = {
		.type = REISER4_FORMATTING_PLUGIN_TYPE,
		.id   = SMALL_FILE_FORMATTING_ID
	},
	[PSET_SD] = {
		.type = REISER4_ITEM_PLUGIN_TYPE,
		.id   = STATIC_STAT_DATA_ID
	},
	[PSET_DIR_ITEM] = {
		.type = REISER4_ITEM_PLUGIN_TYPE,
		.id   = COMPOUND_DIR_ID
	},
	[PSET_CRYPTO] = {
		.type = REISER4_CRYPTO_PLUGIN_TYPE,
		.id   = NONE_CRYPTO_ID
	},
	[PSET_DIGEST] = {
		.type = REISER4_DIGEST_PLUGIN_TYPE,
		.id   = NONE_DIGEST_ID
	},
	[PSET_COMPRESSION] = {
		.type = REISER4_COMPRESSION_PLUGIN_TYPE,
		.id   = NONE_COMPRESSION_ID
	}
};

/* access to default plugin table */
reiser4_internal reiser4_plugin *
get_default_plugin(pset_member memb)
{
	return plugin_by_id(default_plugins[memb].type, default_plugins[memb].id);
}

_INIT_(fs_root)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);
	struct inode * inode;
	int result = 0;

	inode = reiser4_iget(s, sbinfo->df_plug->root_dir_key(s), 0);
	if (IS_ERR(inode))
		return RETERR(PTR_ERR(inode));

	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		iput(inode);
		return RETERR(-ENOMEM);
	}

	s->s_root->d_op = &sbinfo->ops.dentry;

	if (!is_inode_loaded(inode)) {
		pset_member    memb;

		for (memb = 0; memb < PSET_LAST; ++ memb) {
			reiser4_plugin *plug;

			plug = get_default_plugin(memb);
			result = grab_plugin_from(inode, memb, plug);
			if (result != 0)
				break;
		}

		if (result == 0) {
			if (REISER4_DEBUG) {
				plugin_set *pset;

				pset = reiser4_inode_data(inode)->pset;
				for (memb = 0; memb < PSET_LAST; ++ memb)
					assert("nikita-3500",
					       pset_get(pset, memb) != NULL);
			}
		} else
			warning("nikita-3448", "Cannot set plugins of root: %i",
				result);
		reiser4_iget_complete(inode);
	}
	s->s_maxbytes = MAX_LFS_FILESIZE;
	return result;
}

_DONE_(fs_root)
{
	shrink_dcache_parent(s->s_root);
}

_INIT_(sysfs)
{
	return reiser4_sysfs_init(s);
}

_DONE_(sysfs)
{
	reiser4_sysfs_done(s);
}

_INIT_(repacker)
{
	return init_reiser4_repacker(s);
}

_DONE_(repacker)
{
	done_reiser4_repacker(s);
}

_INIT_(safelink)
{
	process_safelinks(s);
	/* failure to process safe-links is not critical. Continue with
	 * mount. */
	return 0;
}

_DONE_(safelink)
{
}

_INIT_(exit_context)
{
	reiser4_exit_context(ctx);
	return 0;
}

_DONE_EMPTY(exit_context)

struct reiser4_subsys {
	int  (*init) _INIT_PARAM_LIST;
	void (*done) _DONE_PARAM_LIST;
};

#define _SUBSYS(subsys) {.init = &_init_##subsys, .done = &_done_##subsys}
static struct reiser4_subsys subsys_array[] = {
	_SUBSYS(mount_flags_check),
	_SUBSYS(sinfo),
	_SUBSYS(stat),
	_SUBSYS(context),
	_SUBSYS(parse_options),
	_SUBSYS(object_ops),
	_SUBSYS(read_super),
	_SUBSYS(tree0),
	_SUBSYS(txnmgr),
	_SUBSYS(ktxnmgrd_context),
	_SUBSYS(ktxnmgrd),
	_SUBSYS(entd),
	_SUBSYS(formatted_fake),
	_SUBSYS(disk_format),
	_SUBSYS(sb_counters),
	_SUBSYS(d_cursor),
	_SUBSYS(fs_root),
	_SUBSYS(sysfs),
	_SUBSYS(repacker),
	_SUBSYS(safelink),
	_SUBSYS(exit_context)
};

#define REISER4_NR_SUBSYS (sizeof(subsys_array) / sizeof(struct reiser4_subsys))

static void done_super (struct super_block * s, int last_done)
{
	int i;
	for (i = last_done; i >= 0; i--)
		subsys_array[i].done(s);
}

/* read super block from device and fill remaining fields in @s.

   This is read_super() of the past.  */
reiser4_internal int
reiser4_fill_super (struct super_block * s, void * data, int silent)
{
	reiser4_context ctx;
	int i;
	int ret;

	assert ("zam-989", s != NULL);

	for (i = 0; i < REISER4_NR_SUBSYS; i++) {
		ret = subsys_array[i].init(s, &ctx, data, silent);
		if (ret) {
			done_super(s, i - 1);
			return ret;
		}
	}
	return 0;
}

#if 0

int reiser4_done_super (struct super_block * s)
{
	reiser4_context ctx;

	init_context(&ctx, s);
	done_super(s, REISER4_NR_SUBSYS - 1);
	return 0;
}

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
