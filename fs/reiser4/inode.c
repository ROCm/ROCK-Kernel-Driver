/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Inode specific operations. */

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "kassign.h"
#include "coord.h"
#include "seal.h"
#include "dscale.h"
#include "plugin/item/item.h"
#include "plugin/security/perm.h"
#include "plugin/plugin.h"
#include "plugin/object.h"
#include "plugin/dir/dir.h"
#include "znode.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"
#include "reiser4.h"

#include <linux/fs.h>		/* for struct super_block,  address_space */

/* return reiser4 internal tree which inode belongs to */
/* Audited by: green(2002.06.17) */
reiser4_internal reiser4_tree *
tree_by_inode(const struct inode * inode /* inode queried */ )
{
	assert("nikita-256", inode != NULL);
	assert("nikita-257", inode->i_sb != NULL);
	return get_tree(inode->i_sb);
}

/* return reiser4-specific inode flags */
static inline unsigned long *
inode_flags(const struct inode * const inode)
{
	assert("nikita-2842", inode != NULL);
	return &reiser4_inode_data(inode)->flags;
}

/* set reiser4-specific flag @f in @inode */
reiser4_internal void
inode_set_flag(struct inode * inode, reiser4_file_plugin_flags f)
{
	assert("nikita-2248", inode != NULL);
	set_bit((int) f, inode_flags(inode));
}

/* clear reiser4-specific flag @f in @inode */
reiser4_internal void
inode_clr_flag(struct inode * inode, reiser4_file_plugin_flags f)
{
	assert("nikita-2250", inode != NULL);
	clear_bit((int) f, inode_flags(inode));
}

/* true if reiser4-specific flag @f is set in @inode */
reiser4_internal int
inode_get_flag(const struct inode * inode, reiser4_file_plugin_flags f)
{
	assert("nikita-2251", inode != NULL);
	return test_bit((int) f, inode_flags(inode));
}

/* convert oid to inode number */
reiser4_internal ino_t oid_to_ino(oid_t oid)
{
	return (ino_t) oid;
}

/* convert oid to user visible inode number */
reiser4_internal ino_t oid_to_uino(oid_t oid)
{
	/* reiser4 object is uniquely identified by oid which is 64 bit
	   quantity. Kernel in-memory inode is indexed (in the hash table) by
	   32 bit i_ino field, but this is not a problem, because there is a
	   way to further distinguish inodes with identical inode numbers
	   (find_actor supplied to iget()).

	   But user space expects unique 32 bit inode number. Obviously this
	   is impossible. Work-around is to somehow hash oid into user visible
	   inode number.
	*/
	oid_t max_ino = (ino_t) ~ 0;

	if (REISER4_INO_IS_OID || (oid <= max_ino))
		return oid;
	else
		/* this is remotely similar to algorithm used to find next pid
		   to use for process: after wrap-around start from some
		   offset rather than from 0. Idea is that there are some long
		   living objects with which we don't want to collide.
		*/
		return REISER4_UINO_SHIFT + ((oid - max_ino) & (max_ino >> 1));
}

/* check that "inode" is on reiser4 file-system */
reiser4_internal int
is_reiser4_inode(const struct inode *inode /* inode queried */ )
{
	return
		inode != NULL &&
		(is_reiser4_super(inode->i_sb) ||
		 inode->i_op == &reiser4_inode_operations);

}

/* Maximal length of a name that can be stored in directory @inode.

   This is used in check during file creation and lookup. */
reiser4_internal int
reiser4_max_filename_len(const struct inode *inode /* inode queried */ )
{
	assert("nikita-287", is_reiser4_inode(inode));
	assert("nikita-1710", inode_dir_item_plugin(inode));
	if (inode_dir_item_plugin(inode)->s.dir.max_name_len)
		return inode_dir_item_plugin(inode)->s.dir.max_name_len(inode);
	else
		return 255;
}

/* Maximal number of hash collisions for this directory. */
reiser4_internal int
max_hash_collisions(const struct inode *dir /* inode queried */ )
{
	assert("nikita-1711", dir != NULL);
#if REISER4_USE_COLLISION_LIMIT
	return reiser4_inode_data(dir)->plugin.max_collisions;
#else
	(void) dir;
	return ~0;
#endif
}

/* Install file, inode, and address_space operation on @inode, depending on
   its mode. */
reiser4_internal int
setup_inode_ops(struct inode *inode /* inode to intialise */ ,
		reiser4_object_create_data * data	/* parameters to create
							 * object */ )
{
	reiser4_super_info_data *sinfo;

	sinfo = get_super_private(inode->i_sb);

	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:{
			dev_t rdev;	/* to keep gcc happy */

			/* ugly hack with rdev */
			if (data == NULL) {
				rdev = inode->i_rdev;
				inode->i_rdev = 0;
			} else
				rdev = data->rdev;
			inode->i_blocks = 0;
			inode->i_op = &sinfo->ops.special;
			/* other fields are already initialised. */
			init_special_inode(inode, inode->i_mode, rdev);
			break;
		}
	case S_IFLNK:
		inode->i_op = &sinfo->ops.symlink;
		inode->i_fop = NULL;
		inode->i_mapping->a_ops = &sinfo->ops.as;
		break;
	case S_IFDIR:
		inode->i_op = &sinfo->ops.dir;
		inode->i_fop = &sinfo->ops.file;
		inode->i_mapping->a_ops = &sinfo->ops.as;
		break;
	case S_IFREG:
		inode->i_op = &sinfo->ops.regular;
		inode->i_fop = &sinfo->ops.file;
		inode->i_mapping->a_ops = &sinfo->ops.as;
		break;
	default:
		warning("nikita-291", "wrong file mode: %o for %llu", inode->i_mode,
			(unsigned long long)get_inode_oid(inode));
		reiser4_make_bad_inode(inode);
		return RETERR(-EINVAL);
	}
	return 0;
}

/* initialise inode from disk data. Called with inode locked.
    Return inode locked. */
static int
init_inode(struct inode *inode /* inode to intialise */ ,
	   coord_t * coord /* coord of stat data */ )
{
	int result;
	item_plugin *iplug;
	void *body;
	int length;
	reiser4_inode *state;

	assert("nikita-292", coord != NULL);
	assert("nikita-293", inode != NULL);

	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (result)
		return result;
	iplug = item_plugin_by_coord(coord);
	body = item_body_by_coord(coord);
	length = item_length_by_coord(coord);

	assert("nikita-295", iplug != NULL);
	assert("nikita-296", body != NULL);
	assert("nikita-297", length > 0);

	/* inode is under I_LOCK now */

	state = reiser4_inode_data(inode);
	/* call stat-data plugin method to load sd content into inode */
	result = iplug->s.sd.init_inode(inode, body, length);
	plugin_set_sd(&state->pset, iplug);
	if (result == 0) {
		result = setup_inode_ops(inode, NULL);
		if (result == 0 &&
		    inode->i_sb->s_root && inode->i_sb->s_root->d_inode) {
			struct inode *root;
			pset_member    ind;

			/* take missing plugins from file-system defaults */
			root = inode->i_sb->s_root->d_inode;
			/* file and directory plugins are already initialised. */
			for (ind = PSET_DIR + 1; ind < PSET_LAST; ++ind) {
				result = grab_plugin(inode, root, ind);
				if (result != 0)
					break;
			}
			if (result != 0) {
				warning("nikita-3447",
					"Cannot set up plugins for %lli",
					(unsigned long long)get_inode_oid(inode));
			}
		}
	}
	zrelse(coord->node);
	return result;
}

/* read `inode' from the disk. This is what was previously in
   reiserfs_read_inode2().

   Must be called with inode locked. Return inode still locked.
*/
static int
read_inode(struct inode *inode /* inode to read from disk */ ,
	   const reiser4_key * key /* key of stat data */,
	   int silent)
{
	int result;
	lock_handle lh;
	reiser4_inode *info;
	coord_t coord;

	assert("nikita-298", inode != NULL);
	assert("nikita-1945", !is_inode_loaded(inode));

	info = reiser4_inode_data(inode);
	assert("nikita-300", info->locality_id != 0);

	coord_init_zero(&coord);
	init_lh(&lh);
	/* locate stat-data in a tree and return znode locked */
	result = lookup_sd(inode, ZNODE_READ_LOCK, &coord, &lh, key, silent);
	assert("nikita-301", !is_inode_loaded(inode));
	if (result == 0) {
		/* use stat-data plugin to load sd into inode. */
		result = init_inode(inode, &coord);
		if (result == 0) {
			/* initialize stat-data seal */
			spin_lock_inode(inode);
			seal_init(&info->sd_seal, &coord, key);
			info->sd_coord = coord;
			spin_unlock_inode(inode);

			/* call file plugin's method to initialize plugin
			 * specific part of inode */
			if (inode_file_plugin(inode)->init_inode_data)
				inode_file_plugin(inode)->init_inode_data(inode,
									  NULL,
									  0);
			/* load detached directory cursors for stateless
			 * directory readers (NFS). */
			load_cursors(inode);

			/* Check the opened inode for consistency. */
			result = get_super_private(inode->i_sb)->df_plug->check_open(inode);
		}
	}
	/* lookup_sd() doesn't release coord because we want znode
	   stay read-locked while stat-data fields are accessed in
	   init_inode() */
	done_lh(&lh);

	if (result != 0)
		reiser4_make_bad_inode(inode);
	return result;
}

/* initialise new reiser4 inode being inserted into hash table. */
static int
init_locked_inode(struct inode *inode /* new inode */ ,
		  void *opaque	/* key of stat data passed to the
				 * iget5_locked as cookie */ )
{
	reiser4_key *key;

	assert("nikita-1995", inode != NULL);
	assert("nikita-1996", opaque != NULL);
	key = opaque;
	set_inode_oid(inode, get_key_objectid(key));
	reiser4_inode_data(inode)->locality_id = get_key_locality(key);
	return 0;
}

/* reiser4_inode_find_actor() - "find actor" supplied by reiser4 to iget5_locked().

   This function is called by iget5_locked() to distinguish reiser4 inodes
   having the same inode numbers. Such inodes can only exist due to some error
   condition. One of them should be bad. Inodes with identical inode numbers
   (objectids) are distinguished by their packing locality.

*/
reiser4_internal int
reiser4_inode_find_actor(struct inode *inode	/* inode from hash table to
						 * check */ ,
			 void *opaque	/* "cookie" passed to
					 * iget5_locked(). This is stat data
					 * key */ )
{
	reiser4_key *key;

	key = opaque;
	return
		/* oid is unique, so first term is enough, actually. */
		get_inode_oid(inode) == get_key_objectid(key) &&
		/*
		 * also, locality should be checked, but locality is stored in
		 * the reiser4-specific part of the inode, and actor can be
		 * called against arbitrary inode that happened to be in this
		 * hash chain. Hence we first have to check that this is
		 * reiser4 inode at least. is_reiser4_inode() is probably too
		 * early to call, as inode may have ->i_op not yet
		 * initialised.
		 */
		is_reiser4_super(inode->i_sb) &&
		/*
		 * usually objectid is unique, but pseudo files use counter to
		 * generate objectid. All pseudo files are placed into special
		 * (otherwise unused) locality.
		 */
		reiser4_inode_data(inode)->locality_id == get_key_locality(key);
}

/*
 * this is our helper function a la iget(). This is be called by
 * reiser4_lookup() and reiser4_read_super(). Return inode locked or error
 * encountered.
 */
reiser4_internal struct inode *
reiser4_iget(struct super_block *super /* super block  */ ,
	     const reiser4_key * key /* key of inode's stat-data */,
	     int silent)
{
	struct inode *inode;
	int result;
	reiser4_inode * info;

	assert("nikita-302", super != NULL);
	assert("nikita-303", key != NULL);

	result = 0;

	/* call iget(). Our ->read_inode() is dummy, so this will either
	    find inode in cache or return uninitialised inode */
	inode = iget5_locked(super,
			     (unsigned long) get_key_objectid(key),
			     reiser4_inode_find_actor,
			     init_locked_inode,
			     (reiser4_key *) key);
	if (inode == NULL)
		return ERR_PTR(RETERR(-ENOMEM));
	if (is_bad_inode(inode) && !silent) {
		warning("nikita-304", "Stat data not found");
		print_key("key", key);
		iput(inode);
		return ERR_PTR(RETERR(-EIO));
	}

	info = reiser4_inode_data(inode);

	/* Reiser4 inode state bit REISER4_LOADED is used to distinguish fully
	   loaded and initialized inode from just allocated inode. If
	   REISER4_LOADED bit is not set, reiser4_iget() completes loading under
	   info->loading.  The place in reiser4 which uses not initialized inode
	   is the reiser4 repacker, see repacker-related functions in
	   plugin/item/extent.c */
	if (!is_inode_loaded(inode)) {
		down(&info->loading);
		if (!is_inode_loaded(inode)) {
			/* locking: iget5_locked returns locked inode */
			assert("nikita-1941", !is_inode_loaded(inode));
			assert("nikita-1949",
			       reiser4_inode_find_actor(inode,
							(reiser4_key *)key));
			/* now, inode has objectid as ->i_ino and locality in
			   reiser4-specific part. This is enough for
			   read_inode() to read stat data from the disk */
			result = read_inode(inode, key, silent);
		}
	}

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);

	if (is_bad_inode(inode)) {
		up(&info->loading);
		iput(inode);
		inode = ERR_PTR(result);
	} else if (REISER4_DEBUG) {
		reiser4_key found_key;

		build_sd_key(inode, &found_key);
		if (!keyeq(&found_key, key)) {
			warning("nikita-305", "Wrong key in sd");
			print_key("sought for", key);
			print_key("found", &found_key);
		}
		if (inode_file_plugin(inode)->not_linked(inode)) {
			warning("nikita-3559", "Unlinked inode found: %llu\n",
				(unsigned long long)get_inode_oid(inode));
			print_inode("inode", inode);
		}
	}
	return inode;
}

/* reiser4_iget() may return not fully initialized inode, this function should
 * be called after one completes reiser4 inode initializing. */
reiser4_internal void reiser4_iget_complete (struct inode * inode)
{
	assert("zam-988", is_reiser4_inode(inode));

	if (!is_inode_loaded(inode)) {
		inode_set_flag(inode, REISER4_LOADED);
		up(&reiser4_inode_data(inode)->loading);
	}
}

reiser4_internal void
reiser4_make_bad_inode(struct inode *inode)
{
	assert("nikita-1934", inode != NULL);

	/* clear LOADED bit */
	inode_clr_flag(inode, REISER4_LOADED);
	make_bad_inode(inode);
	return;
}

reiser4_internal file_plugin *
inode_file_plugin(const struct inode * inode)
{
	assert("nikita-1997", inode != NULL);
	return reiser4_inode_data(inode)->pset->file;
}

reiser4_internal dir_plugin *
inode_dir_plugin(const struct inode * inode)
{
	assert("nikita-1998", inode != NULL);
	return reiser4_inode_data(inode)->pset->dir;
}

reiser4_internal perm_plugin *
inode_perm_plugin(const struct inode * inode)
{
	assert("nikita-1999", inode != NULL);
	return reiser4_inode_data(inode)->pset->perm;
}

reiser4_internal formatting_plugin *
inode_formatting_plugin(const struct inode * inode)
{
	assert("nikita-2000", inode != NULL);
	return reiser4_inode_data(inode)->pset->formatting;
}

reiser4_internal hash_plugin *
inode_hash_plugin(const struct inode * inode)
{
	assert("nikita-2001", inode != NULL);
	return reiser4_inode_data(inode)->pset->hash;
}

reiser4_internal fibration_plugin *
inode_fibration_plugin(const struct inode * inode)
{
	assert("nikita-2001", inode != NULL);
	return reiser4_inode_data(inode)->pset->fibration;
}

reiser4_internal crypto_plugin *
inode_crypto_plugin(const struct inode * inode)
{
	assert("edward-36", inode != NULL);
	return reiser4_inode_data(inode)->pset->crypto;
}

reiser4_internal compression_plugin *
inode_compression_plugin(const struct inode * inode)
{
	assert("edward-37", inode != NULL);
	return reiser4_inode_data(inode)->pset->compression;
}

reiser4_internal digest_plugin *
inode_digest_plugin(const struct inode * inode)
{
	assert("edward-86", inode != NULL);
	return reiser4_inode_data(inode)->pset->digest;
}

reiser4_internal item_plugin *
inode_sd_plugin(const struct inode * inode)
{
	assert("vs-534", inode != NULL);
	return reiser4_inode_data(inode)->pset->sd;
}

reiser4_internal item_plugin *
inode_dir_item_plugin(const struct inode * inode)
{
	assert("vs-534", inode != NULL);
	return reiser4_inode_data(inode)->pset->dir_item;
}

reiser4_internal void
inode_set_extension(struct inode *inode, sd_ext_bits ext)
{
	reiser4_inode *state;

	assert("nikita-2716", inode != NULL);
	assert("nikita-2717", ext < LAST_SD_EXTENSION);
	assert("nikita-3491",
	       spin_inode_object_is_locked(reiser4_inode_data(inode)));

	state = reiser4_inode_data(inode);
	state->extmask |= 1 << ext;
	/* force re-calculation of stat-data length on next call to
	   update_sd(). */
	inode_clr_flag(inode, REISER4_SDLEN_KNOWN);
}

reiser4_internal void
inode_set_plugin(struct inode *inode, reiser4_plugin * plug, pset_member memb)
{
	assert("nikita-2718", inode != NULL);
	assert("nikita-2719", plug != NULL);

	reiser4_inode_data(inode)->plugin_mask |= (1 << memb);
}

reiser4_internal void
inode_check_scale(struct inode *inode, __u64 old, __u64 new)
{
	assert("nikita-2875", inode != NULL);
	spin_lock_inode(inode);
	if (!dscale_fit(old, new))
		inode_clr_flag(inode, REISER4_SDLEN_KNOWN);
	spin_unlock_inode(inode);
}

/*
 * initialize ->ordering field of inode. This field defines how file stat-data
 * and body is ordered within a tree with respect to other objects within the
 * same parent directory.
 */
reiser4_internal void
init_inode_ordering(struct inode *inode,
		    reiser4_object_create_data *crd, int create)
{
	reiser4_key key;

	if (create) {
		struct inode *parent;

		parent = crd->parent;
		assert("nikita-3224", inode_dir_plugin(parent) != NULL);
		inode_dir_plugin(parent)->build_entry_key(parent,
							  &crd->dentry->d_name,
							  &key);
	} else {
		coord_t *coord;

		coord = &reiser4_inode_data(inode)->sd_coord;
		coord_clear_iplug(coord);
		/* safe to use ->sd_coord, because node is under long term
		 * lock */
		WITH_DATA(coord->node, item_key_by_coord(coord, &key));
	}

	set_inode_ordering(inode, get_key_ordering(&key));
}

reiser4_internal znode *
inode_get_vroot(struct inode *inode)
{
	reiser4_block_nr blk;
	znode *result;
	reiser4_inode *info;

	info = reiser4_inode_data(inode);
	LOCK_INODE(info);
	blk = info->vroot;
	UNLOCK_INODE(info);
	if (!disk_addr_eq(&UBER_TREE_ADDR, &blk))
		result = zlook(tree_by_inode(inode), &blk);
	else
		result = NULL;
	return result;
}

reiser4_internal void
inode_set_vroot(struct inode *inode, znode *vroot)
{
	reiser4_inode *info;

	info = reiser4_inode_data(inode);
	LOCK_INODE(info);
	info->vroot = *znode_get_block(vroot);
	UNLOCK_INODE(info);
}

reiser4_internal void
inode_clean_vroot(struct inode *inode)
{
	reiser4_inode *info;

	info = reiser4_inode_data(inode);
	LOCK_INODE(info);
	info->vroot = UBER_TREE_ADDR;
	UNLOCK_INODE(info);
}

reiser4_internal int
get_reiser4_inode_by_key (struct inode ** result, const reiser4_key * key)
{
	struct super_block * super = reiser4_get_current_sb();
	struct inode * inode;

	/* We do not need to read reiser4 inode from disk and initialize all
	 * reiser4 inode fields. */
	inode = iget_locked(super, (unsigned long)get_key_objectid(key));
	if (inode == NULL)
		return -ENOMEM;
	if (is_bad_inode(inode)) {
		iput(inode);
		return -EIO;
	}

	if (inode->i_state & I_NEW) {
		reiser4_inode * inode_data = reiser4_inode_data(inode);

		/* These inode fields are required for tree traversal. */
		set_inode_oid(inode, get_key_objectid(key));
		inode_data->locality_id = get_key_locality(key);
#if REISER4_LARGE_KEY
		inode_data->ordering = get_key_ordering(key);
#endif

		inode->i_mapping->a_ops = &reiser4_as_operations;
		unlock_new_inode(inode);
	}

	*result = inode;
	return 0;
}


#if REISER4_DEBUG_OUTPUT
/* Debugging aid: print information about inode. */
reiser4_internal void
print_inode(const char *prefix /* prefix to print */ ,
	    const struct inode *i /* inode to print */ )
{
	reiser4_key inode_key;
	reiser4_inode *ref;

	if (i == NULL) {
		printk("%s: inode: null\n", prefix);
		return;
	}
	printk("%s: ino: %lu, count: %i, link: %i, mode: %o, size: %llu\n",
	       prefix, i->i_ino, atomic_read(&i->i_count), i->i_nlink, i->i_mode, (unsigned long long) i->i_size);
	printk("\tuid: %i, gid: %i, dev: %i, rdev: %i\n", i->i_uid, i->i_gid, i->i_sb->s_dev, i->i_rdev);
	printk("\tatime: [%li,%li], mtime: [%li,%li], ctime: [%li,%li]\n",
	       i->i_atime.tv_sec, i->i_atime.tv_nsec,
	       i->i_mtime.tv_sec, i->i_mtime.tv_nsec,
	       i->i_ctime.tv_sec, i->i_ctime.tv_nsec);
	printk("\tblkbits: %i, blksize: %lu, blocks: %lu, bytes: %u\n",
	       i->i_blkbits, i->i_blksize, i->i_blocks, i->i_bytes);
	printk("\tversion: %lu, generation: %i, state: %lu, flags: %u\n",
	       i->i_version, i->i_generation, i->i_state, i->i_flags);
	printk("\tis_reiser4_inode: %i\n", is_reiser4_inode(i));
	print_key("\tkey", build_sd_key(i, &inode_key));
	ref = reiser4_inode_data(i);
	print_plugin("\tfile", file_plugin_to_plugin(ref->pset->file));
	print_plugin("\tdir", dir_plugin_to_plugin(ref->pset->dir));
	print_plugin("\tperm", perm_plugin_to_plugin(ref->pset->perm));
	print_plugin("\tformatting", formatting_plugin_to_plugin(ref->pset->formatting));
	print_plugin("\thash", hash_plugin_to_plugin(ref->pset->hash));
	print_plugin("\tsd", item_plugin_to_plugin(ref->pset->sd));

	/* FIXME-VS: this segfaults trying to print seal's coord */
	print_seal("\tsd_seal", &ref->sd_seal);
	print_coord("\tsd_coord", &ref->sd_coord, 0);
	printk("\tflags: %#lx, extmask: %#llx, pmask: %i, locality: %llu\n",
	       *inode_flags(i), ref->extmask,
	       ref->plugin_mask, ref->locality_id);
}
#endif

#if REISER4_DEBUG
void
inode_invariant(const struct inode *inode)
{
	reiser4_inode * object;

	object = reiser4_inode_data(inode);
	assert("nikita-3077", spin_inode_object_is_locked(object));

	spin_lock_eflush(inode->i_sb);

	assert("nikita-3146", object->anonymous_eflushed >= 0 && object->captured_eflushed >= 0);
	assert("nikita-3441", ergo(object->anonymous_eflushed > 0 || object->captured_eflushed > 0,
				   jnode_tree_by_reiser4_inode(object)->rnode != NULL));

	spin_unlock_eflush(inode->i_sb);
}

void
mark_inode_update(struct inode *object, int immediate)
{
	int i;
	int pos;
	reiser4_context *ctx;

	ctx = get_current_context();
	for (i = 0, pos = -1; i < TRACKED_DELAYED_UPDATE; ++i) {
		if (ctx->dirty[i].ino == object->i_ino) {
			pos = i;
			break;
		} else if (ctx->dirty[i].ino == 0)
			pos = i;
	}
	if (pos == -1)
		;/*warning("nikita-3402", "Too many delayed inode updates");*/
	else if (immediate) {
		ctx->dirty[pos].ino = 0;
	} else {
		ctx->dirty[pos].ino = object->i_ino;
		ctx->dirty[pos].delayed = 1;
#ifdef CONFIG_FRAME_POINTER
		ctx->dirty[pos].stack[0] = __builtin_return_address(0);
		ctx->dirty[pos].stack[1] = __builtin_return_address(1);
		ctx->dirty[pos].stack[2] = __builtin_return_address(2);
		ctx->dirty[pos].stack[3] = __builtin_return_address(3);
#endif
	}
}


int
delayed_inode_updates(dirty_inode_info info)
{
	int i;

	for (i = 0; i < TRACKED_DELAYED_UPDATE; ++i) {
		if (info[i].ino != 0 && info[i].delayed)
			return 1;
	}
	return 0;
}

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
