/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map file
   names to the files. */

/* See fs/reiser4/doc/directory-service for initial design note. */

/*
 * Hashed directory logically consists of persistent directory
 * entries. Directory entry is a pair of a file name and a key of stat-data of
 * a file that has this name in the given directory.
 *
 * Directory entries are stored in the tree in the form of directory
 * items. Directory item should implement dir_entry_ops portion of item plugin
 * interface (see plugin/item/item.h). Hashed directory interacts with
 * directory item plugin exclusively through dir_entry_ops operations.
 *
 * Currently there are two implementations of directory items: "simple
 * directory item" (plugin/item/sde.[ch]), and "compound directory item"
 * (plugin/item/cde.[ch]) with the latter being the default.
 *
 * There is, however some delicate way through which directory code interferes
 * with item plugin: key assignment policy. A key for a directory item is
 * chosen by directory code, and as described in kassign.c, this key contains
 * a portion of file name. Directory item uses this knowledge to avoid storing
 * this portion of file name twice: in the key and in the directory item body.
 *
 */

#include "../../forward.h"
#include "../../debug.h"
#include "../../spin_macros.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../seal.h"
#include "dir.h"
#include "../item/item.h"
#include "../security/perm.h"
#include "../pseudo/pseudo.h"
#include "../plugin.h"
#include "../object.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tree.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../reiser4.h"
#include "../../safe_link.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

static int create_dot_dotdot(struct inode *object, struct inode *parent);
static int find_entry(struct inode *dir, struct dentry *name,
		      lock_handle * lh, znode_lock_mode mode,
		      reiser4_dir_entry_desc * entry);
static int check_item(const struct inode *dir,
		      const coord_t * coord, const char *name);

reiser4_internal reiser4_block_nr
hashed_estimate_init(struct inode *parent, struct inode *object)
{
	reiser4_block_nr res = 0;

	assert("vpf-321", parent != NULL);
	assert("vpf-322", object != NULL);

	/* hashed_add_entry(object) */
	res += inode_dir_plugin(object)->estimate.add_entry(object);
	/* reiser4_add_nlink(object) */
	res += inode_file_plugin(object)->estimate.update(object);
	/* hashed_add_entry(object) */
	res += inode_dir_plugin(object)->estimate.add_entry(object);
	/* reiser4_add_nlink(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);

	return 0;
}

/* plugin->u.dir.init
   create sd for directory file. Create stat-data, dot, and dotdot. */
reiser4_internal int
init_hashed(struct inode *object /* new directory */ ,
	    struct inode *parent /* parent directory */ ,
	    reiser4_object_create_data * data UNUSED_ARG	/* info passed
								 * to us, this
								 * is filled by
								 * reiser4()
								 * syscall in
								 * particular */ )
{
	reiser4_block_nr reserve;

	assert("nikita-680", object != NULL);
	assert("nikita-681", S_ISDIR(object->i_mode));
	assert("nikita-682", parent != NULL);
	assert("nikita-684", data != NULL);
	assert("nikita-686", data->id == DIRECTORY_FILE_PLUGIN_ID);
	assert("nikita-687", object->i_mode & S_IFDIR);
	trace_stamp(TRACE_DIR);

	reserve = hashed_estimate_init(parent, object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
		return RETERR(-ENOSPC);

	return create_dot_dotdot(object, parent);
}

static reiser4_block_nr
hashed_estimate_done(struct inode *object)
{
	reiser4_block_nr res = 0;

	/* hashed_rem_entry(object) */
	res += inode_dir_plugin(object)->estimate.rem_entry(object);
	return res;
}

/* plugin->u.dir.estimate.unlink */
reiser4_internal reiser4_block_nr
estimate_unlink_hashed(struct inode *parent, struct inode *object)
{
	reiser4_block_nr res = 0;

	/* hashed_rem_entry(object) */
	res += inode_dir_plugin(object)->estimate.rem_entry(object);
	/* del_nlink(parent) */
	res += 2 * inode_file_plugin(parent)->estimate.update(parent);

	return res;
}

/* ->delete() method of directory plugin
   plugin->u.dir.done
   Delete dot, and call common_file_delete() to delete stat data.
*/
reiser4_internal int
done_hashed(struct inode *object /* object being deleted */)
{
	int result;
	reiser4_block_nr reserve;
	struct dentry goodby_dots;
	reiser4_dir_entry_desc entry;

	assert("nikita-1449", object != NULL);

	if (inode_get_flag(object, REISER4_NO_SD))
		return 0;

	/* of course, this can be rewritten to sweep everything in one
	   cut_tree(). */
	xmemset(&entry, 0, sizeof entry);

	/* FIXME: this done method is called from delete_directory_common which
	 * reserved space already */
	reserve = hashed_estimate_done(object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT | BA_RESERVED))
		return RETERR(-ENOSPC);

	xmemset(&goodby_dots, 0, sizeof goodby_dots);
	entry.obj = goodby_dots.d_inode = object;
	goodby_dots.d_name.name = ".";
	goodby_dots.d_name.len = 1;
	result = rem_entry_hashed(object, &goodby_dots, &entry);
	reiser4_free_dentry_fsdata(&goodby_dots);
	if (unlikely(result != 0 && result != -ENOMEM && result != -ENOENT))
		/* only worth a warning

         		"values of B will give rise to dom!\n"
		             -- v6src/s2/mv.c:89
		*/
		warning("nikita-2252", "Cannot remove dot of %lli: %i",
			get_inode_oid(object), result);
	return 0;
}

/* ->detach() method of directory plugin
   plugin->u.dir.done
   Delete dotdot, decrease nlink on parent
*/
reiser4_internal int
detach_hashed(struct inode *object, struct inode *parent)
{
	int result;
	struct dentry goodby_dots;
	reiser4_dir_entry_desc entry;

	assert("nikita-2885", object != NULL);
	assert("nikita-2886", !inode_get_flag(object, REISER4_NO_SD));

	xmemset(&entry, 0, sizeof entry);

	/* NOTE-NIKITA this only works if @parent is -the- parent of
	   @object, viz. object whose key is stored in dotdot
	   entry. Wouldn't work with hard-links on directories. */
	xmemset(&goodby_dots, 0, sizeof goodby_dots);
	entry.obj = goodby_dots.d_inode = parent;
	goodby_dots.d_name.name = "..";
	goodby_dots.d_name.len = 2;
	result = rem_entry_hashed(object, &goodby_dots, &entry);
	reiser4_free_dentry_fsdata(&goodby_dots);
	if (result == 0) {
		/* the dot should be the only entry remaining at this time... */
		assert("nikita-3400", object->i_size == 1);
		/* and, together with the only name directory can have, they
		 * provides for the last 2 remaining references. If we get
		 * here as part of error handling during mkdir, @object
		 * possibly has no name yet, so its nlink == 1. If we get here
		 * from rename (targeting empty directory), it has no name
		 * already, so its nlink == 1. */
		assert("nikita-3401",
		       object->i_nlink == 2 || object->i_nlink == 1);

		reiser4_del_nlink(parent, object, 0);
	}
	return result;
}


/* ->owns_item() for hashed directory object plugin. */
reiser4_internal int
owns_item_hashed(const struct inode *inode /* object to check against */ ,
		 const coord_t * coord /* coord of item to check */ )
{
	reiser4_key item_key;

	assert("nikita-1335", inode != NULL);
	assert("nikita-1334", coord != NULL);

	if (item_type_by_coord(coord) == DIR_ENTRY_ITEM_TYPE)
		return get_key_locality(item_key_by_coord(coord, &item_key)) == get_inode_oid(inode);
	else
		return owns_item_common(inode, coord);
}

/* helper function for directory_file_create(). Create "." and ".." */
static int
create_dot_dotdot(struct inode *object	/* object to create dot and
					 * dotdot for */ ,
		  struct inode *parent /* parent of @object */ )
{
	int result;
	struct dentry dots_entry;
	reiser4_dir_entry_desc entry;

	assert("nikita-688", object != NULL);
	assert("nikita-689", S_ISDIR(object->i_mode));
	assert("nikita-691", parent != NULL);
	trace_stamp(TRACE_DIR);

	/* We store dot and dotdot as normal directory entries. This is
	   not necessary, because almost all information stored in them
	   is already in the stat-data of directory, the only thing
	   being missed is objectid of grand-parent directory that can
	   easily be added there as extension.

	   But it is done the way it is done, because not storing dot
	   and dotdot will lead to the following complications:

	   . special case handling in ->lookup().
	   . addition of another extension to the sd.
	   . dependency on key allocation policy for stat data.

	*/

	xmemset(&entry, 0, sizeof entry);
	xmemset(&dots_entry, 0, sizeof dots_entry);
	entry.obj = dots_entry.d_inode = object;
	dots_entry.d_name.name = ".";
	dots_entry.d_name.len = 1;
	result = add_entry_hashed(object, &dots_entry, NULL, &entry);
	reiser4_free_dentry_fsdata(&dots_entry);

	if (result == 0) {
		result = reiser4_add_nlink(object, object, 0);
		if (result == 0) {
			entry.obj = dots_entry.d_inode = parent;
			dots_entry.d_name.name = "..";
			dots_entry.d_name.len = 2;
			result = add_entry_hashed(object,
						  &dots_entry, NULL, &entry);
			reiser4_free_dentry_fsdata(&dots_entry);
			/* if creation of ".." failed, iput() will delete
			   object with ".". */
			if (result == 0) {
				result = reiser4_add_nlink(parent, object, 0);
				if (result != 0)
					/*
					 * if we failed to bump i_nlink, try
					 * to remove ".."
					 */
					detach_hashed(object, parent);
			}
		}
	}

	if (result != 0) {
		/*
		 * in the case of error, at least update stat-data so that,
		 * ->i_nlink updates are not lingering.
		 */
		reiser4_update_sd(object);
		reiser4_update_sd(parent);
	}

	return result;
}

/* looks for name specified in @dentry in directory @parent and if name is
   found - key of object found entry points to is stored in @entry->key */
static int
lookup_name_hashed(struct inode *parent /* inode of directory to lookup for
					 * name in */,
		   struct dentry *dentry /* name to look for */,
		   reiser4_key *key /* place to store key */)
{
	int result;
	coord_t *coord;
	lock_handle lh;
	const char *name;
	int len;
	reiser4_dir_entry_desc entry;
	reiser4_dentry_fsdata *fsdata;

	assert("nikita-1247", parent != NULL);
	assert("nikita-1248", dentry != NULL);
	assert("nikita-1123", dentry->d_name.name != NULL);
	assert("vs-1486",
	       dentry->d_op == &get_super_private(parent->i_sb)->ops.dentry);

	result = perm_chk(parent, lookup, parent, dentry);
	if (result != 0)
		return 0;

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	if (!is_name_acceptable(parent, name, len))
		/* some arbitrary error code to return */
		return RETERR(-ENAMETOOLONG);

	fsdata = reiser4_get_dentry_fsdata(dentry);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	coord = &fsdata->dec.entry_coord;
	coord_clear_iplug(coord);
	init_lh(&lh);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "lookup inode: %lli \"%s\"\n", get_inode_oid(parent), dentry->d_name.name);

	/* find entry in a directory. This is plugin method. */
	result = find_entry(parent, dentry, &lh, ZNODE_READ_LOCK, &entry);
	if (result == 0) {
		/* entry was found, extract object key from it. */
		result = WITH_COORD(coord, item_plugin_by_coord(coord)->s.dir.extract_key(coord, key));
	}
	done_lh(&lh);
	return result;

}

/*
 * helper for ->lookup() and ->get_parent() methods: if @inode is a
 * light-weight file, setup its credentials that are not stored in the
 * stat-data in this case
 */
static void
check_light_weight(struct inode *inode, struct inode *parent)
{
	if (inode_get_flag(inode, REISER4_LIGHT_WEIGHT)) {
		inode->i_uid = parent->i_uid;
		inode->i_gid = parent->i_gid;
		/* clear light-weight flag. If inode would be read by any
		   other name, [ug]id wouldn't change. */
		inode_clr_flag(inode, REISER4_LIGHT_WEIGHT);
	}
}

/* implementation of ->lookup() method for hashed directories. */
reiser4_internal int
lookup_hashed(struct inode * parent	/* inode of directory to
					 * lookup into */ ,
	      struct dentry **dentryloc /* name to look for */ )
{
	int result;
	struct inode *inode;
	struct dentry *dentry;
	reiser4_dir_entry_desc entry;

	dentry = *dentryloc;
	/* set up operations on dentry. */
	dentry->d_op = &get_super_private(parent->i_sb)->ops.dentry;

	result = lookup_name_hashed(parent, dentry, &entry.key);
	if (result == 0) {
		inode = reiser4_iget(parent->i_sb, &entry.key, 0);
		if (!IS_ERR(inode)) {
			check_light_weight(inode, parent);
			/* success */
			*dentryloc = d_splice_alias(inode, dentry);
			reiser4_iget_complete(inode);
		} else
			result = PTR_ERR(inode);
	} else if (result == -ENOENT)
		result = lookup_pseudo_file(parent, dentryloc);

	return result;
}

/*
 * ->get_parent() method of hashed directory. This is used by NFS kernel
 * server to "climb" up directory tree to check permissions.
 */
reiser4_internal struct dentry *
get_parent_hashed(struct inode *child)
{
	struct super_block *s;
	struct inode  *parent;
	struct dentry  dotdot;
	struct dentry *dentry;
	reiser4_key key;
	int         result;

	/*
	 * lookup dotdot entry.
	 */

	s = child->i_sb;
	memset(&dotdot, 0, sizeof(dotdot));
	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;
	dotdot.d_op = &get_super_private(s)->ops.dentry;

	result = lookup_name_hashed(child, &dotdot, &key);
	if (result != 0)
		return ERR_PTR(result);

	parent = reiser4_iget(s, &key, 1);
	if (!IS_ERR(parent)) {
		/*
		 * FIXME-NIKITA dubious: attributes are inherited from @child
		 * to @parent. But:
		 *
		 *     (*) this is the only this we can do
		 *
		 *     (*) attributes of light-weight object are inherited
		 *     from a parent through which object was looked up first,
		 *     so it is ambiguous anyway.
		 *
		 */
		check_light_weight(parent, child);
		reiser4_iget_complete(parent);
		dentry = d_alloc_anon(parent);
		if (dentry == NULL) {
			iput(parent);
			dentry = ERR_PTR(RETERR(-ENOMEM));
		} else
			dentry->d_op = &get_super_private(s)->ops.dentry;
	} else if (PTR_ERR(parent) == -ENOENT)
		dentry = ERR_PTR(RETERR(-ESTALE));
	else
		dentry = (void *)parent;
	return dentry;
}

static const char *possible_leak = "Possible disk space leak.";

/* re-bind existing name at @from_coord in @from_dir to point to @to_inode.

   Helper function called from hashed_rename() */
static int
replace_name(struct inode *to_inode	/* inode where @from_coord is
					 * to be re-targeted at */ ,
	     struct inode *from_dir	/* directory where @from_coord
					 * lives */ ,
	     struct inode *from_inode	/* inode @from_coord
					 * originally point to */ ,
	     coord_t * from_coord	/* where directory entry is in
					 * the tree */ ,
	     lock_handle * from_lh /* lock handle on @from_coord */ )
{
	item_plugin *from_item;
	int result;
	znode *node;

	coord_clear_iplug(from_coord);
	node = from_coord->node;
	result = zload(node);
	if (result != 0)
		return result;
	from_item = item_plugin_by_coord(from_coord);
	if (item_type_by_coord(from_coord) == DIR_ENTRY_ITEM_TYPE) {
		reiser4_key to_key;

		build_sd_key(to_inode, &to_key);

		/* everything is found and prepared to change directory entry
		   at @from_coord to point to @to_inode.

		   @to_inode is just about to get new name, so bump its link
		   counter.

		*/
		result = reiser4_add_nlink(to_inode, from_dir, 0);
		if (result != 0) {
			/* Don't issue warning: this may be plain -EMLINK */
			zrelse(node);
			return result;
		}

		result = from_item->s.dir.update_key(from_coord, &to_key, from_lh);
		if (result != 0) {
			reiser4_del_nlink(to_inode, from_dir, 0);
			zrelse(node);
			return result;
		}

		/* @from_inode just lost its name, he-he.

		   If @from_inode was directory, it contained dotdot pointing
		   to @from_dir. @from_dir i_nlink will be decreased when
		   iput() will be called on @from_inode.

		   If file-system is not ADG (hard-links are
		   supported on directories), iput(from_inode) will not remove
		   @from_inode, and thus above is incorrect, but hard-links on
		   directories are problematic in many other respects.
		*/
		result = reiser4_del_nlink(from_inode, from_dir, 0);
		if (result != 0) {
			warning("nikita-2330",
				"Cannot remove link from source: %i. %s",
				result, possible_leak);
		}
		/* Has to return success, because entry is already
		 * modified. */
		result = 0;

		/* NOTE-NIKITA consider calling plugin method in stead of
		   accessing inode fields directly. */
		from_dir->i_mtime = CURRENT_TIME;
	} else {
		warning("nikita-2326", "Unexpected item type");
		print_plugin("item", item_plugin_to_plugin(from_item));
		result = RETERR(-EIO);
	}
	zrelse(node);
	return result;
}

/* add new entry pointing to @inode into @dir at @coord, locked by @lh

   Helper function used by hashed_rename(). */
static int
add_name(struct inode *inode	/* inode where @coord is to be
				 * re-targeted at */ ,
	 struct inode *dir /* directory where @coord lives */ ,
	 struct dentry *name /* new name */ ,
	 coord_t * coord /* where directory entry is in the tree */ ,
	 lock_handle * lh /* lock handle on @coord */ ,
	 int is_dir /* true, if @inode is directory */ )
{
	int result;
	reiser4_dir_entry_desc entry;

	assert("nikita-2333", lh->node == coord->node);
	assert("nikita-2334", is_dir == S_ISDIR(inode->i_mode));

	xmemset(&entry, 0, sizeof entry);
	entry.obj = inode;
	/* build key of directory entry description */
	inode_dir_plugin(dir)->build_entry_key(dir, &name->d_name, &entry.key);

	/* ext2 does this in different order: first inserts new entry,
	   then increases directory nlink. We don't want do this,
	   because reiser4_add_nlink() calls ->add_link() plugin
	   method that can fail for whatever reason, leaving as with
	   cleanup problems.
	*/
	/* @inode is getting new name */
	reiser4_add_nlink(inode, dir, 0);
	/* create @new_name in @new_dir pointing to
	   @old_inode */
	result = WITH_COORD(coord,
			    inode_dir_item_plugin(dir)->s.dir.add_entry(dir,
									coord,
									lh,
									name,
									&entry));
	if (result != 0) {
		int result2;
		result2 = reiser4_del_nlink(inode, dir, 0);
		if (result2 != 0) {
			warning("nikita-2327", "Cannot drop link on %lli %i. %s",
				get_inode_oid(inode),
				result2, possible_leak);
		}
	} else
		INODE_INC_FIELD(dir, i_size);
	return result;
}

static reiser4_block_nr
hashed_estimate_rename(
	struct inode  *old_dir  /* directory where @old is located */,
	struct dentry *old_name /* old name */,
	struct inode  *new_dir  /* directory where @new is located */,
	struct dentry *new_name /* new name */)
{
	reiser4_block_nr res1, res2;
	dir_plugin *p_parent_old, *p_parent_new;
	file_plugin *p_child_old, *p_child_new;

	assert("vpf-311", old_dir != NULL);
	assert("vpf-312", new_dir != NULL);
	assert("vpf-313", old_name != NULL);
	assert("vpf-314", new_name != NULL);

	p_parent_old = inode_dir_plugin(old_dir);
	p_parent_new = inode_dir_plugin(new_dir);
	p_child_old = inode_file_plugin(old_name->d_inode);
	if (new_name->d_inode)
		p_child_new = inode_file_plugin(new_name->d_inode);
	else
		p_child_new = 0;

	/* find_entry - can insert one leaf. */
	res1 = res2 = 1;

	/* replace_name */
	{
		/* reiser4_add_nlink(p_child_old) and reiser4_del_nlink(p_child_old) */
		res1 += 2 * p_child_old->estimate.update(old_name->d_inode);
		/* update key */
		res1 += 1;
		/* reiser4_del_nlink(p_child_new) */
		if (p_child_new)
		    res1 += p_child_new->estimate.update(new_name->d_inode);
	}

	/* else add_name */
	{
		/* reiser4_add_nlink(p_parent_new) and reiser4_del_nlink(p_parent_new) */
		res2 += 2 * inode_file_plugin(new_dir)->estimate.update(new_dir);
		/* reiser4_add_nlink(p_parent_old) */
		res2 += p_child_old->estimate.update(old_name->d_inode);
		/* add_entry(p_parent_new) */
		res2 += p_parent_new->estimate.add_entry(new_dir);
		/* reiser4_del_nlink(p_parent_old) */
		res2 += p_child_old->estimate.update(old_name->d_inode);
	}

	res1 = res1 < res2 ? res2 : res1;


	/* reiser4_write_sd(p_parent_new) */
	res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);

	/* reiser4_write_sd(p_child_new) */
	if (p_child_new)
	    res1 += p_child_new->estimate.update(new_name->d_inode);

	/* hashed_rem_entry(p_parent_old) */
	res1 += p_parent_old->estimate.rem_entry(old_dir);

	/* reiser4_del_nlink(p_child_old) */
	res1 += p_child_old->estimate.update(old_name->d_inode);

	/* replace_name */
	{
	    /* reiser4_add_nlink(p_parent_dir_new) */
	    res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);
	    /* update_key */
	    res1 += 1;
	    /* reiser4_del_nlink(p_parent_new) */
	    res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);
	    /* reiser4_del_nlink(p_parent_old) */
	    res1 += inode_file_plugin(old_dir)->estimate.update(old_dir);
	}

	/* reiser4_write_sd(p_parent_old) */
	res1 += inode_file_plugin(old_dir)->estimate.update(old_dir);

	/* reiser4_write_sd(p_child_old) */
	res1 += p_child_old->estimate.update(old_name->d_inode);

	return res1;
}

static int
hashed_rename_estimate_and_grab(
	struct inode *old_dir /* directory where @old is located */ ,
	struct dentry *old_name /* old name */ ,
	struct inode *new_dir /* directory where @new is located */ ,
	struct dentry *new_name /* new name */ )
{
	reiser4_block_nr reserve;

	reserve = hashed_estimate_rename(old_dir, old_name, new_dir, new_name);

	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
		return RETERR(-ENOSPC);

	return 0;
}

/* check whether @old_inode and @new_inode can be moved within file system
 * tree. This singles out attempts to rename pseudo-files, for example. */
static int
can_rename(struct inode *old_dir, struct inode *old_inode,
	   struct inode *new_dir, struct inode *new_inode)
{
	file_plugin *fplug;
	dir_plugin  *dplug;

	assert("nikita-3370", old_inode != NULL);

	dplug = inode_dir_plugin(new_dir);
	fplug = inode_file_plugin(old_inode);

	if (dplug == NULL)
		return RETERR(-ENOTDIR);
	else if (dplug->create_child == NULL)
		return RETERR(-EPERM);
	else if (!fplug->can_add_link(old_inode))
		return RETERR(-EMLINK);
	else if (new_inode != NULL) {
		fplug = inode_file_plugin(new_inode);
		if (fplug->can_rem_link != NULL &&
		    !fplug->can_rem_link(new_inode))
			return RETERR(-EBUSY);
	}
	return 0;
}

/* ->rename directory plugin method implementation for hashed directories.
   plugin->u.dir.rename
   See comments in the body.

   It is arguable that this function can be made generic so, that it will be
   applicable to any kind of directory plugin that deals with directories
   composed out of directory entries. The only obstacle here is that we don't
   have any data-type to represent directory entry. This should be
   re-considered when more than one different directory plugin will be
   implemented.
*/
reiser4_internal int
rename_hashed(struct inode *old_dir /* directory where @old is located */ ,
	      struct dentry *old_name /* old name */ ,
	      struct inode *new_dir /* directory where @new is located */ ,
	      struct dentry *new_name /* new name */ )
{
	/* From `The Open Group Base Specifications Issue 6'


	   If either the old or new argument names a symbolic link, rename()
	   shall operate on the symbolic link itself, and shall not resolve
	   the last component of the argument. If the old argument and the new
	   argument resolve to the same existing file, rename() shall return
	   successfully and perform no other action.

	   [this is done by VFS: vfs_rename()]


	   If the old argument points to the pathname of a file that is not a
	   directory, the new argument shall not point to the pathname of a
	   directory.

	   [checked by VFS: vfs_rename->may_delete()]

	              If the link named by the new argument exists, it shall
	   be removed and old renamed to new. In this case, a link named new
	   shall remain visible to other processes throughout the renaming
	   operation and refer either to the file referred to by new or old
	   before the operation began.

	   [we should assure this]

	                               Write access permission is required for
	   both the directory containing old and the directory containing new.

	   [checked by VFS: vfs_rename->may_delete(), may_create()]

	   If the old argument points to the pathname of a directory, the new
	   argument shall not point to the pathname of a file that is not a
	   directory.

	   [checked by VFS: vfs_rename->may_delete()]

	              If the directory named by the new argument exists, it
	   shall be removed and old renamed to new. In this case, a link named
	   new shall exist throughout the renaming operation and shall refer
	   either to the directory referred to by new or old before the
	   operation began.

	   [we should assure this]

	                    If new names an existing directory, it shall be
	   required to be an empty directory.

	   [we should check this]

	   If the old argument points to a pathname of a symbolic link, the
	   symbolic link shall be renamed. If the new argument points to a
	   pathname of a symbolic link, the symbolic link shall be removed.

	   The new pathname shall not contain a path prefix that names
	   old. Write access permission is required for the directory
	   containing old and the directory containing new. If the old
	   argument points to the pathname of a directory, write access
	   permission may be required for the directory named by old, and, if
	   it exists, the directory named by new.

	   [checked by VFS: vfs_rename(), vfs_rename_dir()]

	   If the link named by the new argument exists and the file's link
	   count becomes 0 when it is removed and no process has the file
	   open, the space occupied by the file shall be freed and the file
	   shall no longer be accessible. If one or more processes have the
	   file open when the last link is removed, the link shall be removed
	   before rename() returns, but the removal of the file contents shall
	   be postponed until all references to the file are closed.

	   [iput() handles this, but we can do this manually, a la
	   reiser4_unlink()]

	   Upon successful completion, rename() shall mark for update the
	   st_ctime and st_mtime fields of the parent directory of each file.

	   [N/A]

	*/

	int result;
	int is_dir;		/* is @old_name directory */

	struct inode *old_inode;
	struct inode *new_inode;

	reiser4_dir_entry_desc old_entry;
	reiser4_dir_entry_desc new_entry;

	coord_t *new_coord;

	reiser4_dentry_fsdata *new_fsdata;

	lock_handle new_lh;

	dir_plugin  *dplug;
	file_plugin *fplug;

	assert("nikita-2318", old_dir != NULL);
	assert("nikita-2319", new_dir != NULL);
	assert("nikita-2320", old_name != NULL);
	assert("nikita-2321", new_name != NULL);

	old_inode = old_name->d_inode;
	new_inode = new_name->d_inode;

	dplug = inode_dir_plugin(old_dir);
	fplug = NULL;

	new_fsdata = reiser4_get_dentry_fsdata(new_name);
	if (IS_ERR(new_fsdata))
		return PTR_ERR(new_fsdata);

	new_coord = &new_fsdata->dec.entry_coord;
	coord_clear_iplug(new_coord);

	is_dir = S_ISDIR(old_inode->i_mode);

	assert("nikita-3461", old_inode->i_nlink >= 1 + !!is_dir);

	/* if target is existing directory and it's not empty---return error.

	   This check is done specifically, because is_dir_empty() requires
	   tree traversal and have to be done before locks are taken.
	*/
	if (is_dir && new_inode != NULL && is_dir_empty(new_inode) != 0)
		return RETERR(-ENOTEMPTY);

	result = can_rename(old_dir, old_inode, new_dir, new_inode);
	if (result != 0)
		return result;

	result = hashed_rename_estimate_and_grab(old_dir, old_name,
						 new_dir, new_name);
	if (result != 0)
	    return result;

	init_lh(&new_lh);

	/* find entry for @new_name */
	result = find_entry(new_dir,
			    new_name, &new_lh, ZNODE_WRITE_LOCK, &new_entry);

	if (IS_CBKERR(result)) {
		done_lh(&new_lh);
		return result;
	}

	seal_done(&new_fsdata->dec.entry_seal);

	/* add or replace name for @old_inode as @new_name */
	if (new_inode != NULL) {
		/* target (@new_name) exists. */
		/* Not clear what to do with objects that are
		   both directories and files at the same time. */
		if (result == CBK_COORD_FOUND) {
			result = replace_name(old_inode,
					      new_dir,
					      new_inode,
					      new_coord,
					      &new_lh);
			if (result == 0)
				fplug = inode_file_plugin(new_inode);
		} else if (result == CBK_COORD_NOTFOUND) {
			/* VFS told us that @new_name is bound to existing
			   inode, but we failed to find directory entry. */
			warning("nikita-2324", "Target not found");
			result = RETERR(-ENOENT);
		}
	} else {
		/* target (@new_name) doesn't exists. */
		if (result == CBK_COORD_NOTFOUND)
			result = add_name(old_inode,
					  new_dir,
					  new_name,
					  new_coord,
					  &new_lh, is_dir);
		else if (result == CBK_COORD_FOUND) {
			/* VFS told us that @new_name is "negative" dentry,
			   but we found directory entry. */
			warning("nikita-2331", "Target found unexpectedly");
			result = RETERR(-EIO);
		}
	}

	assert("nikita-3462", ergo(result == 0,
				   old_inode->i_nlink >= 2 + !!is_dir));

	/* We are done with all modifications to the @new_dir, release lock on
	   node. */
	done_lh(&new_lh);

	if (fplug != NULL) {
		/* detach @new_inode from name-space */
		result = fplug->detach(new_inode, new_dir);
		if (result != 0)
			warning("nikita-2330", "Cannot detach %lli: %i. %s",
				get_inode_oid(new_inode), result, possible_leak);
	}

	if (new_inode != NULL)
		reiser4_mark_inode_dirty(new_inode);

	if (result == 0) {
		xmemset(&old_entry, 0, sizeof old_entry);
		old_entry.obj = old_inode;

		dplug->build_entry_key(old_dir,
				       &old_name->d_name, &old_entry.key);

		/* At this stage new name was introduced for
		   @old_inode. @old_inode, @new_dir, and @new_inode i_nlink
		   counters were updated.

		   We want to remove @old_name now. If @old_inode wasn't
		   directory this is simple.
		*/
		result = rem_entry_hashed(old_dir, old_name, &old_entry);
		if (result != 0 && result != -ENOMEM) {
			warning("nikita-2335",
				"Cannot remove old name: %i", result);
		} else {
			result = reiser4_del_nlink(old_inode, old_dir, 0);
			if (result != 0 && result != -ENOMEM) {
				warning("nikita-2337",
					"Cannot drop link on old: %i", result);
			}
		}

		if (result == 0 && is_dir) {
			/* @old_inode is directory. We also have to update
			   dotdot entry. */
			coord_t *dotdot_coord;
			lock_handle dotdot_lh;
			struct dentry dotdot_name;
			reiser4_dir_entry_desc dotdot_entry;
			reiser4_dentry_fsdata  dataonstack;
			reiser4_dentry_fsdata *fsdata;

			xmemset(&dataonstack, 0, sizeof dataonstack);
			xmemset(&dotdot_entry, 0, sizeof dotdot_entry);
			dotdot_entry.obj = old_dir;
			xmemset(&dotdot_name, 0, sizeof dotdot_name);
			dotdot_name.d_name.name = "..";
			dotdot_name.d_name.len = 2;
			/*
			 * allocate ->d_fsdata on the stack to avoid using
			 * reiser4_get_dentry_fsdata(). Locking is not needed,
			 * because dentry is private to the current thread.
			 */
			dotdot_name.d_fsdata = &dataonstack;
			init_lh(&dotdot_lh);

			fsdata = &dataonstack;
			dotdot_coord = &fsdata->dec.entry_coord;
			coord_clear_iplug(dotdot_coord);

			result = find_entry(old_inode, &dotdot_name, &dotdot_lh,
					    ZNODE_WRITE_LOCK, &dotdot_entry);
			if (result == 0) {
				/* replace_name() decreases i_nlink on
				 * @old_dir */
				result = replace_name(new_dir,
						      old_inode,
						      old_dir,
						      dotdot_coord,
						      &dotdot_lh);
			} else
				result = RETERR(-EIO);
			done_lh(&dotdot_lh);
		}
	}
	reiser4_update_dir(new_dir);
	reiser4_update_dir(old_dir);
	reiser4_mark_inode_dirty(old_inode);
	if (result == 0) {
		file_plugin *fplug;

		if (new_inode != NULL) {
			/* add safe-link for target file (in case we removed
			 * last reference to the poor fellow */
			fplug = inode_file_plugin(new_inode);
			if (fplug->not_linked(new_inode))
				result = safe_link_add(new_inode, SAFE_UNLINK);
		}
	}
	return result;
}

/* ->add_entry() method for hashed directory object plugin.
   plugin->u.dir.add_entry
*/
reiser4_internal int
add_entry_hashed(struct inode *object	/* directory to add new name
					 * in */ ,
		 struct dentry *where /* new name */ ,
		 reiser4_object_create_data * data UNUSED_ARG	/* parameters
								 * of new
								 * object */ ,
		 reiser4_dir_entry_desc * entry	/* parameters of new
						 * directory entry */ )
{
	int result;
	coord_t *coord;
	lock_handle lh;
	reiser4_dentry_fsdata *fsdata;
	reiser4_block_nr       reserve;

	assert("nikita-1114", object != NULL);
	assert("nikita-1250", where != NULL);

	fsdata = reiser4_get_dentry_fsdata(where);
	if (unlikely(IS_ERR(fsdata)))
		return PTR_ERR(fsdata);

	reserve = inode_dir_plugin(object)->estimate.add_entry(object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
		return RETERR(-ENOSPC);

	init_lh(&lh);
	ON_TRACE(TRACE_DIR, "[%i]: creating \"%s\" in %llu\n", current->pid, where->d_name.name, get_inode_oid(object));
	coord = &fsdata->dec.entry_coord;
	coord_clear_iplug(coord);

	/* check for this entry in a directory. This is plugin method. */
	result = find_entry(object, where, &lh, ZNODE_WRITE_LOCK, entry);
	if (likely(result == -ENOENT)) {
		/* add new entry. Just pass control to the directory
		   item plugin. */
		assert("nikita-1709", inode_dir_item_plugin(object));
		assert("nikita-2230", coord->node == lh.node);
		seal_done(&fsdata->dec.entry_seal);
		result = inode_dir_item_plugin(object)->s.dir.add_entry(object, coord, &lh, where, entry);
		if (result == 0) {
			adjust_dir_file(object, where, fsdata->dec.pos + 1, +1);
			INODE_INC_FIELD(object, i_size);
		}
	} else if (result == 0) {
		assert("nikita-2232", coord->node == lh.node);
		result = RETERR(-EEXIST);
	}
	done_lh(&lh);

	return result;
}

/* ->rem_entry() method for hashed directory object plugin.
   plugin->u.dir.rem_entry
 */
reiser4_internal int
rem_entry_hashed(struct inode *object	/* directory from which entry
					 * is begin removed */ ,
		 struct dentry *where	/* name that is being
					 * removed */ ,
		 reiser4_dir_entry_desc * entry	/* description of entry being
						 * removed */ )
{
	int result;
	coord_t *coord;
	lock_handle lh;
	reiser4_dentry_fsdata *fsdata;
	__u64 tograb;

	/* yes, nested function, so what? Sue me. */
	int rem_entry(void) {
		item_plugin *iplug;
		struct inode *child;

		iplug = inode_dir_item_plugin(object);
		child = where->d_inode;
		assert("nikita-3399", child != NULL);

		/* check that we are really destroying an entry for @child */
		if (REISER4_DEBUG) {
			int result;
			reiser4_key key;

			result = iplug->s.dir.extract_key(coord, &key);
			if (result != 0)
				return result;
			if (get_key_objectid(&key) != get_inode_oid(child)) {
				warning("nikita-3397",
					"rem_entry: %#llx != %#llx\n",
					get_key_objectid(&key),
					get_inode_oid(child));
				return RETERR(-EIO);
			}
		}
		return iplug->s.dir.rem_entry(object,
					      &where->d_name, coord, &lh, entry);
	}

	assert("nikita-1124", object != NULL);
	assert("nikita-1125", where != NULL);

	tograb = inode_dir_plugin(object)->estimate.rem_entry(object);
	result = reiser4_grab_space(tograb, BA_CAN_COMMIT | BA_RESERVED);
	if (result != 0)
		return RETERR(-ENOSPC);

	init_lh(&lh);

	/* check for this entry in a directory. This is plugin method. */
	result = find_entry(object, where, &lh, ZNODE_WRITE_LOCK, entry);
	fsdata = reiser4_get_dentry_fsdata(where);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	coord = &fsdata->dec.entry_coord;

	assert("nikita-3404",
	       get_inode_oid(where->d_inode) != get_inode_oid(object) ||
	       object->i_size <= 1);

	coord_clear_iplug(coord);
	if (result == 0) {
		/* remove entry. Just pass control to the directory item
		   plugin. */
		assert("vs-542", inode_dir_item_plugin(object));
		seal_done(&fsdata->dec.entry_seal);
		adjust_dir_file(object, where, fsdata->dec.pos, -1);
		result = WITH_COORD(coord, rem_entry());
		if (result == 0) {
			if (object->i_size >= 1)
				INODE_DEC_FIELD(object, i_size);
			else {
				warning("nikita-2509", "Dir %llu is runt",
					get_inode_oid(object));
				result = RETERR(-EIO);
			}
			write_current_logf(WRITE_TREE_LOG,
					   "..de k %#llx %#llx %i %lli",
					   get_inode_oid(where->d_inode),
					   get_inode_oid(object),
					   where->d_inode->i_nlink,
					   where->d_inode->i_size);
			assert("nikita-3405", where->d_inode->i_nlink != 1 ||
			       where->d_inode->i_size != 2 ||
			       inode_dir_plugin(where->d_inode) == NULL);
		}
	}
	done_lh(&lh);

	return result;
}

static int entry_actor(reiser4_tree * tree /* tree being scanned */ ,
		       coord_t * coord /* current coord */ ,
		       lock_handle * lh /* current lock handle */ ,
		       void *args /* argument to scan */ );

/*
 * argument package used by entry_actor to scan entries with identical keys.
 */
typedef struct entry_actor_args {
	/* name we are looking for */
	const char *name;
	/* key of directory entry. entry_actor() scans through sequence of
	 * items/units having the same key */
	reiser4_key *key;
	/* how many entries with duplicate key was scanned so far. */
	int non_uniq;
#if REISER4_USE_COLLISION_LIMIT || REISER4_STATS
	/* scan limit */
	int max_non_uniq;
#endif
	/* return parameter: set to true, if ->name wasn't found */
	int not_found;
	/* what type of lock to take when moving to the next node during
	 * scan */
	znode_lock_mode mode;

	/* last coord that was visited during scan */
	coord_t last_coord;
	/* last node locked during scan */
	lock_handle last_lh;
	/* inode of directory */
	const struct inode *inode;
} entry_actor_args;

static int
check_entry(const struct inode *dir, coord_t *coord, const struct qstr *name)
{
	return WITH_COORD(coord, check_item(dir, coord, name->name));
}

/* Look for given @name within directory @dir.

   This is called during lookup, creation and removal of directory
   entries.

   First calculate key that directory entry for @name would have. Search
   for this key in the tree. If such key is found, scan all items with
   the same key, checking name in each directory entry along the way.
*/
static int
find_entry(struct inode *dir /* directory to scan */,
	   struct dentry *de /* name to search for */,
	   lock_handle * lh /* resulting lock handle */,
	   znode_lock_mode mode /* required lock mode */,
	   reiser4_dir_entry_desc * entry /* parameters of found directory
					   * entry */)
{
	const struct qstr *name;
	seal_t *seal;
	coord_t *coord;
	int result;
	__u32 flags;
	de_location *dec;
	reiser4_dentry_fsdata *fsdata;

	assert("nikita-1130", lh != NULL);
	assert("nikita-1128", dir != NULL);

	name = &de->d_name;
	assert("nikita-1129", name != NULL);

	/* dentry private data don't require lock, because dentry
	   manipulations are protected by i_sem on parent.

	   This is not so for inodes, because there is no -the- parent in
	   inode case.
	*/
	fsdata = reiser4_get_dentry_fsdata(de);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);
	dec = &fsdata->dec;

	coord = &dec->entry_coord;
	coord_clear_iplug(coord);
	seal = &dec->entry_seal;
	/* compose key of directory entry for @name */
	inode_dir_plugin(dir)->build_entry_key(dir, name, &entry->key);

	if (seal_is_set(seal)) {
		/* check seal */
		result = seal_validate(seal, coord, &entry->key, LEAF_LEVEL,
				       lh, FIND_EXACT, mode, ZNODE_LOCK_LOPRI);
		if (result == 0) {
			/* key was found. Check that it is really item we are
			   looking for. */
			result = check_entry(dir, coord, name);
			if (result == 0)
				return 0;
		}
	}
	flags = (mode == ZNODE_WRITE_LOCK) ? CBK_FOR_INSERT : 0;
	/*
	 * find place in the tree where directory item should be located.
	 */
	result = object_lookup(dir,
			       &entry->key,
			       coord,
			       lh,
			       mode,
			       FIND_EXACT,
			       LEAF_LEVEL,
			       LEAF_LEVEL,
			       flags,
			       0/*ra_info*/);

	if (result == CBK_COORD_FOUND) {
		entry_actor_args arg;

		/* fast path: no hash collisions */
		result = check_entry(dir, coord, name);
		if (result == 0) {
			seal_init(seal, coord, &entry->key);
			dec->pos = 0;
		} else if (result > 0) {
			/* Iterate through all units with the same keys. */
			arg.name = name->name;
			arg.key = &entry->key;
			arg.not_found = 0;
			arg.non_uniq = 0;
#if REISER4_USE_COLLISION_LIMIT
			arg.max_non_uniq = max_hash_collisions(dir);
			assert("nikita-2851", arg.max_non_uniq > 1);
#endif
			arg.mode = mode;
			arg.inode = dir;
			coord_init_zero(&arg.last_coord);
			init_lh(&arg.last_lh);

			result = iterate_tree(tree_by_inode(dir), coord, lh,
					      entry_actor, &arg, mode, 1);
			/* if end of the tree or extent was reached during
			   scanning. */
			if (arg.not_found || (result == -E_NO_NEIGHBOR)) {
				/* step back */
				done_lh(lh);

				result = zload(arg.last_coord.node);
				if (result == 0) {
					coord_clear_iplug(&arg.last_coord);
					coord_dup(coord, &arg.last_coord);
					move_lh(lh, &arg.last_lh);
					result = RETERR(-ENOENT);
					zrelse(arg.last_coord.node);
					--arg.non_uniq;
				}
			}

			done_lh(&arg.last_lh);
			if (result == 0)
				seal_init(seal, coord, &entry->key);

			if (result == 0 || result == -ENOENT) {
				assert("nikita-2580", arg.non_uniq > 0);
				dec->pos = arg.non_uniq - 1;
			}
		}
	} else
		dec->pos = -1;
	return result;
}

/* Function called by find_entry() to look for given name in the directory. */
static int
entry_actor(reiser4_tree * tree UNUSED_ARG /* tree being scanned */ ,
	    coord_t * coord /* current coord */ ,
	    lock_handle * lh /* current lock handle */ ,
	    void *entry_actor_arg /* argument to scan */ )
{
	reiser4_key unit_key;
	entry_actor_args *args;

	assert("nikita-1131", tree != NULL);
	assert("nikita-1132", coord != NULL);
	assert("nikita-1133", entry_actor_arg != NULL);

	args = entry_actor_arg;
	++args->non_uniq;
#if REISER4_USE_COLLISION_LIMIT
	if (args->non_uniq > args->max_non_uniq) {
		args->not_found = 1;
		/* hash collision overflow. */
		return RETERR(-EBUSY);
	}
#endif

	/*
	 * did we just reach the end of the sequence of items/units with
	 * identical keys?
	 */
	if (!keyeq(args->key, unit_key_by_coord(coord, &unit_key))) {
		assert("nikita-1791", keylt(args->key, unit_key_by_coord(coord, &unit_key)));
		args->not_found = 1;
		args->last_coord.between = AFTER_UNIT;
		return 0;
	}

	coord_dup(&args->last_coord, coord);
	/*
	 * did scan just moved to the next node?
	 */
	if (args->last_lh.node != lh->node) {
		int lock_result;

		/*
		 * if so, lock new node with the mode requested by the caller
		 */
		done_lh(&args->last_lh);
		assert("nikita-1896", znode_is_any_locked(lh->node));
		lock_result = longterm_lock_znode(&args->last_lh, lh->node,
						  args->mode, ZNODE_LOCK_HIPRI);
		if (lock_result != 0)
			return lock_result;
	}
	return check_item(args->inode, coord, args->name);
}

/*
 * return 0 iff @coord contains a directory entry for the file with the name
 * @name.
 */
static int
check_item(const struct inode *dir, const coord_t * coord, const char *name)
{
	item_plugin *iplug;
	char buf[DE_NAME_BUF_LEN];

	iplug = item_plugin_by_coord(coord);
 	if (iplug == NULL) {
 		warning("nikita-1135", "Cannot get item plugin");
 		print_coord("coord", coord, 1);
 		return RETERR(-EIO);
 	} else if (item_id_by_coord(coord) != item_id_by_plugin(inode_dir_item_plugin(dir))) {
 		/* item id of current item does not match to id of items a
 		   directory is built of */
 		warning("nikita-1136", "Wrong item plugin");
 		print_coord("coord", coord, 1);
 		print_plugin("plugin", item_plugin_to_plugin(iplug));
 		return RETERR(-EIO);
 	}
	assert("nikita-1137", iplug->s.dir.extract_name);

	ON_TRACE(TRACE_DIR, "[%i]: check_item: \"%s\", \"%s\" in %lli (%lli)\n",
		 current->pid, name, iplug->s.dir.extract_name(coord, buf),
		 get_inode_oid(dir), *znode_get_block(coord->node));
	/* Compare name stored in this entry with name we are looking for.

	   NOTE-NIKITA Here should go code for support of something like
	   unicode, code tables, etc.
	*/
	return !!strcmp(name, iplug->s.dir.extract_name(coord, buf));
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
