/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Methods of directory plugin. */

#include "../../forward.h"
#include "../../debug.h"
#include "../../spin_macros.h"
#include "../plugin_header.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../type_safe_list.h"
#include "../plugin.h"
#include "dir.h"
#include "../item/item.h"
#include "../security/perm.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tap.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../safe_link.h"
#include "../object.h"

#include "hashed_dir.h"
#include "pseudo_dir.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct file  */
#include <linux/quotaops.h>
#include <linux/dcache.h>	/* for struct dentry */

/* helper function. Standards require than for many file-system operations
   on success ctime and mtime of parent directory is to be updated. */
reiser4_internal int
reiser4_update_dir(struct inode *dir)
{
	assert("nikita-2525", dir != NULL);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	return reiser4_mark_inode_dirty(dir);
}

/* estimate disk space necessary to add a link from @parent to @object. */
static reiser4_block_nr common_estimate_link(
	struct inode *parent /* parent directory */,
	struct inode *object /* object to which new link is being cerated */)
{
	reiser4_block_nr res = 0;
	file_plugin *fplug;
	dir_plugin *dplug;

	assert("vpf-317", object != NULL);
	assert("vpf-318", parent != NULL );

	fplug = inode_file_plugin(object);
	dplug = inode_dir_plugin(parent);

	/* reiser4_add_nlink(object) */
	res += fplug->estimate.update(object);
	/* add_entry(parent) */
	res += dplug->estimate.add_entry(parent);
	/* reiser4_del_nlink(object) */
	res += fplug->estimate.update(object);
	/* update_dir(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);
	/* safe-link */
	res += estimate_one_item_removal(tree_by_inode(object));

	return res;
}

/* add link from @parent directory to @existing object.

       . get plugins
       . check permissions
       . check that "existing" can hold yet another link
       . start transaction
       . add link to "existing"
       . add entry to "parent"
       . if last step fails, remove link from "existing"

*/
static int
link_common(struct inode *parent /* parent directory */ ,
	    struct dentry *existing	/* dentry of object to which
					 * new link is being
					 * cerated */ ,
	    struct dentry *newname /* new name */ )
{
	int result;
	struct inode *object;
	dir_plugin *parent_dplug;
	reiser4_dir_entry_desc entry;
	reiser4_object_create_data data;
	reiser4_block_nr reserve;

	assert("nikita-1431", existing != NULL);
	assert("nikita-1432", parent != NULL);
	assert("nikita-1433", newname != NULL);

	object = existing->d_inode;
	assert("nikita-1434", object != NULL);

	/* check for race with create_object() */
	if (inode_get_flag(object, REISER4_IMMUTABLE))
		return RETERR(-E_REPEAT);

	/* links to directories are not allowed if file-system
	   logical name-space should be ADG */
	if (S_ISDIR(object->i_mode) && reiser4_is_set(parent->i_sb, REISER4_ADG))
		return RETERR(-EISDIR);

	/* check permissions */
	result = perm_chk(parent, link, existing, parent, newname);
	if (result != 0)
		return result;

	parent_dplug = inode_dir_plugin(parent);

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	data.mode = object->i_mode;
	data.id = inode_file_plugin(object)->h.id;

	reserve = common_estimate_link(parent, existing->d_inode);
	if ((__s64)reserve < 0)
	    return reserve;

	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
	    return RETERR(-ENOSPC);

	/*
	 * Subtle race handling: sys_link() doesn't take i_sem on @parent. It
	 * means that link(2) can race against unlink(2) or rename(2), and
	 * inode is dead (->i_nlink == 0) when reiser4_link() is entered.
	 *
	 * For such inode we have to undo special processing done in
	 * reiser4_unlink() viz. creation of safe-link.
	 */
	if (unlikely(inode_file_plugin(object)->not_linked(object))) {
		result = safe_link_del(object, SAFE_UNLINK);
		if (result != 0)
			return result;
	}

	result = reiser4_add_nlink(object, parent, 1);
	if (result == 0) {
		/* add entry to the parent */
		result = parent_dplug->add_entry(parent, newname, &data, &entry);
		if (result != 0) {
			/* failure to add entry to the parent, remove
			   link from "existing" */
			reiser4_del_nlink(object, parent, 1);
			/* now, if this fails, we have a file with too
			   big nlink---space leak, much better than
			   directory entry pointing to nowhere */
			/* may be it should be recorded somewhere, but
			   if addition of link to parent and update of
			   object's stat data both failed, chances are
			   that something is going really wrong */
		}
	}
	if (result == 0) {
		atomic_inc(&object->i_count);
		/* Upon successful completion, link() shall mark for update
		   the st_ctime field of the file. Also, the st_ctime and
		   st_mtime fields of the directory that contains the new
		   entry shall be marked for update. --SUS
		*/
		result = reiser4_update_dir(parent);
	}
	return result;
}

/* estimate disk space necessary to remove a link between @parent and
 * @object. */
static reiser4_block_nr common_estimate_unlink (
	struct inode *parent /* parent directory */,
	struct inode *object /* object to which new link is being cerated */)
{
	reiser4_block_nr res = 0;
	file_plugin *fplug;
	dir_plugin *dplug;

	assert("vpf-317", object != NULL);
	assert("vpf-318", parent != NULL );

	fplug = inode_file_plugin(object);
	dplug = inode_dir_plugin(parent);

	/* rem_entry(parent) */
	res += dplug->estimate.rem_entry(parent);
	/* reiser4_del_nlink(object) */
	res += fplug->estimate.update(object);
	/* update_dir(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);
	/* fplug->unlink */
	res += fplug->estimate.unlink(object, parent);
	/* safe-link */
	res += estimate_one_insert_item(tree_by_inode(object));

	return res;
}

/* grab space for unlink. */
static int
unlink_check_and_grab(struct inode *parent, struct dentry *victim)
{
	file_plugin  *fplug;
	struct inode *child;
	int           result;

	result = 0;
	child = victim->d_inode;
	fplug = inode_file_plugin(child);

	/* check for race with create_object() */
	if (inode_get_flag(child, REISER4_IMMUTABLE))
		return RETERR(-E_REPEAT);
	/* object being deleted should have stat data */
	assert("vs-949", !inode_get_flag(child, REISER4_NO_SD));

	/* check permissions */
	result = perm_chk(parent, unlink, parent, victim);
	if (result != 0)
		return result;

	/* ask object plugin */
	if (fplug->can_rem_link != NULL && !fplug->can_rem_link(child))
		return RETERR(-ENOTEMPTY);

	result = (int)common_estimate_unlink(parent, child);
	if (result < 0)
		return result;

	return reiser4_grab_reserved(child->i_sb, result, BA_CAN_COMMIT);
}

/* remove link from @parent directory to @victim object.

       . get plugins
       . find entry in @parent
       . check permissions
       . decrement nlink on @victim
       . if nlink drops to 0, delete object
*/
static int
unlink_common(struct inode *parent /* parent object */ ,
	      struct dentry *victim /* name being removed from @parent */)
{
	int           result;
	struct inode *object;
	file_plugin  *fplug;

	object = victim->d_inode;
	fplug  = inode_file_plugin(object);
	assert("nikita-2882", fplug->detach != NULL);

	result = unlink_check_and_grab(parent, victim);
	if (result != 0)
		return result;

	result = fplug->detach(object, parent);
	if (result == 0) {
		dir_plugin            *parent_dplug;
		reiser4_dir_entry_desc entry;

		parent_dplug = inode_dir_plugin(parent);
		xmemset(&entry, 0, sizeof entry);

		/* first, delete directory entry */
		result = parent_dplug->rem_entry(parent, victim, &entry);
		if (result == 0) {
			/*
			 * if name was removed successfully, we _have_ to
			 * return 0 from this function, because upper level
			 * caller (vfs_{rmdir,unlink}) expect this.
			 */
			/* now that directory entry is removed, update
			 * stat-data */
		        reiser4_del_nlink(object, parent, 1);
			/* Upon successful completion, unlink() shall mark for
			   update the st_ctime and st_mtime fields of the
			   parent directory. Also, if the file's link count is
			   not 0, the st_ctime field of the file shall be
			   marked for update. --SUS */
			reiser4_update_dir(parent);
			/* add safe-link for this file */
			if (fplug->not_linked(object))
				safe_link_add(object, SAFE_UNLINK);
		}
	}

	if (unlikely(result != 0)) {
		if (result != -ENOMEM)
			warning("nikita-3398", "Cannot unlink %llu (%i)",
				get_inode_oid(object), result);
		/* if operation failed commit pending inode modifications to
		 * the stat-data */
		reiser4_update_sd(object);
		reiser4_update_sd(parent);
	}

	reiser4_release_reserved(object->i_sb);

	/* @object's i_ctime was updated by ->rem_link() method(). */

	return result;
}

/* Estimate the maximum amount of nodes will be allocated or changed for:
   - insert an in the parent entry
   - update the SD of parent
   - estimate child creation
*/
static reiser4_block_nr common_estimate_create_child(
	struct inode *parent, /* parent object */
	struct inode *object /* object */)
{
	assert("vpf-309", parent != NULL);
	assert("vpf-307", object != NULL);

	return
		/* object creation estimation */
		inode_file_plugin(object)->estimate.create(object) +
		/* stat data of parent directory estimation */
		inode_file_plugin(parent)->estimate.update(parent) +
		/* adding entry estimation */
		inode_dir_plugin(parent)->estimate.add_entry(parent) +
		/* to undo in the case of failure */
		inode_dir_plugin(parent)->estimate.rem_entry(parent);
}

/* Create child in directory.

   . get object's plugin
   . get fresh inode
   . initialize inode
   . add object's stat-data
   . initialize object's directory
   . add entry to the parent
   . instantiate dentry

*/
/* ->create_child method of directory plugin */
static int
create_child_common(reiser4_object_create_data * data	/* parameters
							 * of new
							 * object */,
		    struct inode ** retobj)
{
	int result;

	struct dentry *dentry;	/* parent object */
	struct inode *parent;	/* new name */

	dir_plugin *par_dir;	/* directory plugin on the parent */
	dir_plugin *obj_dir;	/* directory plugin on the new object */
	file_plugin *obj_plug;	/* object plugin on the new object */
	struct inode *object;	/* new object */
	reiser4_block_nr reserve;

	reiser4_dir_entry_desc entry;	/* new directory entry */

	assert("nikita-1420", data != NULL);
	parent = data->parent;
	dentry = data->dentry;

	assert("nikita-1418", parent != NULL);
	assert("nikita-1419", dentry != NULL);
	par_dir = inode_dir_plugin(parent);
	/* check permissions */
	result = perm_chk(parent, create, parent, dentry, data);
	if (result != 0)
		return result;

	/* check, that name is acceptable for parent */
	if (par_dir->is_name_acceptable &&
	    !par_dir->is_name_acceptable(parent,
					 dentry->d_name.name,
					 (int) dentry->d_name.len))
		return RETERR(-ENAMETOOLONG);

	result = 0;
	obj_plug = file_plugin_by_id((int) data->id);
	if (obj_plug == NULL) {
		warning("nikita-430", "Cannot find plugin %i", data->id);
		return RETERR(-ENOENT);
	}
	object = new_inode(parent->i_sb);
	if (object == NULL)
		return RETERR(-ENOMEM);
	/* we'll update i_nlink below */
	object->i_nlink = 0;
	/* new_inode() initializes i_ino to "arbitrary" value. Reset it to 0,
	 * to simplify error handling: if some error occurs before i_ino is
	 * initialized with oid, i_ino should already be set to some
	 * distinguished value. */
	object->i_ino = 0;

	/* So that on error iput will be called. */
	*retobj = object;

	if (DQUOT_ALLOC_INODE(object)) {
		DQUOT_DROP(object);
		object->i_flags |= S_NOQUOTA;
		return RETERR(-EDQUOT);
	}

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	plugin_set_file(&reiser4_inode_data(object)->pset, obj_plug);
	result = obj_plug->set_plug_in_inode(object, parent, data);
	if (result) {
		warning("nikita-431", "Cannot install plugin %i on %llx",
			data->id, get_inode_oid(object));
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return result;
	}

	/* reget plugin after installation */
	obj_plug = inode_file_plugin(object);

	if (obj_plug->create == NULL) {
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return RETERR(-EPERM);
	}

	/* if any of hash, tail, sd or permission plugins for newly created
	   object are not set yet set them here inheriting them from parent
	   directory
	*/
	assert("nikita-2070", obj_plug->adjust_to_parent != NULL);
	result = obj_plug->adjust_to_parent(object,
					    parent,
					    object->i_sb->s_root->d_inode);
	if (result != 0) {
		warning("nikita-432", "Cannot inherit from %llx to %llx",
			get_inode_oid(parent), get_inode_oid(object));
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return result;
	}

	/* call file plugin's method to initialize plugin specific part of
	 * inode */
	if (obj_plug->init_inode_data)
		obj_plug->init_inode_data(object, data, 1/*create*/);

	/* obtain directory plugin (if any) for new object. */
	obj_dir = inode_dir_plugin(object);
	if (obj_dir != NULL && obj_dir->init == NULL) {
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return RETERR(-EPERM);
	}

	reiser4_inode_data(object)->locality_id = get_inode_oid(parent);

	reserve = common_estimate_create_child(parent, object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT)) {
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return RETERR(-ENOSPC);
	}

	/* mark inode `immutable'. We disable changes to the file being
	   created until valid directory entry for it is inserted. Otherwise,
	   if file were expanded and insertion of directory entry fails, we
	   have to remove file, but we only alloted enough space in
	   transaction to remove _empty_ file. 3.x code used to remove stat
	   data in different transaction thus possibly leaking disk space on
	   crash. This all only matters if it's possible to access file
	   without name, for example, by inode number
	*/
	inode_set_flag(object, REISER4_IMMUTABLE);

	/* create empty object, this includes allocation of new objectid. For
	   directories this implies creation of dot and dotdot  */
	assert("nikita-2265", inode_get_flag(object, REISER4_NO_SD));

	/* mark inode as `loaded'. From this point onward
	   reiser4_delete_inode() will try to remove its stat-data. */
	inode_set_flag(object, REISER4_LOADED);

	result = obj_plug->create(object, parent, data);
	if (result != 0) {
		inode_clr_flag(object, REISER4_IMMUTABLE);
		if (result != -ENAMETOOLONG && result != -ENOMEM)
			warning("nikita-2219",
				"Failed to create sd for %llu",
				get_inode_oid(object));
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		return result;
	}

	if (obj_dir != NULL)
		result = obj_dir->init(object, parent, data);
	if (result == 0) {
		assert("nikita-434", !inode_get_flag(object, REISER4_NO_SD));
		/* insert inode into VFS hash table */
		insert_inode_hash(object);
		/* create entry */
		result = par_dir->add_entry(parent, dentry, data, &entry);
		if (result == 0) {
			result = reiser4_add_nlink(object, parent, 0);
			/* If O_CREAT is set and the file did not previously
			   exist, upon successful completion, open() shall
			   mark for update the st_atime, st_ctime, and
			   st_mtime fields of the file and the st_ctime and
			   st_mtime fields of the parent directory. --SUS
			*/
			/* @object times are already updated by
			   reiser4_add_nlink() */
			if (result == 0)
				reiser4_update_dir(parent);
			if (result != 0)
				/* cleanup failure to add nlink */
				par_dir->rem_entry(parent, dentry, &entry);
		}
		if (result != 0)
			/* cleanup failure to add entry */
			obj_plug->detach(object, parent);
	} else if (result != -ENOMEM)
		warning("nikita-2219", "Failed to initialize dir for %llu: %i",
			get_inode_oid(object), result);

	/*
	 * update stat-data, committing all pending modifications to the inode
	 * fields.
	 */
	reiser4_update_sd(object);
	if (result != 0) {
		DQUOT_FREE_INODE(object);
		object->i_flags |= S_NOQUOTA;
		/* if everything was ok (result == 0), parent stat-data is
		 * already updated above (update_parent_dir()) */
		reiser4_update_sd(parent);
		/* failure to create entry, remove object */
		obj_plug->delete(object);
	}

	/* file has name now, clear immutable flag */
	inode_clr_flag(object, REISER4_IMMUTABLE);

	/* on error, iput() will call ->delete_inode(). We should keep track
	   of the existence of stat-data for this inode and avoid attempt to
	   remove it in reiser4_delete_inode(). This is accomplished through
	   REISER4_NO_SD bit in inode.u.reiser4_i.plugin.flags
	*/
	return result;
}

/* ->is_name_acceptable() method of directory plugin */
/* Audited by: green(2002.06.15) */
reiser4_internal int
is_name_acceptable(const struct inode *inode /* directory to check */ ,
		   const char *name UNUSED_ARG /* name to check */ ,
		   int len /* @name's length */ )
{
	assert("nikita-733", inode != NULL);
	assert("nikita-734", name != NULL);
	assert("nikita-735", len > 0);

	return len <= reiser4_max_filename_len(inode);
}

/* return true, iff @coord points to the valid directory item that is part of
 * @inode directory. */
static int
is_valid_dir_coord(struct inode * inode, coord_t * coord)
{
	return
		item_type_by_coord(coord) == DIR_ENTRY_ITEM_TYPE &&
		inode_file_plugin(inode)->owns_item(inode, coord);
}

/* true if directory is empty (only contains dot and dotdot) */
reiser4_internal int
is_dir_empty(const struct inode *dir)
{
	assert("nikita-1976", dir != NULL);

	/* rely on our method to maintain directory i_size being equal to the
	   number of entries. */
	return dir->i_size <= 2 ? 0 : RETERR(-ENOTEMPTY);
}

/* compare two logical positions within the same directory */
reiser4_internal cmp_t dir_pos_cmp(const dir_pos * p1, const dir_pos * p2)
{
	cmp_t result;

	assert("nikita-2534", p1 != NULL);
	assert("nikita-2535", p2 != NULL);

	result = de_id_cmp(&p1->dir_entry_key, &p2->dir_entry_key);
	if (result == EQUAL_TO) {
		int diff;

		diff = p1->pos - p2->pos;
		result = (diff < 0) ? LESS_THAN : (diff ? GREATER_THAN : EQUAL_TO);
	}
	return result;
}

/* true, if file descriptor @f is created by NFS server by "demand" to serve
 * one file system operation. This means that there may be "detached state"
 * for underlying inode. */
static inline int
file_is_stateless(struct file *f)
{
	return reiser4_get_dentry_fsdata(f->f_dentry)->stateless;
}

#define CID_SHIFT (20)
#define CID_MASK  (0xfffffull)

/* calculate ->fpos from user-supplied cookie. Normally it is dir->f_pos, but
 * in the case of stateless directory operation (readdir-over-nfs), client id
 * was encoded in the high bits of cookie and should me masked off. */
static loff_t
get_dir_fpos(struct file * dir)
{
	if (file_is_stateless(dir))
		return dir->f_pos & CID_MASK;
	else
		return dir->f_pos;
}

/* see comment before readdir_common() for overview of why "adjustment" is
 * necessary. */
static void
adjust_dir_pos(struct file   * dir,
	       readdir_pos   * readdir_spot,
	       const dir_pos * mod_point,
	       int             adj)
{
	dir_pos *pos;

	/*
	 * new directory entry was added (adj == +1) or removed (adj == -1) at
	 * the @mod_point. Directory file descriptor @dir is doing readdir and
	 * is currently positioned at @readdir_spot. Latter has to be updated
	 * to maintain stable readdir.
	 */

	ON_TRACE(TRACE_DIR, "adjust: %s/%i",
		 dir ? (char *)dir->f_dentry->d_name.name : "(anon)", adj);
	ON_TRACE(TRACE_DIR, "\nf_pos: %llu, spot.fpos: %llu entry_no: %llu\n",
		 dir ? dir->f_pos : 0, readdir_spot->fpos,
		 readdir_spot->entry_no);

	reiser4_stat_inc(dir.readdir.adjust_pos);

	/* directory is positioned to the beginning. */
	if (readdir_spot->entry_no == 0)
		return;

	pos = &readdir_spot->position;
	switch (dir_pos_cmp(mod_point, pos)) {
	case LESS_THAN:
		/* @mod_pos is _before_ @readdir_spot, that is, entry was
		 * added/removed on the left (in key order) of current
		 * position. */
		/* logical number of directory entry readdir is "looking" at
		 * changes */
		readdir_spot->entry_no += adj;
		assert("nikita-2577",
		       ergo(dir != NULL, get_dir_fpos(dir) + adj >= 0));
		if (de_id_cmp(&pos->dir_entry_key,
			      &mod_point->dir_entry_key) == EQUAL_TO) {
			assert("nikita-2575", mod_point->pos < pos->pos);
			/*
			 * if entry added/removed has the same key as current
			 * for readdir, update counter of duplicate keys in
			 * @readdir_spot.
			 */
			pos->pos += adj;
		}
		reiser4_stat_inc(dir.readdir.adjust_lt);
		break;
	case GREATER_THAN:
		/* directory is modified after @pos: nothing to do. */
		reiser4_stat_inc(dir.readdir.adjust_gt);
		break;
	case EQUAL_TO:
		/* cannot insert an entry readdir is looking at, because it
		   already exists. */
		assert("nikita-2576", adj < 0);
		/* directory entry to which @pos points to is being
		   removed.

		   NOTE-NIKITA: Right thing to do is to update @pos to point
		   to the next entry. This is complex (we are under spin-lock
		   for one thing). Just rewind it to the beginning. Next
		   readdir will have to scan the beginning of
		   directory. Proper solution is to use semaphore in
		   spin lock's stead and use rewind_right() here.

		   NOTE-NIKITA: now, semaphore is used, so...
		*/
		xmemset(readdir_spot, 0, sizeof *readdir_spot);
		reiser4_stat_inc(dir.readdir.adjust_eq);
	}
}

/* scan all file-descriptors for this directory and adjust their positions
   respectively. */
reiser4_internal void
adjust_dir_file(struct inode *dir, const struct dentry * de, int offset, int adj)
{
	reiser4_file_fsdata *scan;
	dir_pos mod_point;

	assert("nikita-2536", dir != NULL);
	assert("nikita-2538", de  != NULL);
	assert("nikita-2539", adj != 0);

	build_de_id(dir, &de->d_name, &mod_point.dir_entry_key);
	mod_point.pos = offset;

	spin_lock_inode(dir);

	/*
	 * new entry was added/removed in directory @dir. Scan all file
	 * descriptors for @dir that are currently involved into @readdir and
	 * update them.
	 */

	for_all_type_safe_list(readdir, get_readdir_list(dir), scan)
		adjust_dir_pos(scan->back, &scan->dir.readdir, &mod_point, adj);

	spin_unlock_inode(dir);
}

/*
 * traverse tree to start/continue readdir from the readdir position @pos.
 */
static int
dir_go_to(struct file *dir, readdir_pos * pos, tap_t * tap)
{
	reiser4_key key;
	int result;
	struct inode *inode;

	assert("nikita-2554", pos != NULL);

	inode = dir->f_dentry->d_inode;
	result = inode_dir_plugin(inode)->build_readdir_key(dir, &key);
	if (result != 0)
		return result;
	result = object_lookup(inode,
			       &key,
			       tap->coord,
			       tap->lh,
			       tap->mode,
			       FIND_EXACT,
			       LEAF_LEVEL,
			       LEAF_LEVEL,
			       0,
			       &tap->ra_info);
	if (result == CBK_COORD_FOUND)
		result = rewind_right(tap, (int) pos->position.pos);
	else {
		tap->coord->node = NULL;
		done_lh(tap->lh);
		result = RETERR(-EIO);
	}
	return result;
}

/*
 * handling of non-unique keys: calculate at what ordinal position within
 * sequence of directory items with identical keys @pos is.
 */
static int
set_pos(struct inode * inode, readdir_pos * pos, tap_t * tap)
{
	int          result;
	coord_t      coord;
	lock_handle  lh;
	tap_t        scan;
	de_id       *did;
	reiser4_key  de_key;

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&scan, &coord, &lh, ZNODE_READ_LOCK);
	tap_copy(&scan, tap);
	tap_load(&scan);
	pos->position.pos = 0;

	did = &pos->position.dir_entry_key;

	if (is_valid_dir_coord(inode, scan.coord)) {

		build_de_id_by_key(unit_key_by_coord(scan.coord, &de_key), did);

		while (1) {

			result = go_prev_unit(&scan);
			if (result != 0)
				break;

			if (!is_valid_dir_coord(inode, scan.coord)) {
				result = -EINVAL;
				break;
			}

			/* get key of directory entry */
			unit_key_by_coord(scan.coord, &de_key);
			if (de_id_key_cmp(did, &de_key) != EQUAL_TO) {
				/* duplicate-sequence is over */
				break;
			}
			pos->position.pos ++;
		}
	} else
		result = RETERR(-ENOENT);
	tap_relse(&scan);
	tap_done(&scan);
	return result;
}


/*
 * "rewind" directory to @offset, i.e., set @pos and @tap correspondingly.
 */
static int
dir_rewind(struct file *dir, readdir_pos * pos, tap_t * tap)
{
	__u64 destination;
	__s64 shift;
	int result;
	struct inode *inode;
	loff_t dirpos;

	assert("nikita-2553", dir != NULL);
	assert("nikita-2548", pos != NULL);
	assert("nikita-2551", tap->coord != NULL);
	assert("nikita-2552", tap->lh != NULL);

	dirpos = get_dir_fpos(dir);
	shift = dirpos - pos->fpos;
	/* this is logical directory entry within @dir which we are rewinding
	 * to */
	destination = pos->entry_no + shift;

	inode = dir->f_dentry->d_inode;
	if (dirpos < 0)
		return RETERR(-EINVAL);
	else if (destination == 0ll || dirpos == 0) {
		/* rewind to the beginning of directory */
		xmemset(pos, 0, sizeof *pos);
		reiser4_stat_inc(dir.readdir.reset);
		return dir_go_to(dir, pos, tap);
	} else if (destination >= inode->i_size)
		return RETERR(-ENOENT);

	if (shift < 0) {
		/* I am afraid of negative numbers */
		shift = -shift;
		/* rewinding to the left */
		reiser4_stat_inc(dir.readdir.rewind_left);
		if (shift <= (int) pos->position.pos) {
			/* destination is within sequence of entries with
			   duplicate keys. */
			reiser4_stat_inc(dir.readdir.left_non_uniq);
			result = dir_go_to(dir, pos, tap);
		} else {
			shift -= pos->position.pos;
			while (1) {
				/* repetitions: deadlock is possible when
				   going to the left. */
				result = dir_go_to(dir, pos, tap);
				if (result == 0) {
					result = rewind_left(tap, shift);
					if (result == -E_DEADLOCK) {
						tap_done(tap);
						reiser4_stat_inc(dir.readdir.left_restart);
						continue;
					}
				}
				break;
			}
		}
	} else {
		/* rewinding to the right */
		reiser4_stat_inc(dir.readdir.rewind_right);
		result = dir_go_to(dir, pos, tap);
		if (result == 0)
			result = rewind_right(tap, shift);
	}
	if (result == 0) {
		result = set_pos(inode, pos, tap);
		if (result == 0) {
			/* update pos->position.pos */
			pos->entry_no = destination;
			pos->fpos = dirpos;
		}
	}
	return result;
}

/*
 * Function that is called by common_readdir() on each directory entry while
 * doing readdir. ->filldir callback may block, so we had to release long term
 * lock while calling it. To avoid repeating tree traversal, seal is used. If
 * seal is broken, we return -E_REPEAT. Node is unlocked in this case.
 *
 * Whether node is unlocked in case of any other error is undefined. It is
 * guaranteed to be still locked if success (0) is returned.
 *
 * When ->filldir() wants no more, feed_entry() returns 1, and node is
 * unlocked.
 */
static int
feed_entry(struct file *f,
	   readdir_pos * pos, tap_t *tap, filldir_t filldir, void *dirent)
{
	item_plugin *iplug;
	char *name;
	reiser4_key sd_key;
	int result;
	char buf[DE_NAME_BUF_LEN];
	char name_buf[32];
	char *local_name;
	unsigned file_type;
	seal_t seal;
	coord_t *coord;
	reiser4_key entry_key;

	coord = tap->coord;
	iplug = item_plugin_by_coord(coord);

	/* pointer to name within the node */
	name = iplug->s.dir.extract_name(coord, buf);
	assert("nikita-1371", name != NULL);

	/* key of object the entry points to */
	if (iplug->s.dir.extract_key(coord, &sd_key) != 0)
		return RETERR(-EIO);

	/* we must release longterm znode lock before calling filldir to avoid
	   deadlock which may happen if filldir causes page fault. So, copy
	   name to intermediate buffer */
	if (strlen(name) + 1 > sizeof(name_buf)) {
		local_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
		if (local_name == NULL)
			return RETERR(-ENOMEM);
	} else
		local_name = name_buf;

	strcpy(local_name, name);
	file_type = iplug->s.dir.extract_file_type(coord);

	unit_key_by_coord(coord, &entry_key);
	seal_init(&seal, coord, &entry_key);

	longterm_unlock_znode(tap->lh);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "readdir: %s, %llu, %llu, %llu\n",
		 name, pos->fpos, pos->entry_no, get_key_objectid(&sd_key));

	/*
	 * send information about directory entry to the ->filldir() filler
	 * supplied to us by caller (VFS).
	 *
	 * ->filldir is entitled to do weird things. For example, ->filldir
	 * supplied by knfsd re-enters file system. Make sure no locks are
	 * held.
	 */
	assert("nikita-3436", lock_stack_isclean(get_current_lock_stack()));

	result = filldir(dirent, name, (int) strlen(name),
			 /* offset of this entry */
			 f->f_pos,
			 /* inode number of object bounden by this entry */
			 oid_to_uino(get_key_objectid(&sd_key)),
			 file_type);
	if (local_name != name_buf)
		kfree(local_name);
	if (result < 0)
		/* ->filldir() is satisfied. (no space in buffer, IOW) */
		result = 1;
	else
		result = seal_validate(&seal, coord, &entry_key, LEAF_LEVEL,
				       tap->lh, FIND_EXACT,
				       tap->mode, ZNODE_LOCK_HIPRI);
	return result;
}

static void
move_entry(readdir_pos * pos, coord_t * coord)
{
	reiser4_key de_key;
	de_id *did;

	/* update @pos */
	++pos->entry_no;
	did = &pos->position.dir_entry_key;

	/* get key of directory entry */
	unit_key_by_coord(coord, &de_key);

	if (de_id_key_cmp(did, &de_key) == EQUAL_TO)
		/* we are within sequence of directory entries
		   with duplicate keys. */
		++pos->position.pos;
	else {
		pos->position.pos = 0;
		build_de_id_by_key(&de_key, did);
	}
	++pos->fpos;
}

/*
 *     STATELESS READDIR
 *
 * readdir support in reiser4 relies on ability to update readdir_pos embedded
 * into reiser4_file_fsdata on each directory modification (name insertion and
 * removal), see readdir_common() function below. This obviously doesn't work
 * when reiser4 is accessed over NFS, because NFS doesn't keep any state
 * across client READDIR requests for the same directory.
 *
 * To address this we maintain a "pool" of detached reiser4_file_fsdata
 * (d_cursor). Whenever NFS readdir request comes, we detect this, and try to
 * find detached reiser4_file_fsdata corresponding to previous readdir
 * request. In other words, additional state is maintained on the
 * server. (This is somewhat contrary to the design goals of NFS protocol.)
 *
 * To efficiently detect when our ->readdir() method is called by NFS server,
 * dentry is marked as "stateless" in reiser4_decode_fh() (this is checked by
 * file_is_stateless() function).
 *
 * To find out d_cursor in the pool, we encode client id (cid) in the highest
 * bits of NFS readdir cookie: when first readdir request comes to the given
 * directory from the given client, cookie is set to 0. This situation is
 * detected, global cid_counter is incremented, and stored in highest bits of
 * all direntry offsets returned to the client, including last one. As the
 * only valid readdir cookie is one obtained as direntry->offset, we are
 * guaranteed that next readdir request (continuing current one) will have
 * current cid in the highest bits of starting readdir cookie. All d_cursors
 * are hashed into per-super-block hash table by (oid, cid) key.
 *
 * In addition d_cursors are placed into per-super-block radix tree where they
 * are keyed by oid alone. This is necessary to efficiently remove them during
 * rmdir.
 *
 * At last, currently unused d_cursors are linked into special list. This list
 * is used d_cursor_shrink to reclaim d_cursors on memory pressure.
 *
 */

TYPE_SAFE_LIST_DECLARE(d_cursor);
TYPE_SAFE_LIST_DECLARE(a_cursor);

typedef struct {
	__u16 cid;
	__u64 oid;
} d_cursor_key;

struct dir_cursor {
	int                  ref;
	reiser4_file_fsdata *fsdata;
	d_cursor_hash_link   hash;
	d_cursor_list_link   list;
	d_cursor_key         key;
	d_cursor_info       *info;
	a_cursor_list_link   alist;
};

static kmem_cache_t *d_cursor_slab;
static struct shrinker *d_cursor_shrinker;
static unsigned long d_cursor_unused = 0;
static spinlock_t d_lock = SPIN_LOCK_UNLOCKED;
static a_cursor_list_head cursor_cache = TYPE_SAFE_LIST_HEAD_INIT(cursor_cache);

#define D_CURSOR_TABLE_SIZE (256)

static inline unsigned long
d_cursor_hash(d_cursor_hash_table *table, const d_cursor_key * key)
{
	assert("nikita-3555", IS_POW(D_CURSOR_TABLE_SIZE));
	return (key->oid + key->cid) & (D_CURSOR_TABLE_SIZE - 1);
}

static inline int
d_cursor_eq(const d_cursor_key * k1, const d_cursor_key * k2)
{
	return k1->cid == k2->cid && k1->oid == k2->oid;
}

#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TYPE_SAFE_HASH_DEFINE(d_cursor,
		      dir_cursor,
		      d_cursor_key,
		      key,
		      hash,
		      d_cursor_hash,
		      d_cursor_eq);
#undef KFREE
#undef KMALLOC

TYPE_SAFE_LIST_DEFINE(d_cursor, dir_cursor, list);
TYPE_SAFE_LIST_DEFINE(a_cursor, dir_cursor, alist);

static void kill_cursor(dir_cursor *cursor);

/*
 * shrink d_cursors cache. Scan LRU list of unused cursors, freeing requested
 * number. Return number of still freeable cursors.
 */
int d_cursor_shrink(int nr, unsigned int gfp_mask)
{
	if (nr != 0) {
		dir_cursor *scan;
		int killed;

		killed = 0;
		spin_lock(&d_lock);
		while (!a_cursor_list_empty(&cursor_cache)) {
			scan = a_cursor_list_front(&cursor_cache);
			assert("nikita-3567", scan->ref == 0);
			kill_cursor(scan);
			++ killed;
			-- nr;
			if (nr == 0)
				break;
		}
		spin_unlock(&d_lock);
	}
	return d_cursor_unused;
}

/*
 * perform global initializations for the d_cursor sub-system.
 */
reiser4_internal int
d_cursor_init(void)
{
	d_cursor_slab = kmem_cache_create("d_cursor", sizeof (dir_cursor), 0,
					  SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (d_cursor_slab == NULL)
		return RETERR(-ENOMEM);
	else {
		/* actually, d_cursors are "priceless", because there is no
		 * way to recover information stored in them. On the other
		 * hand, we don't want to consume all kernel memory by
		 * them. As a compromise, just assign higher "seeks" value to
		 * d_cursor cache, so that it will be shrunk only if system is
		 * really tight on memory. */
		d_cursor_shrinker = set_shrinker(DEFAULT_SEEKS << 3,
						 d_cursor_shrink);
		if (d_cursor_shrinker == NULL)
			return RETERR(-ENOMEM);
		else
			return 0;
	}
}

/*
 * Dual to d_cursor_init(): release global d_cursor resources.
 */
reiser4_internal void
d_cursor_done(void)
{
	if (d_cursor_shrinker != NULL) {
		remove_shrinker(d_cursor_shrinker);
		d_cursor_shrinker = NULL;
	}
	if (d_cursor_slab != NULL) {
		kmem_cache_destroy(d_cursor_slab);
		d_cursor_slab = NULL;
	}
}

/*
 * initialize per-super-block d_cursor resources
 */
reiser4_internal int
d_cursor_init_at(struct super_block *s)
{
	d_cursor_info *p;

	p = &get_super_private(s)->d_info;

	INIT_RADIX_TREE(&p->tree, GFP_KERNEL);
	return d_cursor_hash_init(&p->table, D_CURSOR_TABLE_SIZE, NULL);
}

/*
 * Dual to d_cursor_init_at: release per-super-block d_cursor resources
 */
reiser4_internal void
d_cursor_done_at(struct super_block *s)
{
	d_cursor_hash_done(&get_super_private(s)->d_info.table);
}

/*
 * return d_cursor data for the file system @inode is in.
 */
static inline d_cursor_info * d_info(struct inode *inode)
{
	return &get_super_private(inode->i_sb)->d_info;
}

/*
 * lookup d_cursor in the per-super-block radix tree.
 */
static inline dir_cursor *lookup(d_cursor_info *info, unsigned long index)
{
	return (dir_cursor *)radix_tree_lookup(&info->tree, index);
}

/*
 * attach @cursor to the radix tree. There may be multiple cursors for the
 * same oid, they are chained into circular list.
 */
static void bind_cursor(dir_cursor *cursor, unsigned long index)
{
	dir_cursor *head;

	head = lookup(cursor->info, index);
	if (head == NULL) {
		/* this is the first cursor for this index */
		d_cursor_list_clean(cursor);
		radix_tree_insert(&cursor->info->tree, index, cursor);
	} else {
		/* some cursor already exists. Chain ours */
		d_cursor_list_insert_after(head, cursor);
	}
}

/*
 * remove @cursor from indices and free it
 */
static void
kill_cursor(dir_cursor *cursor)
{
	unsigned long index;

	assert("nikita-3566", cursor->ref == 0);
	assert("nikita-3572", cursor->fsdata != NULL);

	index = (unsigned long)cursor->key.oid;
	readdir_list_remove_clean(cursor->fsdata);
	reiser4_free_fsdata(cursor->fsdata);
	cursor->fsdata = NULL;

	if (d_cursor_list_is_clean(cursor))
		/* this is last cursor for a file. Kill radix-tree entry */
		radix_tree_delete(&cursor->info->tree, index);
	else {
		void **slot;

		/*
		 * there are other cursors for the same oid.
		 */

		/*
		 * if radix tree point to the cursor being removed, re-target
		 * radix tree slot to the next cursor in the (non-empty as was
		 * checked above) element of the circular list of all cursors
		 * for this oid.
		 */
		slot = radix_tree_lookup_slot(&cursor->info->tree, index);
		assert("nikita-3571", *slot != NULL);
		if (*slot == cursor)
			*slot = d_cursor_list_next(cursor);
		/* remove cursor from circular list */
		d_cursor_list_remove_clean(cursor);
	}
	/* remove cursor from the list of unused cursors */
	a_cursor_list_remove_clean(cursor);
	/* remove cursor from the hash table */
	d_cursor_hash_remove(&cursor->info->table, cursor);
	/* and free it */
	kmem_cache_free(d_cursor_slab, cursor);
	-- d_cursor_unused;
}

/* possible actions that can be performed on all cursors for the given file */
enum cursor_action {
	/* load all detached state: this is called when stat-data is loaded
	 * from the disk to recover information about all pending readdirs */
	CURSOR_LOAD,
	/* detach all state from inode, leaving it in the cache. This is
	 * called when inode is removed form the memory by memory pressure */
	CURSOR_DISPOSE,
	/* detach cursors from the inode, and free them. This is called when
	 * inode is destroyed. */
	CURSOR_KILL
};

static void
process_cursors(struct inode *inode, enum cursor_action act)
{
	oid_t oid;
	dir_cursor *start;
	readdir_list_head *head;
	reiser4_context ctx;
	d_cursor_info *info;

	/* this can be called by
	 *
	 * kswapd->...->prune_icache->..reiser4_destroy_inode
	 *
	 * without reiser4_context
	 */
	init_context(&ctx, inode->i_sb);

	assert("nikita-3558", inode != NULL);

	info = d_info(inode);
	oid = get_inode_oid(inode);
	spin_lock_inode(inode);
	head = get_readdir_list(inode);
	spin_lock(&d_lock);
	/* find any cursor for this oid: reference to it is hanging of radix
	 * tree */
	start = lookup(info, (unsigned long)oid);
	if (start != NULL) {
		dir_cursor *scan;
		reiser4_file_fsdata *fsdata;

		/* process circular list of cursors for this oid */
		scan = start;
		do {
			dir_cursor *next;

			next = d_cursor_list_next(scan);
			fsdata = scan->fsdata;
			assert("nikita-3557", fsdata != NULL);
			if (scan->key.oid == oid) {
				switch (act) {
				case CURSOR_DISPOSE:
					readdir_list_remove_clean(fsdata);
					break;
				case CURSOR_LOAD:
					readdir_list_push_front(head, fsdata);
					break;
				case CURSOR_KILL:
					kill_cursor(scan);
					break;
				}
			}
			if (scan == next)
				/* last cursor was just killed */
				break;
			scan = next;
		} while (scan != start);
	}
	spin_unlock(&d_lock);
	/* check that we killed 'em all */
	assert("nikita-3568", ergo(act == CURSOR_KILL,
				   readdir_list_empty(get_readdir_list(inode))));
	assert("nikita-3569", ergo(act == CURSOR_KILL,
				   lookup(info, oid) == NULL));
	spin_unlock_inode(inode);
	reiser4_exit_context(&ctx);
}

/* detach all cursors from inode. This is called when inode is removed from
 * the memory by memory pressure */
reiser4_internal void dispose_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_DISPOSE);
}

/* attach all detached cursors to the inode. This is done when inode is loaded
 * into memory */
reiser4_internal void load_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_LOAD);
}

/* free all cursors for this inode. This is called when inode is destroyed. */
reiser4_internal void kill_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_KILL);
}

/* global counter used to generate "client ids". These ids are encoded into
 * high bits of fpos. */
static __u32 cid_counter = 0;

/*
 * detach fsdata (if detachable) from file descriptor, and put cursor on the
 * "unused" list. Called when file descriptor is not longer in active use.
 */
static void
clean_fsdata(struct file *f)
{
	dir_cursor   *cursor;
	reiser4_file_fsdata *fsdata;

	assert("nikita-3570", file_is_stateless(f));

	fsdata = (reiser4_file_fsdata *)f->private_data;
	if (fsdata != NULL) {
		cursor = fsdata->cursor;
		if (cursor != NULL) {
			spin_lock(&d_lock);
			-- cursor->ref;
			if (cursor->ref == 0) {
				a_cursor_list_push_back(&cursor_cache, cursor);
				++ d_cursor_unused;
			}
			spin_unlock(&d_lock);
			f->private_data = NULL;
		}
	}
}

/* add detachable readdir state to the @f */
static int
insert_cursor(dir_cursor *cursor, struct file *f, struct inode *inode)
{
	int                  result;
	reiser4_file_fsdata *fsdata;

	xmemset(cursor, 0, sizeof *cursor);

	/* this is either first call to readdir, or rewind. Anyway, create new
	 * cursor. */
	fsdata = create_fsdata(NULL, GFP_KERNEL);
	if (fsdata != NULL) {
		result = radix_tree_preload(GFP_KERNEL);
		if (result == 0) {
			d_cursor_info *info;
			oid_t oid;

			info = d_info(inode);
			oid  = get_inode_oid(inode);
			/* cid occupies higher 12 bits of f->f_pos. Don't
			 * allow it to become negative: this confuses
			 * nfsd_readdir() */
			cursor->key.cid = (++ cid_counter) & 0x7ff;
			cursor->key.oid = oid;
			cursor->fsdata  = fsdata;
			cursor->info    = info;
			cursor->ref     = 1;
			spin_lock_inode(inode);
			/* install cursor as @f's private_data, discarding old
			 * one if necessary */
			clean_fsdata(f);
			reiser4_free_file_fsdata(f);
			f->private_data = fsdata;
			fsdata->cursor = cursor;
			spin_unlock_inode(inode);
			spin_lock(&d_lock);
			/* insert cursor into hash table */
			d_cursor_hash_insert(&info->table, cursor);
			/* and chain it into radix-tree */
			bind_cursor(cursor, (unsigned long)oid);
			spin_unlock(&d_lock);
			radix_tree_preload_end();
			f->f_pos = ((__u64)cursor->key.cid) << CID_SHIFT;
		}
	} else
		result = RETERR(-ENOMEM);
	return result;
}

/* find or create cursor for readdir-over-nfs */
static int
try_to_attach_fsdata(struct file *f, struct inode *inode)
{
	loff_t pos;
	int    result;
	dir_cursor *cursor;

	/*
	 * we are serialized by inode->i_sem
	 */

	if (!file_is_stateless(f))
		return 0;

	pos = f->f_pos;
	result = 0;
	if (pos == 0) {
		/*
		 * first call to readdir (or rewind to the beginning of
		 * directory)
		 */
		cursor = kmem_cache_alloc(d_cursor_slab, GFP_KERNEL);
		if (cursor != NULL)
			result = insert_cursor(cursor, f, inode);
		else
			result = RETERR(-ENOMEM);
	} else {
		/* try to find existing cursor */
		d_cursor_key key;

		key.cid = pos >> CID_SHIFT;
		key.oid = get_inode_oid(inode);
		spin_lock(&d_lock);
		cursor = d_cursor_hash_find(&d_info(inode)->table, &key);
		if (cursor != NULL) {
			/* cursor was found */
			if (cursor->ref == 0) {
				/* move it from unused list */
				a_cursor_list_remove_clean(cursor);
				-- d_cursor_unused;
			}
			++ cursor->ref;
		}
		spin_unlock(&d_lock);
		if (cursor != NULL) {
			spin_lock_inode(inode);
			assert("nikita-3556", cursor->fsdata->back == NULL);
			clean_fsdata(f);
			reiser4_free_file_fsdata(f);
			f->private_data = cursor->fsdata;
			spin_unlock_inode(inode);
		}
	}
	return result;
}

/* detach fsdata, if necessary */
static void
detach_fsdata(struct file *f)
{
	struct inode *inode;

	if (!file_is_stateless(f))
		return;

	inode = f->f_dentry->d_inode;
	spin_lock_inode(inode);
	clean_fsdata(f);
	spin_unlock_inode(inode);
}

/*
 * prepare for readdir.
 */
static int
dir_readdir_init(struct file *f, tap_t * tap, readdir_pos ** pos)
{
	struct inode *inode;
	reiser4_file_fsdata *fsdata;
	int result;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	/* try to find detached readdir state */
	result = try_to_attach_fsdata(f, inode);
	if (result != 0)
		return result;

	fsdata = reiser4_get_file_fsdata(f);
	assert("nikita-2571", fsdata != NULL);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	/* add file descriptor to the readdir list hanging of directory
	 * inode. This list is used to scan "readdirs-in-progress" while
	 * inserting or removing names in the directory. */
	spin_lock_inode(inode);
	if (readdir_list_is_clean(fsdata))
		readdir_list_push_front(get_readdir_list(inode), fsdata);
	*pos = &fsdata->dir.readdir;
	spin_unlock_inode(inode);

	ON_TRACE(TRACE_DIR, " fpos: %llu entry_no: %llu\n",
		 (*pos)->entry_no, (*pos)->fpos);

	/* move @tap to the current position */
	return dir_rewind(f, *pos, tap);
}

/*
 * ->readdir method of directory plugin
 *
 * readdir problems:
 *
 *     Traditional UNIX API for scanning through directory
 *     (readdir/seekdir/telldir/opendir/closedir/rewindir/getdents) is based
 *     on the assumption that directory is structured very much like regular
 *     file, in particular, it is implied that each name within given
 *     directory (directory entry) can be uniquely identified by scalar offset
 *     and that such offset is stable across the life-time of the name is
 *     identifies.
 *
 *     This is manifestly not so for reiser4. In reiser4 the only stable
 *     unique identifies for the directory entry is its key that doesn't fit
 *     into seekdir/telldir API.
 *
 * solution:
 *
 *     Within each file descriptor participating in readdir-ing of directory
 *     plugin/dir/dir.h:readdir_pos is maintained. This structure keeps track
 *     of the "current" directory entry that file descriptor looks at. It
 *     contains a key of directory entry (plus some additional info to deal
 *     with non-unique keys that we wouldn't dwell onto here) and a logical
 *     position of this directory entry starting from the beginning of the
 *     directory, that is ordinal number of this entry in the readdir order.
 *
 *     Obviously this logical position is not stable in the face of directory
 *     modifications. To work around this, on each addition or removal of
 *     directory entry all file descriptors for directory inode are scanned
 *     and their readdir_pos are updated accordingly (adjust_dir_pos()).
 *
 */
static int
readdir_common(struct file *f /* directory file being read */ ,
	       void *dirent /* opaque data passed to us by VFS */ ,
	       filldir_t filld	/* filler function passed to us
				   * by VFS */ )
{
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	tap_t tap;
	readdir_pos *pos;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	reiser4_stat_inc(dir.readdir.calls);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

	reiser4_readdir_readahead_init(inode, &tap);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
		 "readdir: inode: %llu offset: %#llx\n",
		 get_inode_oid(inode), f->f_pos);

 repeat:
	result = dir_readdir_init(f, &tap, &pos);
	if (result == 0) {
		result = tap_load(&tap);
		/* scan entries one by one feeding them to @filld */
		while (result == 0) {
			coord_t *coord;

			coord = tap.coord;
			assert("nikita-2572", coord_is_existing_unit(coord));
			assert("nikita-3227", is_valid_dir_coord(inode, coord));

			result = feed_entry(f, pos, &tap, filld, dirent);
			ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
				 "readdir: entry: offset: %#llx\n", f->f_pos);
			if (result > 0) {
				break;
			} else if (result == 0) {
				++ f->f_pos;
				result = go_next_unit(&tap);
				if (result == -E_NO_NEIGHBOR ||
				    result == -ENOENT) {
					result = 0;
					break;
				} else if (result == 0) {
					if (is_valid_dir_coord(inode, coord))
						move_entry(pos, coord);
					else
						break;
				}
			} else if (result == -E_REPEAT) {
				/* feed_entry() had to restart. */
				++ f->f_pos;
				tap_relse(&tap);
				goto repeat;
			} else
				warning("vs-1617",
					"readdir_common: unexpected error %d",
					result);
		}
		tap_relse(&tap);

		if (result >= 0)
			f->f_version = inode->i_version;
	} else if (result == -E_NO_NEIGHBOR || result == -ENOENT)
		result = 0;
	tap_done(&tap);
	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
		 "readdir_exit: offset: %#llx\n", f->f_pos);
	detach_fsdata(f);
	return (result <= 0) ? result : 0;
}

/*
 * seek method for directory. See comment before readdir_common() for
 * explanation.
 */
loff_t
seek_dir(struct file *file, loff_t off, int origin)
{
	loff_t result;
	struct inode *inode;

	inode = file->f_dentry->d_inode;
	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "seek_dir: %s: %lli -> %lli/%i\n",
		 file->f_dentry->d_name.name, file->f_pos, off, origin);
	down(&inode->i_sem);

	/* update ->f_pos */
	result = default_llseek(file, off, origin);
	if (result >= 0) {
		int ff;
		coord_t coord;
		lock_handle lh;
		tap_t tap;
		readdir_pos *pos;

		coord_init_zero(&coord);
		init_lh(&lh);
		tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

		ff = dir_readdir_init(file, &tap, &pos);
		detach_fsdata(file);
		if (ff != 0)
			result = (loff_t) ff;
		tap_done(&tap);
	}
	detach_fsdata(file);
	up(&inode->i_sem);
	return result;
}

/* ->attach method of directory plugin */
static int
attach_common(struct inode *child UNUSED_ARG, struct inode *parent UNUSED_ARG)
{
	assert("nikita-2647", child != NULL);
	assert("nikita-2648", parent != NULL);

	return 0;
}

/* ->estimate.add_entry method of directory plugin
   estimation of adding entry which supposes that entry is inserting a unit into item
*/
static reiser4_block_nr
estimate_add_entry_common(struct inode *inode)
{
	return estimate_one_insert_into_item(tree_by_inode(inode));
}

/* ->estimate.rem_entry method of directory plugin */
static reiser4_block_nr
estimate_rem_entry_common(struct inode *inode)
{
	return estimate_one_item_removal(tree_by_inode(inode));
}

/* placeholder for VFS methods not-applicable to the object */
static ssize_t
noperm(void)
{
	return RETERR(-EPERM);
}

#define dir_eperm ((void *)noperm)

static int
_noop(void)
{
	return 0;
}

#define enoop ((void *)_noop)

static int
change_dir(struct inode * inode, reiser4_plugin * plugin)
{
	/* cannot change dir plugin of already existing object */
	return RETERR(-EINVAL);
}

static reiser4_plugin_ops dir_plugin_ops = {
	.init     = NULL,
	.load     = NULL,
	.save_len = NULL,
	.save     = NULL,
	.change   = change_dir
};

/*
 * definition of directory plugins
 */

dir_plugin dir_plugins[LAST_DIR_ID] = {
	/* standard hashed directory plugin */
	[HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = HASHED_DIR_PLUGIN_ID,
			.pops = &dir_plugin_ops,
			.label = "dir",
			.desc = "hashed directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.get_parent = get_parent_hashed,
		.lookup = lookup_hashed,
		.unlink = unlink_common,
		.link = link_common,
		.is_name_acceptable = is_name_acceptable,
		.build_entry_key = build_entry_key_common,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_hashed,
		.rem_entry = rem_entry_hashed,
		.create_child = create_child_common,
		.rename = rename_hashed,
		.readdir = readdir_common,
		.init = init_hashed,
		.done = done_hashed,
		.attach = attach_common,
		.detach = detach_hashed,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink    = estimate_unlink_hashed
		}
	},
	/* hashed directory for which seekdir/telldir are guaranteed to
	 * work. Brain-damage. */
	[SEEKABLE_HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = SEEKABLE_HASHED_DIR_PLUGIN_ID,
			.pops = &dir_plugin_ops,
			.label = "dir32",
			.desc = "directory hashed with 31 bit hash",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.get_parent = get_parent_hashed,
		.lookup = lookup_hashed,
		.unlink = unlink_common,
		.link = link_common,
		.is_name_acceptable = is_name_acceptable,
		.build_entry_key = build_entry_key_stable_entry,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_hashed,
		.rem_entry = rem_entry_hashed,
		.create_child = create_child_common,
		.rename = rename_hashed,
		.readdir = readdir_common,
		.init = init_hashed,
		.done = done_hashed,
		.attach = attach_common,
		.detach = detach_hashed,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink    = estimate_unlink_hashed
		}
	},
	/* pseudo directory. */
	[PSEUDO_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = PSEUDO_DIR_PLUGIN_ID,
			.pops = &dir_plugin_ops,
			.label = "pseudo",
			.desc = "pseudo directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.get_parent = get_parent_pseudo,
		.lookup = lookup_pseudo,
		.unlink = dir_eperm,
		.link = dir_eperm,
		.is_name_acceptable = NULL,
		.build_entry_key = NULL,
		.build_readdir_key = NULL,
		.add_entry = dir_eperm,
		.rem_entry = dir_eperm,
		.create_child = NULL,
		.rename = dir_eperm,
		.readdir = readdir_pseudo,
		.init = enoop,
		.done = enoop,
		.attach = enoop,
		.detach = enoop,
		.estimate = {
			.add_entry = NULL,
			.rem_entry = NULL,
			.unlink    = NULL
		}
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
