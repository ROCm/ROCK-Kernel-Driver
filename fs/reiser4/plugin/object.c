/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Examples of object plugins: file, directory, symlink, special file */
/* Plugins associated with inode:

   Plugin of inode is plugin referenced by plugin-id field of on-disk
   stat-data. How we store this plugin in in-core inode is not
   important. Currently pointers are used, another variant is to store
   offsets and do array lookup on each access.

   Now, each inode has one selected plugin: object plugin that
   determines what type of file this object is: directory, regular etc.

   This main plugin can use other plugins that are thus subordinated to
   it. Directory instance of object plugin uses hash; regular file
   instance uses tail policy plugin.

   Object plugin is either taken from id in stat-data or guessed from
   i_mode bits. Once it is established we ask it to install its
   subordinate plugins, by looking again in stat-data or inheriting them
   from parent.
*/
/* How new inode is initialized during ->read_inode():
    1 read stat-data and initialize inode fields: i_size, i_mode,
      i_generation, capabilities etc.
    2 read plugin id from stat data or try to guess plugin id
      from inode->i_mode bits if plugin id is missing.
    3 Call ->init_inode() method of stat-data plugin to initialise inode fields.

NIKITA-FIXME-HANS: can you say a little about 1 being done before 3?  What if stat data does contain i_size, etc., due to it being an unusual plugin?
    4 Call ->activate() method of object's plugin. Plugin is either read from
      from stat-data or guessed from mode bits
    5 Call ->inherit() method of object plugin to inherit as yet
NIKITA-FIXME-HANS: are you missing an "un" here?
initialized
      plugins from parent.

   Easy induction proves that on last step all plugins of inode would be
   initialized.

   When creating new object:
    1 obtain object plugin id (see next period)
NIKITA-FIXME-HANS: period?
    2 ->install() this plugin
    3 ->inherit() the rest from the parent

*/
/* We need some examples of creating an object with default and
  non-default plugin ids.  Nikita, please create them.
*/

#include "../forward.h"
#include "../debug.h"
#include "../key.h"
#include "../kassign.h"
#include "../coord.h"
#include "../seal.h"
#include "plugin_header.h"
#include "item/static_stat.h"
#include "file/file.h"
#include "file/pseudo.h"
#include "symlink.h"
#include "dir/dir.h"
#include "item/item.h"
#include "plugin.h"
#include "object.h"
#include "../znode.h"
#include "../tap.h"
#include "../tree.h"
#include "../vfs_ops.h"
#include "../inode.h"
#include "../super.h"
#include "../reiser4.h"
#include "../prof.h"
#include "../safe_link.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/quotaops.h>
#include <linux/security.h> /* security_inode_delete() */
#include <linux/writeback.h> /* wake_up_inode() */
#include <linux/xattr_acl.h>
#include <linux/xattr.h>

/* helper function to print errors */
static void
key_warning(const reiser4_key * key /* key to print */,
	    const struct inode *inode,
	    int code /* error code to print */)
{
	assert("nikita-716", key != NULL);

	if (code != -ENOMEM) {
		warning("nikita-717", "Error for inode %llu (%i)",
			get_key_objectid(key), code);
		print_key("for key", key);
		print_inode("inode", inode);
	}
}

/* NIKITA-FIXME-HANS: perhaps this function belongs in another file? */
#if REISER4_DEBUG
static void
check_inode_seal(const struct inode *inode,
		 const coord_t *coord, const reiser4_key *key)
{
	reiser4_key unit_key;

	unit_key_by_coord(coord, &unit_key);
	assert("nikita-2752",
	       WITH_DATA_RET(coord->node, 1, keyeq(key, &unit_key)));
	assert("nikita-2753", get_inode_oid(inode) == get_key_objectid(key));
}

static void
check_sd_coord(coord_t *coord, const reiser4_key *key)
{
	reiser4_key ukey;

	coord_clear_iplug(coord);
	if (zload(coord->node))
		return;

	if (!coord_is_existing_unit(coord) ||
	    !item_plugin_by_coord(coord) ||
	    !keyeq(unit_key_by_coord(coord, &ukey), key) ||
	    (znode_get_level(coord->node) != LEAF_LEVEL) ||
	    !item_is_statdata(coord)) {
		warning("nikita-1901", "Conspicuous seal");
		print_key("key", key);
		print_coord("coord", coord, 1);
		impossible("nikita-2877", "no way");
	}
	zrelse(coord->node);
}

#else
#define check_inode_seal(inode, coord, key) noop
#define check_sd_coord(coord, key) noop
#endif

/* find sd of inode in a tree, deal with errors */
reiser4_internal int
lookup_sd(struct inode *inode /* inode to look sd for */ ,
	  znode_lock_mode lock_mode /* lock mode */ ,
	  coord_t * coord /* resulting coord */ ,
	  lock_handle * lh /* resulting lock handle */ ,
	  const reiser4_key * key /* resulting key */,
	  int silent)
{
	int result;
	__u32 flags;

	assert("nikita-1692", inode != NULL);
	assert("nikita-1693", coord != NULL);
	assert("nikita-1694", key != NULL);

	/* look for the object's stat data in a tree.
	   This returns in "node" pointer to a locked znode and in "pos"
	   position of an item found in node. Both are only valid if
	   coord_found is returned. */
	flags = (lock_mode == ZNODE_WRITE_LOCK) ? CBK_FOR_INSERT : 0;
	flags |= CBK_UNIQUE;
	/*
	 * traverse tree to find stat data. We cannot use vroot here, because
	 * it only covers _body_ of the file, and stat data don't belong
	 * there.
	 */
	result = coord_by_key(tree_by_inode(inode),
			       key,
			       coord,
			       lh,
			       lock_mode,
			       FIND_EXACT,
			       LEAF_LEVEL,
			       LEAF_LEVEL,
			       flags,
			       0);
	if (REISER4_DEBUG && result == 0)
		check_sd_coord(coord, key);

	if (result != 0 && !silent)
		key_warning(key, inode, result);
	return result;
}

/* insert new stat-data into tree. Called with inode state
    locked. Return inode state locked. */
static int
insert_new_sd(struct inode *inode /* inode to create sd for */ )
{
	int result;
	reiser4_key key;
	coord_t coord;
	reiser4_item_data data;
	char *area;
	reiser4_inode *ref;
	lock_handle lh;
	oid_t oid;

	assert("nikita-723", inode != NULL);
	assert("nikita-3406", inode_get_flag(inode, REISER4_NO_SD));

	ref = reiser4_inode_data(inode);
	spin_lock_inode(inode);

	/*
	 * prepare specification of new item to be inserted
	 */

	data.iplug = inode_sd_plugin(inode);
	data.length = data.iplug->s.sd.save_len(inode);
	spin_unlock_inode(inode);

	data.data = NULL;
	data.user = 0;
/* could be optimized for case where there is only one node format in
 * use in the filesystem, probably there are lots of such
 * places we could optimize for only one node layout.... -Hans */
	if (data.length > tree_by_inode(inode)->nplug->max_item_size()) {
		/* This is silly check, but we don't know actual node where
		   insertion will go into. */
		return RETERR(-ENAMETOOLONG);
	}
	oid = oid_allocate(inode->i_sb);
/* NIKITA-FIXME-HANS: what is your opinion on whether this error check should be encapsulated into oid_allocate? */
	if (oid == ABSOLUTE_MAX_OID)
		return RETERR(-EOVERFLOW);

	set_inode_oid(inode, oid);

	coord_init_zero(&coord);
	init_lh(&lh);

	result = insert_by_key(tree_by_inode(inode),
			       build_sd_key(inode, &key),
			       &data,
			       &coord,
			       &lh,
			       /* stat data lives on a leaf level */
			       LEAF_LEVEL,
			       CBK_UNIQUE);

	/* we don't want to re-check that somebody didn't insert
	   stat-data while we were doing io, because if it did,
	   insert_by_key() returned error. */
	/* but what _is_ possible is that plugin for inode's stat-data,
	   list of non-standard plugins or their state would change
	   during io, so that stat-data wouldn't fit into sd. To avoid
	   this race we keep inode_state lock. This lock has to be
	   taken each time you access inode in a way that would cause
	   changes in sd size: changing plugins etc.
	*/

	if (result == IBK_INSERT_OK) {
		write_current_logf(WRITE_TREE_LOG, "..sd i %#llx %#llx",
				   get_inode_oid(inode), ref->locality_id);

		coord_clear_iplug(&coord);
		result = zload(coord.node);
		if (result == 0) {
			/* have we really inserted stat data? */
			assert("nikita-725", item_is_statdata(&coord));

			/* inode was just created. It is inserted into hash
			   table, but no directory entry was yet inserted into
			   parent. So, inode is inaccessible through
			   ->lookup(). All places that directly grab inode
			   from hash-table (like old knfsd), should check
			   IMMUTABLE flag that is set by common_create_child.
			*/
			assert("nikita-3240", data.iplug != NULL);
			assert("nikita-3241", data.iplug->s.sd.save != NULL);
			area = item_body_by_coord(&coord);
			result = data.iplug->s.sd.save(inode, &area);
			znode_make_dirty(coord.node);
			if (result == 0) {
				/* object has stat-data now */
				inode_clr_flag(inode, REISER4_NO_SD);
				inode_set_flag(inode, REISER4_SDLEN_KNOWN);
				/* initialise stat-data seal */
				seal_init(&ref->sd_seal, &coord, &key);
				ref->sd_coord = coord;
				check_inode_seal(inode, &coord, &key);
			} else if (result != -ENOMEM)
				/*
				 * convert any other error code to -EIO to
				 * avoid confusing user level with unexpected
				 * errors.
				 */
				result = RETERR(-EIO);
			zrelse(coord.node);
		}
	}
	done_lh(&lh);

	if (result != 0)
		key_warning(&key, inode, result);
	else
		oid_count_allocated();

	return result;
}


/* update stat-data at @coord */
static int
update_sd_at(struct inode * inode, coord_t * coord, reiser4_key * key,
	     lock_handle * lh)
{
	int                result;
	reiser4_item_data  data;
	char              *area;
	reiser4_inode     *state;
	znode             *loaded;

	state = reiser4_inode_data(inode);

	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (result != 0)
		return result;
	loaded = coord->node;

	spin_lock_inode(inode);
	assert("nikita-728", inode_sd_plugin(inode) != NULL);
	data.iplug = inode_sd_plugin(inode);

	/* if inode has non-standard plugins, add appropriate stat data
	 * extension */
	if (state->plugin_mask != 0)
		inode_set_extension(inode, PLUGIN_STAT);

	/* data.length is how much space to add to (or remove
	   from if negative) sd */
	if (!inode_get_flag(inode, REISER4_SDLEN_KNOWN)) {
		/* recalculate stat-data length */
		data.length =
			data.iplug->s.sd.save_len(inode) -
			item_length_by_coord(coord);
		inode_set_flag(inode, REISER4_SDLEN_KNOWN);
	} else
		data.length = 0;
	spin_unlock_inode(inode);

	/* if on-disk stat data is of different length than required
	   for this inode, resize it */
	if (data.length != 0) {
		data.data = NULL;
		data.user = 0;

		/* insertion code requires that insertion point (coord) was
		 * between units. */
		coord->between = AFTER_UNIT;
		result = resize_item(coord,
				     &data, key, lh, COPI_DONT_SHIFT_LEFT);
		if (result != 0) {
			key_warning(key, inode, result);
			zrelse(loaded);
			return result;
		}
		if (loaded != coord->node) {
			/* resize_item moved coord to another node. Zload it */
			zrelse(loaded);
			coord_clear_iplug(coord);
			result = zload(coord->node);
			if (result != 0)
				return result;
			loaded = coord->node;
		}
	}

	area = item_body_by_coord(coord);
	spin_lock_inode(inode);
	result = data.iplug->s.sd.save(inode, &area);
	znode_make_dirty(coord->node);

	/* re-initialise stat-data seal */

	/*
	 * coord.between was possibly skewed from AT_UNIT when stat-data size
	 * was changed and new extensions were pasted into item.
	 */
	coord->between = AT_UNIT;
	seal_init(&state->sd_seal, coord, key);
	state->sd_coord = *coord;
	spin_unlock_inode(inode);
	check_inode_seal(inode, coord, key);
	zrelse(loaded);
	return result;
}

reiser4_internal int
locate_inode_sd(struct inode *inode,
		reiser4_key *key,
		coord_t *coord,
		lock_handle *lh)
{
	reiser4_inode *state;
	seal_t seal;
	int result;

	assert("nikita-3483", inode != NULL);

	state = reiser4_inode_data(inode);
	spin_lock_inode(inode);
	*coord = state->sd_coord;
	coord_clear_iplug(coord);
	seal = state->sd_seal;
	spin_unlock_inode(inode);

	build_sd_key(inode, key);
	if (seal_is_set(&seal)) {
		/* first, try to use seal */
		result = seal_validate(&seal,
				       coord,
				       key,
				       LEAF_LEVEL,
				       lh,
				       FIND_EXACT,
				       ZNODE_WRITE_LOCK,
				       ZNODE_LOCK_LOPRI);
		if (result == 0)
			check_sd_coord(coord, key);
	} else
		result = -E_REPEAT;

	if (result != 0) {
		coord_init_zero(coord);
		result = lookup_sd(inode, ZNODE_WRITE_LOCK, coord, lh, key, 0);
	}
	return result;
}

/* Update existing stat-data in a tree. Called with inode state locked. Return
   inode state locked. */
static int
update_sd(struct inode *inode /* inode to update sd for */ )
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;

	assert("nikita-726", inode != NULL);

	/* no stat-data, nothing to update?! */
	assert("nikita-3482", !inode_get_flag(inode, REISER4_NO_SD));

	init_lh(&lh);

	result = locate_inode_sd(inode, &key, &coord, &lh);
	if (result == 0)
		result = update_sd_at(inode, &coord, &key, &lh);
	done_lh(&lh);

	return result;
}
/* NIKITA-FIXME-HANS: the distinction between writing and updating made in the function names seems muddled, please adopt a better function naming strategy */
/* save object's stat-data to disk */
reiser4_internal int
write_sd_by_inode_common(struct inode *inode /* object to save */)
{
	int result;

	assert("nikita-730", inode != NULL);

	mark_inode_update(inode, 1);

	if (inode_get_flag(inode, REISER4_NO_SD))
		/* object doesn't have stat-data yet */
		result = insert_new_sd(inode);
	else
		result = update_sd(inode);
	if (result != 0 && result != -ENAMETOOLONG && result != -ENOMEM)
		/* Don't issue warnings about "name is too long" */
		warning("nikita-2221", "Failed to save sd for %llu: %i",
			get_inode_oid(inode), result);
	return result;
}

/* checks whether yet another hard links to this object can be added */
reiser4_internal int
can_add_link_common(const struct inode *object /* object to check */ )
{
	assert("nikita-732", object != NULL);

	/* inode->i_nlink is unsigned int, so just check for integer
	 * overflow */
	return object->i_nlink + 1 != 0;
}

/* remove object stat data. Space for it must be reserved by caller before */
reiser4_internal int
common_object_delete_no_reserve(struct inode *inode /* object to remove */,
				int mode /* cut_mode */)
{
	int result;

	assert("nikita-1477", inode != NULL);

	if (!inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_key sd_key;

		DQUOT_FREE_INODE(inode);
		DQUOT_DROP(inode);

		build_sd_key(inode, &sd_key);
		write_current_logf(WRITE_TREE_LOG, "..sd k %#llx", get_inode_oid(inode));
		result = cut_tree(tree_by_inode(inode), &sd_key, &sd_key, NULL, mode);
		if (result == 0) {
			inode_set_flag(inode, REISER4_NO_SD);
			result = oid_release(inode->i_sb, get_inode_oid(inode));
			if (result == 0) {
				oid_count_released();

				result = safe_link_del(inode, SAFE_UNLINK);
			}
		}
	} else
		result = 0;
	return result;
}

/* delete object stat-data. This is to be used when file deletion turns into stat data removal */
reiser4_internal int
delete_object(struct inode *inode /* object to remove */, int mode /* cut mode */)
{
	int result;

	assert("nikita-1477", inode != NULL);
	assert("nikita-3420", inode->i_size == 0 || S_ISLNK(inode->i_mode));
	assert("nikita-3421", inode->i_nlink == 0);

	if (!inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_block_nr reserve;

		/* grab space which is needed to remove 2 items from the tree:
		 stat data and safe-link */
		reserve = 2 * estimate_one_item_removal(tree_by_inode(inode));
		if (reiser4_grab_space_force(reserve,
					     BA_RESERVED | BA_CAN_COMMIT))
			return RETERR(-ENOSPC);
		result = common_object_delete_no_reserve(inode, mode);
	} else
		result = 0;
	return result;
}

reiser4_internal int
delete_file_common(struct inode * inode)
{
	return delete_object(inode, 1);
}

/* common directory consists of two items: stat data and one item containing "." and ".." */
static int delete_directory_common(struct inode *inode)
{
	int result;
	dir_plugin *dplug;

	dplug = inode_dir_plugin(inode);
	assert("vs-1101", dplug && dplug->done);

	/* grab space enough for removing two items */
	if (reiser4_grab_space(2 * estimate_one_item_removal(tree_by_inode(inode)), BA_RESERVED | BA_CAN_COMMIT))
		return RETERR(-ENOSPC);

	result = dplug->done(inode);
	if (!result)
		result = common_object_delete_no_reserve(inode, 1);
	all_grabbed2free();
	return result;
}

/* ->set_plug_in_inode() default method. */
static int
set_plug_in_inode_common(struct inode *object /* inode to set plugin on */ ,
			 struct inode *parent /* parent object */ ,
			 reiser4_object_create_data * data	/* creational
								 * data */ )
{
	__u64 mask;

	object->i_mode = data->mode;
	/* this should be plugin decision */
	object->i_uid = current->fsuid;
	object->i_mtime = object->i_atime = object->i_ctime = CURRENT_TIME;
/* NIKITA-FIXME-HANS: which is defined as what where? */
	/* support for BSD style group-id assignment. */
	if (reiser4_is_set(object->i_sb, REISER4_BSD_GID))
		object->i_gid = parent->i_gid;
	else if (parent->i_mode & S_ISGID) {
		/* parent directory has sguid bit */
		object->i_gid = parent->i_gid;
		if (S_ISDIR(object->i_mode))
			/* sguid is inherited by sub-directories */
			object->i_mode |= S_ISGID;
	} else
		object->i_gid = current->fsgid;

	/* this object doesn't have stat-data yet */
	inode_set_flag(object, REISER4_NO_SD);
	/* setup inode and file-operations for this inode */
	setup_inode_ops(object, data);
	object->i_nlink = 0;
	seal_init(&reiser4_inode_data(object)->sd_seal, NULL, NULL);
	mask = (1 << UNIX_STAT) | (1 << LIGHT_WEIGHT_STAT);
	if (!reiser4_is_set(object->i_sb, REISER4_32_BIT_TIMES))
		mask |= (1 << LARGE_TIMES_STAT);

	reiser4_inode_data(object)->extmask = mask;
	return 0;
}

/* Determine object plugin for @inode based on i_mode.

   Many objects in reiser4 file system are controlled by standard object
   plugins that emulate traditional unix objects: unix file, directory, symlink, fifo, and so on.

   For such files we don't explicitly store plugin id in object stat
   data. Rather required plugin is guessed from mode bits, where file "type"
   is encoded (see stat(2)).
*/
reiser4_internal int
guess_plugin_by_mode(struct inode *inode	/* object to guess plugins
						 * for */ )
{
	int fplug_id;
	int dplug_id;
	reiser4_inode *info;

	assert("nikita-736", inode != NULL);

	dplug_id = fplug_id = -1;

	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		fplug_id = SPECIAL_FILE_PLUGIN_ID;
		break;
	case S_IFLNK:
		fplug_id = SYMLINK_FILE_PLUGIN_ID;
		break;
	case S_IFDIR:
		fplug_id = DIRECTORY_FILE_PLUGIN_ID;
		dplug_id = HASHED_DIR_PLUGIN_ID;
		break;
	default:
		warning("nikita-737", "wrong file mode: %o", inode->i_mode);
		return RETERR(-EIO);
	case S_IFREG:
		fplug_id = UNIX_FILE_PLUGIN_ID;
		break;
	}
	info = reiser4_inode_data(inode);
	plugin_set_file(&info->pset,
			(fplug_id >= 0) ? file_plugin_by_id(fplug_id) : NULL);
	plugin_set_dir(&info->pset,
		       (dplug_id >= 0) ? dir_plugin_by_id(dplug_id) : NULL);
	return 0;
}

/* this comon implementation of create estimation function may be used when object creation involves insertion of one item
   (usualy stat data) into tree */
static reiser4_block_nr estimate_create_file_common(struct inode *object)
{
	return estimate_one_insert_item(tree_by_inode(object));
}

/* this comon implementation of create directory estimation function may be used when directory creation involves
   insertion of two items (usualy stat data and item containing "." and "..") into tree */
static reiser4_block_nr estimate_create_dir_common(struct inode *object)
{
	return 2 * estimate_one_insert_item(tree_by_inode(object));
}

/* ->create method of object plugin */
static int
create_common(struct inode *object, struct inode *parent UNUSED_ARG,
	      reiser4_object_create_data * data UNUSED_ARG)
{
	reiser4_block_nr reserve;
	assert("nikita-744", object != NULL);
	assert("nikita-745", parent != NULL);
	assert("nikita-747", data != NULL);
	assert("nikita-748", inode_get_flag(object, REISER4_NO_SD));

	reserve = estimate_create_file_common(object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
		return RETERR(-ENOSPC);
	return write_sd_by_inode_common(object);
}

/* standard implementation of ->owns_item() plugin method: compare objectids
    of keys in inode and coord */
reiser4_internal int
owns_item_common(const struct inode *inode	/* object to check
						 * against */ ,
		 const coord_t * coord /* coord to check */ )
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert("nikita-760", inode != NULL);
	assert("nikita-761", coord != NULL);

	return			/*coord_is_in_node( coord ) && */
	    coord_is_existing_item(coord) &&
	    (get_key_objectid(build_sd_key(inode, &file_key)) == get_key_objectid(item_key_by_coord(coord, &item_key)));
}

/* @count bytes of flow @f got written, update correspondingly f->length,
   f->data and f->key */
reiser4_internal void
move_flow_forward(flow_t * f, unsigned count)
{
	if (f->data)
		f->data += count;
	f->length -= count;
	set_key_offset(&f->key, get_key_offset(&f->key) + count);
}

/* default ->add_link() method of file plugin */
static int
add_link_common(struct inode *object, struct inode *parent UNUSED_ARG)
{
	/*
	 * increment ->i_nlink and update ->i_ctime
	 */

	INODE_INC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* default ->rem_link() method of file plugin */
static int
rem_link_common(struct inode *object, struct inode *parent UNUSED_ARG)
{
	assert("nikita-2021", object != NULL);
	assert("nikita-2163", object->i_nlink > 0);

	/*
	 * decrement ->i_nlink and update ->i_ctime
	 */

	INODE_DEC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* ->not_linked() method for file plugins */
static int
not_linked_common(const struct inode *inode)
{
	assert("nikita-2007", inode != NULL);
	return (inode->i_nlink == 0);
}

/* ->not_linked() method the for directory file plugin */
static int
not_linked_dir(const struct inode *inode)
{
	assert("nikita-2008", inode != NULL);
	/* one link from dot */
	return (inode->i_nlink == 1);
}

/* ->adjust_to_parent() method for regular files */
static int
adjust_to_parent_common(struct inode *object /* new object */ ,
			struct inode *parent /* parent directory */ ,
			struct inode *root /* root directory */ )
{
	assert("nikita-2165", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2069", parent != NULL);

	/*
	 * inherit missing plugins from parent
	 */

	grab_plugin(object, parent, PSET_FILE);
	grab_plugin(object, parent, PSET_SD);
	grab_plugin(object, parent, PSET_FORMATTING);
	grab_plugin(object, parent, PSET_PERM);
	return 0;
}

/* ->adjust_to_parent() method for directory files */
static int
adjust_to_parent_dir(struct inode *object /* new object */ ,
		     struct inode *parent /* parent directory */ ,
		     struct inode *root /* root directory */ )
{
	int result = 0;
	pset_member memb;

	assert("nikita-2166", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2167", parent != NULL);

	/*
	 * inherit missing plugins from parent
	 */
	for (memb = 0; memb < PSET_LAST; ++ memb) {
		result = grab_plugin(object, parent, memb);
		if (result != 0)
			break;
	}
	return result;
}

/* simplest implementation of ->getattr() method. Completely static. */
static int
getattr_common(struct vfsmount *mnt UNUSED_ARG, struct dentry *dentry, struct kstat *stat)
{
	struct inode *obj;

	assert("nikita-2298", dentry != NULL);
	assert("nikita-2299", stat != NULL);
	assert("nikita-2300", dentry->d_inode != NULL);

	obj = dentry->d_inode;

	stat->dev = obj->i_sb->s_dev;
	stat->ino = oid_to_uino(get_inode_oid(obj));
	stat->mode = obj->i_mode;
	/* don't confuse userland with huge nlink. This is not entirely
	 * correct, because nlink_t is not necessary 16 bit signed. */
	stat->nlink = min(obj->i_nlink, (typeof(obj->i_nlink))0x7fff);
	stat->uid = obj->i_uid;
	stat->gid = obj->i_gid;
	stat->rdev = obj->i_rdev;
	stat->atime = obj->i_atime;
	stat->mtime = obj->i_mtime;
	stat->ctime = obj->i_ctime;
	stat->size = obj->i_size;
	stat->blocks = (inode_get_bytes(obj) + VFS_BLKSIZE - 1) >> VFS_BLKSIZE_BITS;
	/* "preferred" blocksize for efficient file system I/O */
	stat->blksize = get_super_private(obj->i_sb)->optimal_io_size;

	return 0;
}

/* plugin->u.file.release */
static int
release_dir(struct inode *inode, struct file *file)
{
	/* this is called when directory file descriptor is closed. */
	spin_lock_inode(inode);
	/* remove directory from readddir list. See comment before
	 * readdir_common() for details. */
	if (file->private_data != NULL)
		readdir_list_remove_clean(reiser4_get_file_fsdata(file));
	spin_unlock_inode(inode);
	return 0;
}

/* default implementation of ->bind() method of file plugin */
static int
bind_common(struct inode *child UNUSED_ARG, struct inode *parent UNUSED_ARG)
{
	return 0;
}

#define detach_common bind_common
#define cannot ((void *)bind_common)

static int
detach_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2883", dplug != NULL);
	assert("nikita-2884", dplug->detach != NULL);
	return dplug->detach(child, parent);
}


/* this common implementation of update estimation function may be used when stat data update does not do more than
   inserting a unit into a stat data item which is probably true for most cases */
reiser4_internal reiser4_block_nr
estimate_update_common(const struct inode *inode)
{
	return estimate_one_insert_into_item(tree_by_inode(inode));
}

static reiser4_block_nr
estimate_unlink_common(struct inode *object UNUSED_ARG,
		       struct inode *parent UNUSED_ARG)
{
	return 0;
}

static reiser4_block_nr
estimate_unlink_dir_common(struct inode *object, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(object);
	assert("nikita-2888", dplug != NULL);
	assert("nikita-2887", dplug->estimate.unlink != NULL);
	return dplug->estimate.unlink(object, parent);
}

/* implementation of ->bind() method for file plugin of directory file */
static int
bind_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2646", dplug != NULL);
	return dplug->attach(child, parent);
}

reiser4_internal int
setattr_reserve_common(reiser4_tree *tree)
{
	assert("vs-1096", is_grab_enabled(get_current_context()));
	return reiser4_grab_space(estimate_one_insert_into_item(tree),
				  BA_CAN_COMMIT);
}

/* ->setattr() method. This is called when inode attribute (including
 * ->i_size) is modified. */
reiser4_internal int
setattr_common(struct inode *inode /* Object to change attributes */,
	       struct iattr *attr /* change description */)
{
	int   result;

	assert("nikita-3119", !(attr->ia_valid & ATTR_SIZE));

	result = inode_change_ok(inode, attr);
	if (result)
		return result;

	/*
	 * grab disk space and call standard inode_setattr().
	 */
	result = setattr_reserve_common(tree_by_inode(inode));
	if (!result) {
		if ((attr->ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
		    (attr->ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid)) {
			result = DQUOT_TRANSFER(inode, attr) ? -EDQUOT : 0;
			if (result) {
				all_grabbed2free();
				return result;
			}
		}
		result = inode_setattr(inode, attr);
		if (!result)
			reiser4_update_sd(inode);
	}

	all_grabbed2free();
	return result;
}

/* doesn't seem to be exported in headers. */
extern spinlock_t inode_lock;

/* ->delete_inode() method. This is called by
 * iput()->iput_final()->drop_inode() when last reference to inode is released
 * and inode has no names. */
static void delete_inode_common(struct inode *object)
{
	/* create context here.
	 *
	 * removal of inode from the hash table (done at the very beginning of
	 * generic_delete_inode(), truncate of pages, and removal of file's
	 * extents has to be performed in the same atom. Otherwise, it may so
	 * happen, that twig node with unallocated extent will be flushed to
	 * the disk.
	 */
	reiser4_context ctx;

	/*
	 * FIXME: this resembles generic_delete_inode
	 */
	list_del_init(&object->i_list);
	list_del_init(&object->i_sb_list);
	object->i_state |= I_FREEING;
	inodes_stat.nr_inodes--;
	spin_unlock(&inode_lock);

	init_context(&ctx, object->i_sb);

	kill_cursors(object);

	if (!is_bad_inode(object)) {
		file_plugin *fplug;

		/* truncate object body */
		fplug = inode_file_plugin(object);
		if (fplug->pre_delete != NULL && fplug->pre_delete(object) != 0)
			warning("vs-1216", "Failed to delete file body %llu",
				get_inode_oid(object));
		else
			assert("vs-1430",
			       reiser4_inode_data(object)->anonymous_eflushed == 0 &&
			       reiser4_inode_data(object)->captured_eflushed == 0);
	}

	if (object->i_data.nrpages) {
		warning("vs-1434", "nrpages %ld\n", object->i_data.nrpages);
		truncate_inode_pages(&object->i_data, 0);
	}
	security_inode_delete(object);
	if (!is_bad_inode(object))
		DQUOT_INIT(object);

	object->i_sb->s_op->delete_inode(object);

	spin_lock(&inode_lock);
	hlist_del_init(&object->i_hash);
	spin_unlock(&inode_lock);
	wake_up_inode(object);
	if (object->i_state != I_CLEAR)
		BUG();
	destroy_inode(object);
	reiser4_exit_context(&ctx);
}

/*
 * ->forget_inode() method. Called by iput()->iput_final()->drop_inode() when
 * last reference to inode with names is released
 */
static void forget_inode_common(struct inode *object)
{
	generic_forget_inode(object);
}

/* ->drop_inode() method. Called by iput()->iput_final() when last reference
 * to inode is released */
static void drop_common(struct inode * object)
{
	file_plugin *fplug;

	assert("nikita-2643", object != NULL);

	/* -not- creating context in this method, because it is frequently
	   called and all existing ->not_linked() methods are one liners. */

	fplug = inode_file_plugin(object);
	/* fplug is NULL for fake inode */
	if (fplug != NULL && fplug->not_linked(object)) {
		assert("nikita-3231", fplug->delete_inode != NULL);
		fplug->delete_inode(object);
	} else {
		assert("nikita-3232", fplug->forget_inode != NULL);
		fplug->forget_inode(object);
	}
}

static ssize_t
isdir(void)
{
	return RETERR(-EISDIR);
}

#define eisdir ((void *)isdir)

static ssize_t
perm(void)
{
	return RETERR(-EPERM);
}

#define eperm ((void *)perm)

static int
can_rem_dir(const struct inode * inode)
{
	/* is_dir_empty() returns 0 is dir is empty */
	return !is_dir_empty(inode);
}

static int
process_truncate(struct inode *inode, __u64 size)
{
	int result;
	struct iattr attr;
	file_plugin *fplug;
	reiser4_context ctx;

	init_context(&ctx, inode->i_sb);

	attr.ia_size = size;
	attr.ia_valid = ATTR_SIZE | ATTR_CTIME;
	fplug = inode_file_plugin(inode);

	down(&inode->i_sem);
	result = fplug->setattr(inode, &attr);
	up(&inode->i_sem);

	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);

	return result;
}

reiser4_internal int
safelink_common(struct inode *object, reiser4_safe_link_t link, __u64 value)
{
	int result;

	if (link == SAFE_UNLINK)
		/* nothing to do. iput() in the caller (process_safelink) will
		 * finish with file */
		result = 0;
	else if (link == SAFE_TRUNCATE)
		result = process_truncate(object, value);
	else {
		warning("nikita-3438", "Unrecognized safe-link type: %i", link);
		result = RETERR(-EIO);
	}
	return result;
}

reiser4_internal int prepare_write_common (
	struct file * file, struct page * page, unsigned from, unsigned to)
{
	int result;
	file_plugin *fplug;
	struct inode *inode;

	assert("umka-3099", file != NULL);
	assert("umka-3100", page != NULL);
	assert("umka-3095", PageLocked(page));

	if (to - from == PAGE_CACHE_SIZE || PageUptodate(page))
		return 0;

	inode = page->mapping->host;
	fplug = inode_file_plugin(inode);

	if (fplug->readpage == NULL)
		return RETERR(-EINVAL);

	result = fplug->readpage(file, page);
	if (result != 0) {
		SetPageError(page);
		ClearPageUptodate(page);
		/* All reiser4 readpage() implementations should return the
		 * page locked in case of error. */
		assert("nikita-3472", PageLocked(page));
	} else {
		/*
		 * ->readpage() either:
		 *
		 *     1. starts IO against @page. @page is locked for IO in
		 *     this case.
		 *
		 *     2. doesn't start IO. @page is unlocked.
		 *
		 * In either case, page should be locked.
		 */
		lock_page(page);
		/*
		 * IO (if any) is completed at this point. Check for IO
		 * errors.
		 */
		if (!PageUptodate(page))
			result = RETERR(-EIO);
	}
	assert("umka-3098", PageLocked(page));
	return result;
}

reiser4_internal int
key_by_inode_and_offset_common(struct inode *inode, loff_t off, reiser4_key *key)
{
	key_init(key);
	set_key_locality(key, reiser4_inode_data(inode)->locality_id);
	set_key_ordering(key, get_inode_ordering(inode));
	set_key_objectid(key, get_inode_oid(inode));/*FIXME: inode->i_ino */
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) off);
	return 0;
}

/* default implementation of ->sync() method: commit all transactions */
static int
sync_common(struct inode *inode, int datasync)
{
	return txnmgr_force_commit_all(inode->i_sb, 0);
}

static int
wire_size_common(struct inode *inode)
{
	return inode_onwire_size(inode);
}

static char *
wire_write_common(struct inode *inode, char *start)
{
	return build_inode_onwire(inode, start);
}

static char *
wire_read_common(char *addr, reiser4_object_on_wire *obj)
{
	return extract_obj_key_id_from_onwire(addr, &obj->u.std.key_id);
}

static void
wire_done_common(reiser4_object_on_wire *obj)
{
	/* nothing to do */
}

static struct dentry *
wire_get_common(struct super_block *sb, reiser4_object_on_wire *obj)
{
	struct inode *inode;
	struct dentry *dentry;
	reiser4_key key;

	extract_key_from_id(&obj->u.std.key_id, &key);
	inode = reiser4_iget(sb, &key, 1);
	if (!IS_ERR(inode)) {
		reiser4_iget_complete(inode);
		dentry = d_alloc_anon(inode);
		if (dentry == NULL) {
			iput(inode);
			dentry = ERR_PTR(-ENOMEM);
		} else
			dentry->d_op = &get_super_private(sb)->ops.dentry;
	} else if (PTR_ERR(inode) == -ENOENT)
		/*
		 * inode wasn't found at the key encoded in the file
		 * handle. Hence, file handle is stale.
		 */
		dentry = ERR_PTR(RETERR(-ESTALE));
	else
		dentry = (void *)inode;
	return dentry;
}


static int
change_file(struct inode * inode, reiser4_plugin * plugin)
{
	/* cannot change object plugin of already existing object */
	return RETERR(-EINVAL);
}

static reiser4_plugin_ops file_plugin_ops = {
	.init     = NULL,
	.load     = NULL,
	.save_len = NULL,
	.save     = NULL,
	.change   = change_file
};


/*
 * Definitions of object plugins.
 */

file_plugin file_plugins[LAST_FILE_PLUGIN_ID] = {
	[UNIX_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = UNIX_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "reg",
			.desc = "regular file",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.open = NULL,
		.truncate = truncate_unix_file,
		.write_sd_by_inode = write_sd_by_inode_common,
		.capturepage = capturepage_unix_file,
		.readpage = readpage_unix_file,
		.capture = capture_unix_file,
		.read = read_unix_file,
		.write = write_unix_file,
		.release = release_unix_file,
		.ioctl = ioctl_unix_file,
		.mmap = mmap_unix_file,
		.get_block = get_block_unix_file,
		.flow_by_inode = flow_by_inode_unix_file,
		.key_by_inode = key_by_inode_unix_file,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_common,
		.delete = delete_file_common,
		.sync = sync_unix_file,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_unix_file,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_unix_file,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.wire = {
			 .write = wire_write_common,
			 .read  = wire_read_common,
			 .get   = wire_get_common,
			 .size  = wire_size_common,
			 .done  = wire_done_common
		 },
		.readpages = readpages_unix_file,
		.init_inode_data = init_inode_data_unix_file,
		.pre_delete = pre_delete_unix_file,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.destroy_inode = NULL,
		.forget_inode = forget_inode_common,
		.sendfile = sendfile_unix_file,
		.prepare_write = prepare_write_unix_file
	},
	[DIRECTORY_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = DIRECTORY_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "dir",
			.desc = "directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO},
		.open = NULL,
		.truncate = eisdir,
		.write_sd_by_inode = write_sd_by_inode_common,
		.capturepage = NULL,
		.readpage = eisdir,
		.capture = NULL,
		.read = eisdir,
		.write = eisdir,
		.release = release_dir,
		.ioctl = eisdir,
		.mmap = eisdir,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_dir,
		.create = create_common,
		.delete = delete_directory_common,
		.sync = sync_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_hashed,
		.can_add_link = can_add_link_common,
		.can_rem_link = can_rem_dir,
		.not_linked = not_linked_dir,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = seek_dir,
		.detach = detach_dir,
		.bind = bind_dir,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_dir_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_dir_common
		},
		.wire = {
			 .write = wire_write_common,
			 .read  = wire_read_common,
			 .get   = wire_get_common,
			 .size  = wire_size_common,
			 .done  = wire_done_common
		 },
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.destroy_inode = NULL,
		.forget_inode = forget_inode_common,
	},
	[SYMLINK_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = SYMLINK_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "symlink",
			.desc = "symbolic link",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.open = NULL,
		.truncate = eperm,
		.write_sd_by_inode = write_sd_by_inode_common,
		.capturepage = NULL,
		.readpage = eperm,
		.capture = NULL,
		.read = eperm,
		.write = eperm,
		.release = NULL,
		.ioctl = eperm,
		.mmap = eperm,
		.sync = sync_common,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_symlink,
		/* FIXME-VS: symlink should probably have its own destroy
		 * method */
		.delete = delete_file_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = NULL,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.wire = {
			 .write = wire_write_common,
			 .read  = wire_read_common,
			 .get   = wire_get_common,
			 .size  = wire_size_common,
			 .done  = wire_done_common
		 },
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.destroy_inode = destroy_inode_symlink,
		.forget_inode = forget_inode_common,
	},
	[SPECIAL_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = SPECIAL_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "special",
			.desc = "special: fifo, device or socket",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO}
		,
		.open = NULL,
		.truncate = eperm,
		.create = create_common,
		.write_sd_by_inode = write_sd_by_inode_common,
		.capturepage = NULL,
		.readpage = eperm,
		.capture = NULL,
		.read = eperm,
		.write = eperm,
		.release = NULL,
		.ioctl = eperm,
		.mmap = eperm,
		.sync = sync_common,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.delete = delete_file_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.wire = {
			 .write = wire_write_common,
			 .read  = wire_read_common,
			 .get   = wire_get_common,
			 .size  = wire_size_common,
			 .done  = wire_done_common
		 },
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.destroy_inode = NULL,
		.forget_inode = forget_inode_common,
	},
	[PSEUDO_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = PSEUDO_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "pseudo",
			.desc = "pseudo file",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.open =              open_pseudo,
		.truncate          = eperm,
		.write_sd_by_inode = eperm,
		.readpage          = eperm,
		.capturepage       = NULL,
		.capture           = NULL,
		.read              = read_pseudo,
		.write             = write_pseudo,
		.release           = release_pseudo,
		.ioctl             = eperm,
		.mmap              = eperm,
		.sync = sync_common,
		.get_block         = eperm,
		.flow_by_inode     = NULL,
		.key_by_inode      = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent  = NULL,
		.create            = NULL,
		.delete            = eperm,
		.add_link          = NULL,
		.rem_link          = NULL,
		.owns_item         = NULL,
		.can_add_link      = cannot,
		.can_rem_link      = cannot,
		.not_linked        = NULL,
		.setattr           = inode_setattr,
		.getattr           = getattr_common,
		.seek              = seek_pseudo,
		.detach            = detach_common,
		.bind              = bind_common,
		.safelink = NULL,
		.estimate = {
			.create = NULL,
			.update = NULL,
			.unlink = NULL
		},
		.wire = {
			 .write = wire_write_pseudo,
			 .read  = wire_read_pseudo,
			 .get   = wire_get_pseudo,
			 .size  = wire_size_pseudo,
			 .done  = wire_done_pseudo
		 },
		.readpages = NULL,
		.init_inode_data = NULL,
		.pre_delete = NULL,
		.drop = drop_pseudo,
		.delete_inode = NULL,
		.destroy_inode = NULL,
		.forget_inode = NULL,
	},
	[CRC_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = CRC_FILE_PLUGIN_ID,
			.pops = &cryptcompress_plugin_ops,
			.label = "cryptcompress",
			.desc = "cryptcompress file",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		/* FIXME: check which of these are relly needed */
		.open = open_cryptcompress,
		.truncate = truncate_cryptcompress,
		.write_sd_by_inode = write_sd_by_inode_common,
		.readpage = readpage_cryptcompress,
		.capturepage = NULL,
		.capture = capture_cryptcompress,
		.read = generic_file_read,
		.write = write_cryptcompress,
		.release = NULL,
		.ioctl = NULL,
		.mmap = generic_file_mmap,
		.get_block = get_block_cryptcompress,
		.sync = sync_common,
		.flow_by_inode = flow_by_inode_cryptcompress,
		.key_by_inode = key_by_inode_cryptcompress,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_cryptcompress,
		.delete = delete_cryptcompress,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_cryptcompress,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.wire = {
			 .write = wire_write_common,
			 .read  = wire_read_common,
			 .get   = wire_get_common,
			 .size  = wire_size_common,
			 .done  = wire_done_common
		 },
		.readpages = readpages_cryptcompress,
		.init_inode_data = init_inode_data_cryptcompress,
		.pre_delete = pre_delete_cryptcompress,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.destroy_inode = destroy_inode_cryptcompress,
		.forget_inode = forget_inode_common,
		.sendfile = sendfile_common,
		.prepare_write = prepare_write_common
	}
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
