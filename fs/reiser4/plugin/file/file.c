/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "../../inode.h"
#include "../../super.h"
#include "../../tree_walk.h"
#include "../../carry.h"
#include "../../page_cache.h"
#include "../../ioctl.h"
#include "../object.h"
#include "../../prof.h"
#include "../../safe_link.h"
#include "funcs.h"

#include <linux/writeback.h>
#include <linux/pagevec.h>

/* this file contains file plugin methods of reiser4 unix files.

 Those files are either built of tail items only (FORMATTING_ID) or of extent
 items only (EXTENT_POINTER_ID) or empty (have no items but stat data) */

static int unpack(struct inode *inode, int forever);

/* get unix file plugin specific portion of inode */
reiser4_internal unix_file_info_t *
unix_file_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.unix_file_info;
}

static int
file_is_built_of_tails(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_TAILS;
}

reiser4_internal int
file_is_built_of_extents(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_EXTENTS;
}

reiser4_internal int
file_is_empty(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_EMPTY;
}

reiser4_internal int
file_state_is_unknown(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_UNKNOWN;
}

reiser4_internal void
set_file_state_extents(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_EXTENTS;
}

reiser4_internal void
set_file_state_tails(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_TAILS;
}

static void
set_file_state_empty(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_EMPTY;
}

static void
set_file_state_unknown(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_UNKNOWN;
}
static int
less_than_ldk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keylt(key, znode_get_ld_key(node)));
}

reiser4_internal int
equal_to_rdk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keyeq(key, znode_get_rd_key(node)));
}

#if REISER4_DEBUG

static int
less_than_rdk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keylt(key, znode_get_rd_key(node)));
}

int
equal_to_ldk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keyeq(key, znode_get_ld_key(node)));
}

/* get key of item next to one @coord is set to */
static reiser4_key *
get_next_item_key(const coord_t *coord, reiser4_key *next_key)
{
	if (coord->item_pos == node_num_items(coord->node) - 1) {
		/* get key of next item if it is in right neighbor */
		UNDER_RW_VOID(dk, znode_get_tree(coord->node), read,
			      *next_key = *znode_get_rd_key(coord->node));
	} else {
		/* get key of next item if it is in the same node */
		coord_t next;

		coord_dup_nocheck(&next, coord);
		next.unit_pos = 0;
		check_me("vs-730", coord_next_item(&next) == 0);
		item_key_by_coord(&next, next_key);
	}
	return next_key;
}

static int
item_of_that_file(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key max_possible;
	item_plugin *iplug;

	iplug = item_plugin_by_coord(coord);
	assert("vs-1011", iplug->b.max_key_inside);
	return keylt(key, iplug->b.max_key_inside(coord, &max_possible));
}

static int
check_coord(const coord_t *coord, const reiser4_key *key)
{
	coord_t twin;

	if (!REISER4_DEBUG)
		return 1;
	node_plugin_by_node(coord->node)->lookup(coord->node, key, FIND_MAX_NOT_MORE_THAN, &twin);
	return coords_equal(coord, &twin);
}

#endif /* REISER4_DEBUG */

void init_uf_coord(uf_coord_t *uf_coord, lock_handle *lh)
{
	coord_init_zero(&uf_coord->base_coord);
        coord_clear_iplug(&uf_coord->base_coord);
	uf_coord->lh = lh;
	init_lh(lh);
	memset(&uf_coord->extension, 0, sizeof(uf_coord->extension));
	uf_coord->valid = 0;
}

static inline void
invalidate_extended_coord(uf_coord_t *uf_coord)
{
        coord_clear_iplug(&uf_coord->base_coord);
	uf_coord->valid = 0;
}

static inline void
validate_extended_coord(uf_coord_t *uf_coord, loff_t offset)
{
	assert("vs-1333", uf_coord->valid == 0);
	assert("vs-1348", item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension);

	/* FIXME: */
	item_body_by_coord(&uf_coord->base_coord);
	item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension(uf_coord, offset);
}

reiser4_internal write_mode_t
how_to_write(uf_coord_t *uf_coord, const reiser4_key *key)
{
	write_mode_t result;
	coord_t *coord;
	ON_DEBUG(reiser4_key check);

	coord = &uf_coord->base_coord;

	assert("vs-1252", znode_is_wlocked(coord->node));
	assert("vs-1253", znode_is_loaded(coord->node));

	if (uf_coord->valid == 1) {
		assert("vs-1332", check_coord(coord, key));
		return (coord->between == AFTER_UNIT) ? APPEND_ITEM : OVERWRITE_ITEM;
	}

	if (less_than_ldk(coord->node, key)) {
		assert("vs-1014", get_key_offset(key) == 0);

		coord_init_before_first_item(coord, coord->node);
		uf_coord->valid = 1;
		result = FIRST_ITEM;
		goto ok;
	}

	assert("vs-1335", less_than_rdk(coord->node, key));

	if (node_is_empty(coord->node)) {
		assert("vs-879", znode_get_level(coord->node) == LEAF_LEVEL);
		assert("vs-880", get_key_offset(key) == 0);
		/*
		 * Situation that check below tried to handle is follows: some
		 * other thread writes to (other) file and has to insert empty
		 * leaf between two adjacent extents. Generally, we are not
		 * supposed to muck with this node. But it is possible that
		 * said other thread fails due to some error (out of disk
		 * space, for example) and leaves empty leaf
		 * lingering. Nothing prevents us from reusing it.
		 */
		assert("vs-1000", UNDER_RW(dk, current_tree, read,
					   keylt(key, znode_get_rd_key(coord->node))));
		assert("vs-1002", coord->between == EMPTY_NODE);
		result = FIRST_ITEM;
		uf_coord->valid = 1;
		goto ok;
	}

	assert("vs-1336", coord->item_pos < node_num_items(coord->node));
	assert("vs-1007", ergo(coord->between == AFTER_UNIT || coord->between == AT_UNIT, keyle(item_key_by_coord(coord, &check), key)));
	assert("vs-1008", ergo(coord->between == AFTER_UNIT || coord->between == AT_UNIT, keylt(key, get_next_item_key(coord, &check))));

	switch(coord->between) {
	case AFTER_ITEM:
		uf_coord->valid = 1;
		result = FIRST_ITEM;
		break;
	case AFTER_UNIT:
		assert("vs-1323", (item_is_tail(coord) || item_is_extent(coord)) && item_of_that_file(coord, key));
		assert("vs-1208", keyeq(item_plugin_by_coord(coord)->s.file.append_key(coord, &check), key));
		result = APPEND_ITEM;
		validate_extended_coord(uf_coord, get_key_offset(key));
		break;
	case AT_UNIT:
		/* FIXME: it would be nice to check that coord matches to key */
		assert("vs-1324", (item_is_tail(coord) || item_is_extent(coord)) && item_of_that_file(coord, key));
		validate_extended_coord(uf_coord, get_key_offset(key));
		result = OVERWRITE_ITEM;
		break;
	default:
		assert("vs-1337", 0);
		result = OVERWRITE_ITEM;
		break;
	}

ok:
	assert("vs-1349", uf_coord->valid == 1);
	assert("vs-1332", check_coord(coord, key));
	return result;
}

/* obtain lock on right neighbor and drop lock on current node */
reiser4_internal int
goto_right_neighbor(coord_t * coord, lock_handle * lh)
{
	int result;
	lock_handle lh_right;

	assert("vs-1100", znode_is_locked(coord->node));

	init_lh(&lh_right);
	result = reiser4_get_right_neighbor(
		&lh_right, coord->node,
		znode_is_wlocked(coord->node) ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK,
		GN_CAN_USE_UPPER_LEVELS);
	if (result) {
		done_lh(&lh_right);
		return result;
	}

	done_lh(lh);

	coord_init_first_unit_nocheck(coord, lh_right.node);
	move_lh(lh, &lh_right);

	return 0;

}

/* this is to be used after find_file_item and in find_file_item_nohint to determine real state of file */
static void
set_file_state(struct inode *inode, int cbk_result, tree_level level)
{
	assert("vs-1649", inode != NULL);

	if (cbk_errored(cbk_result))
		/* error happened in find_file_item */
		return;

	assert("vs-1164", level == LEAF_LEVEL || level == TWIG_LEVEL);

	if (inode_get_flag(inode, REISER4_PART_CONV)) {
		set_file_state_unknown(inode);
		return;
	}

	if (file_state_is_unknown(inode)) {
		if (cbk_result == CBK_COORD_NOTFOUND)
			set_file_state_empty(inode);
		else if (level == LEAF_LEVEL)
			set_file_state_tails(inode);
		else
			set_file_state_extents(inode);
	} else {
		/* file state is known, check that it is set correctly */
		assert("vs-1161", ergo(cbk_result == CBK_COORD_NOTFOUND,
				       file_is_empty(inode)));
		assert("vs-1162", ergo(level == LEAF_LEVEL && cbk_result == CBK_COORD_FOUND,
				       file_is_built_of_tails(inode)));
		assert("vs-1165", ergo(level == TWIG_LEVEL && cbk_result == CBK_COORD_FOUND,
				       file_is_built_of_extents(inode)));
	}
}

reiser4_internal int
find_file_item(hint_t *hint, /* coord, lock handle and seal are here */
	       const reiser4_key *key, /* key of position in a file of next read/write */
	       znode_lock_mode lock_mode, /* which lock (read/write) to put on returned node */
	       ra_info_t *ra_info,
	       struct inode *inode)
{
	int result;
	coord_t *coord;
	lock_handle *lh;
	__u32 cbk_flags;

	assert("nikita-3030", schedulable());

	/* collect statistics on the number of calls to this function */
	reiser4_stat_inc(file.find_file_item);

	coord = &hint->coord.base_coord;
	lh = hint->coord.lh;
	init_lh(lh);
	if (hint) {
		result = hint_validate(hint, key, 1/*check key*/, lock_mode);
		if (!result) {
			if (coord->between == AFTER_UNIT && equal_to_rdk(coord->node, key)) {
				result = goto_right_neighbor(coord, lh);
				if (result == -E_NO_NEIGHBOR)
					return RETERR(-EIO);
				if (result)
					return result;
				assert("vs-1152", equal_to_ldk(coord->node, key));
				/* we moved to different node. Invalidate coord extension, zload is necessary to init it
				   again */
				hint->coord.valid = 0;
				reiser4_stat_inc(file.find_file_item_via_right_neighbor);
			} else {
				reiser4_stat_inc(file.find_file_item_via_seal);
			}

			set_file_state(inode, CBK_COORD_FOUND, znode_get_level(coord->node));
			return CBK_COORD_FOUND;
		}
	}

	/* collect statistics on the number of calls to this function which did not get optimized */
	reiser4_stat_inc(file.find_file_item_via_cbk);

	coord_init_zero(coord);
	cbk_flags = (lock_mode == ZNODE_READ_LOCK) ? CBK_UNIQUE : (CBK_UNIQUE | CBK_FOR_INSERT);
	if (inode != NULL) {
		result = object_lookup(inode,
				       key,
				       coord,
				       lh,
				       lock_mode,
				       FIND_MAX_NOT_MORE_THAN,
				       TWIG_LEVEL,
				       LEAF_LEVEL,
				       cbk_flags,
				       ra_info);
	} else {
		result = coord_by_key(current_tree,
				      key,
				      coord,
				      lh,
				      lock_mode,
				      FIND_MAX_NOT_MORE_THAN,
				      TWIG_LEVEL,
				      LEAF_LEVEL,
				      cbk_flags,
				      ra_info);
	}

	set_file_state(inode, result, znode_get_level(coord->node));

	/* FIXME: we might already have coord extension initialized */
	hint->coord.valid = 0;
	return result;
}

reiser4_internal int
find_file_item_nohint(coord_t *coord, lock_handle *lh, const reiser4_key *key,
		      znode_lock_mode lock_mode, struct inode *inode)
{
	int result;

	result = object_lookup(inode, key, coord, lh, lock_mode,
			       FIND_MAX_NOT_MORE_THAN,
			       TWIG_LEVEL, LEAF_LEVEL,
			       (lock_mode == ZNODE_READ_LOCK) ? CBK_UNIQUE : (CBK_UNIQUE | CBK_FOR_INSERT),
			       NULL /* ra_info */);
	set_file_state(inode, result, znode_get_level(coord->node));
	return result;
}

/* plugin->u.file.write_flowom = NULL
   plugin->u.file.read_flow = NULL */

reiser4_internal void
hint_init_zero(hint_t *hint, lock_handle *lh)
{
	xmemset(hint, 0, sizeof (*hint));
	hint->coord.lh = lh;
}

/* find position of last byte of last item of the file plus 1. This is used by truncate and mmap to find real file
   size */
static int
find_file_size(struct inode *inode, loff_t *file_size)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	assert("vs-1247", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	key_by_inode_unix_file(inode, get_key_offset(max_key()), &key);

	init_lh(&lh);
	result = find_file_item_nohint(&coord, &lh, &key, ZNODE_READ_LOCK, inode);
	if (cbk_errored(result)) {
		/* error happened */
		done_lh(&lh);
		return result;
	}

	if (result == CBK_COORD_NOTFOUND) {
		/* empty file */
		done_lh(&lh);
		*file_size = 0;
		return 0;
	}

	/* there are items of this file (at least one) */
	/*coord_clear_iplug(&coord);*/
	result = zload(coord.node);
	if (unlikely(result)) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(&coord);

	assert("vs-853", iplug->s.file.append_key);
	iplug->s.file.append_key(&coord, &key);

	*file_size = get_key_offset(&key);

	zrelse(coord.node);
	done_lh(&lh);

	return 0;
}

static int
find_file_state(unix_file_info_t *uf_info)
{
	int result;

	assert("vs-1628", ea_obtained(uf_info));

	result = 0;
	if (uf_info->container == UF_CONTAINER_UNKNOWN) {
		loff_t file_size;

		result = find_file_size(unix_file_info_to_inode(uf_info), &file_size);
	}
	assert("vs-1074", ergo(result == 0, uf_info->container != UF_CONTAINER_UNKNOWN));
	return result;
}

/* estimate and reserve space needed to truncate page which gets partially truncated: one block for page itself, stat
   data update (estimate_one_insert_into_item) and one item insertion (estimate_one_insert_into_item) which may happen
   if page corresponds to hole extent and unallocated one will have to be created */
static int reserve_partial_page(reiser4_tree *tree)
{
	grab_space_enable();
	return reiser4_grab_reserved(reiser4_get_current_sb(),
				     1 +
				     2 * estimate_one_insert_into_item(tree),
				     BA_CAN_COMMIT);
}

/* estimate and reserve space needed to cut one item and update one stat data */
reiser4_internal int reserve_cut_iteration(reiser4_tree *tree)
{
	__u64 estimate = estimate_one_item_removal(tree)
		+ estimate_one_insert_into_item(tree);

	assert("nikita-3172", lock_stack_isclean(get_current_lock_stack()));

	grab_space_enable();
	/* We need to double our estimate now that we can delete more than one
	   node. */
	return reiser4_grab_reserved(reiser4_get_current_sb(), estimate*2,
				     BA_CAN_COMMIT);
}

/* cut file items one by one starting from the last one until new file size (inode->i_size) is reached. Reserve space
   and update file stat data on every single cut from the tree */
reiser4_internal int
cut_file_items(struct inode *inode, loff_t new_size, int update_sd, loff_t cur_size, int mode)
{
	reiser4_key from_key, to_key;
	reiser4_key smallest_removed;
	int result;

	assert("vs-1248", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	key_by_inode_unix_file(inode, new_size, &from_key);
	to_key = from_key;
	set_key_offset(&to_key, cur_size - 1/*get_key_offset(max_key())*/);
	/* this loop normally runs just once */
	while (1) {
		result = reserve_cut_iteration(tree_by_inode(inode));
		if (result)
			break;

		result = cut_tree_object(current_tree, &from_key, &to_key,
					 &smallest_removed, inode, mode);
		if (result == -E_REPEAT) {
			/* -E_REPEAT is a signal to interrupt a long file truncation process */
			INODE_SET_FIELD(inode, i_size, get_key_offset(&smallest_removed));
			if (update_sd) {
				inode->i_ctime = inode->i_mtime = CURRENT_TIME;
				result = reiser4_update_sd(inode);
				if (result)
					break;
			}

			all_grabbed2free();
			reiser4_release_reserved(inode->i_sb);

			/* cut_tree_object() was interrupted probably because
			 * current atom requires commit, we have to release
			 * transaction handle to allow atom commit. */
			txn_restart_current();
			continue;
		}
		if (result && !(result == CBK_COORD_NOTFOUND && new_size == 0 && inode->i_size == 0))
			break;

		INODE_SET_FIELD(inode, i_size, new_size);
		if (update_sd) {
			/* Final sd update after the file gets its correct size */
			inode->i_ctime = inode->i_mtime = CURRENT_TIME;
			result = reiser4_update_sd(inode);
		}
		break;
	}

	all_grabbed2free();
	reiser4_release_reserved(inode->i_sb);

	return result;
}

int find_or_create_extent(struct page *page);

/* part of unix_file_truncate: it is called when truncate is used to make file shorter */
static int
shorten_file(struct inode *inode, loff_t new_size)
{
	int result;
	struct page *page;
	int padd_from;
	unsigned long index;
	char *kaddr;

	/* all items of ordinary reiser4 file are grouped together. That is why we can use cut_tree. Plan B files (for
	   instance) can not be truncated that simply */
	result = cut_file_items(inode, new_size, 1/*update_sd*/, get_key_offset(max_key()), 1);
	if (result)
		return result;

	assert("vs-1105", new_size == inode->i_size);
	if (new_size == 0) {
		set_file_state_empty(inode);
		return 0;
	}

	if (file_is_built_of_tails(inode))
		/* No need to worry about zeroing last page after new file end */
		return 0;

	padd_from = inode->i_size & (PAGE_CACHE_SIZE - 1);
	if (!padd_from)
		/* file is truncated to page boundary */
		return 0;

	result = reserve_partial_page(tree_by_inode(inode));
	if (result) {
		reiser4_release_reserved(inode->i_sb);
		return result;
	}

	/* last page is partially truncated - zero its content */
	index = (inode->i_size >> PAGE_CACHE_SHIFT);
	page = read_cache_page(inode->i_mapping, index, readpage_unix_file/*filler*/, 0);
	if (IS_ERR(page)) {
		all_grabbed2free();
		reiser4_release_reserved(inode->i_sb);
		if (likely(PTR_ERR(page) == -EINVAL)) {
			/* looks like file is built of tail items */
			return 0;
		}
		return PTR_ERR(page);
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		all_grabbed2free();
		page_cache_release(page);
		reiser4_release_reserved(inode->i_sb);
		return RETERR(-EIO);
	}

	/* if page correspons to hole extent unit - unallocated one will be created here. This is not necessary */
	result = find_or_create_extent(page);

	/* FIXME: cut_file_items has already updated inode. Probably it would be better to update it here when file is
	   really truncated */
	all_grabbed2free();
	if (result) {
		page_cache_release(page);
		reiser4_release_reserved(inode->i_sb);
		return result;
	}

	lock_page(page);
	assert("vs-1066", PageLocked(page));
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + padd_from, 0, PAGE_CACHE_SIZE - padd_from);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	unlock_page(page);
	page_cache_release(page);
	reiser4_release_reserved(inode->i_sb);
	return 0;
}

static loff_t
write_flow(struct file *, struct inode *, const char *buf, loff_t count, loff_t pos);

/* it is called when truncate is used to make file longer and when write position is set past real end of file. It
   appends file which has size @cur_size with hole of certain size (@hole_size). It returns 0 on success, error code
   otherwise */
static int
append_hole(struct inode *inode, loff_t new_size)
{
	int result;
	loff_t written;
	loff_t hole_size;

	assert("vs-1107", inode->i_size < new_size);

	result = 0;
	hole_size = new_size - inode->i_size;
	written = write_flow(NULL, inode, NULL/*buf*/, hole_size,
			     inode->i_size);
	if (written != hole_size) {
		/* return error because file is not expanded as required */
		if (written > 0)
			result = RETERR(-ENOSPC);
		else
			result = written;
	} else {
		assert("vs-1081", inode->i_size == new_size);
	}
	return result;
}

/* this either cuts or add items of/to the file so that items match new_size. It is used in unix_file_setattr when it is
   used to truncate
VS-FIXME-HANS: explain that
and in unix_file_delete */
static int
truncate_file_body(struct inode *inode, loff_t new_size)
{
	int result;

	if (inode->i_size < new_size)
		result = append_hole(inode, new_size);
	else
		result = shorten_file(inode, new_size);

	return result;
}

/* plugin->u.file.truncate
   all the work is done on reiser4_setattr->unix_file_setattr->truncate_file_body
*/
reiser4_internal int
truncate_unix_file(struct inode *inode, loff_t new_size)
{
	return 0;
}

/* plugin->u.write_sd_by_inode = write_sd_by_inode_common */

/* get access hint (seal, coord, key, level) stored in reiser4 private part of
   struct file if it was stored in a previous access to the file */
reiser4_internal int
load_file_hint(struct file *file, hint_t *hint, lock_handle *lh)
{
	reiser4_file_fsdata *fsdata;

	if (file) {
		fsdata = reiser4_get_file_fsdata(file);
		if (IS_ERR(fsdata))
			return PTR_ERR(fsdata);

		if (seal_is_set(&fsdata->reg.hint.seal)) {
			*hint = fsdata->reg.hint;
			hint->coord.lh = lh;
			/* force re-validation of the coord on the first
			 * iteration of the read/write loop. */
			hint->coord.valid = 0;
			return 0;
		}
		xmemset(&fsdata->reg.hint, 0, sizeof(hint_t));
	}
	hint_init_zero(hint, lh);
	return 0;
}


/* this copies hint for future tree accesses back to reiser4 private part of
   struct file */
reiser4_internal void
save_file_hint(struct file *file, const hint_t *hint)
{
	reiser4_file_fsdata *fsdata;

	if (!file || !seal_is_set(&hint->seal))
		return;

	fsdata = reiser4_get_file_fsdata(file);
	assert("vs-965", !IS_ERR(fsdata));
	fsdata->reg.hint = *hint;
	return;
}

reiser4_internal void
unset_hint(hint_t *hint)
{
	assert("vs-1315", hint);
	seal_done(&hint->seal);
}

/* coord must be set properly. So, that set_hint has nothing to do */
reiser4_internal void
set_hint(hint_t *hint, const reiser4_key *key, znode_lock_mode mode)
{
	ON_DEBUG(coord_t *coord = &hint->coord.base_coord);
	assert("vs-1207", WITH_DATA(coord->node, check_coord(coord, key)));

	seal_init(&hint->seal, &hint->coord.base_coord, key);
	hint->offset = get_key_offset(key);
	hint->level = znode_get_level(hint->coord.base_coord.node);
	hint->mode = mode;
}

reiser4_internal int
hint_is_set(const hint_t *hint)
{
	return seal_is_set(&hint->seal);
}

#if REISER4_DEBUG
static int all_but_offset_key_eq(const reiser4_key *k1, const reiser4_key *k2)
{
	return (get_key_locality(k1) == get_key_locality(k2) &&
		get_key_type(k1) == get_key_type(k2) &&
		get_key_band(k1) == get_key_band(k2) &&
		get_key_ordering(k1) == get_key_ordering(k2) &&
		get_key_objectid(k1) == get_key_objectid(k2));
}
#endif

reiser4_internal int
hint_validate(hint_t *hint, const reiser4_key *key, int check_key, znode_lock_mode lock_mode)
{
	if (!hint || !hint_is_set(hint) || hint->mode != lock_mode)
		/* hint either not set or set by different operation */
		return RETERR(-E_REPEAT);

	assert("vs-1277", all_but_offset_key_eq(key, &hint->seal.key));

	if (check_key && get_key_offset(key) != hint->offset)
		/* hint is set for different key */
		return RETERR(-E_REPEAT);

	return seal_validate(&hint->seal, &hint->coord.base_coord, key,
			     hint->level, hint->coord.lh,
			     FIND_MAX_NOT_MORE_THAN,
			     lock_mode, ZNODE_LOCK_LOPRI);
}

/* look for place at twig level for extent corresponding to page, call extent's writepage method to create
   unallocated extent if it does not exist yet, initialize jnode, capture page */
reiser4_internal int
find_or_create_extent(struct page *page)
{
	int result;
	uf_coord_t uf_coord;
	coord_t *coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin *iplug;
	znode *loaded;
	struct inode *inode;

	reiser4_stat_inc(file.page_ops.writepage_calls);

	assert("vs-1065", page->mapping && page->mapping->host);
	inode = page->mapping->host;

	/* get key of first byte of the page */
	key_by_inode_unix_file(inode, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	init_uf_coord(&uf_coord, &lh);
	coord = &uf_coord.base_coord;

	result = find_file_item_nohint(coord, &lh, &key, ZNODE_WRITE_LOCK, inode);
	if (IS_CBKERR(result)) {
		done_lh(&lh);
		return result;
	}

	/*coord_clear_iplug(coord);*/
	result = zload(coord->node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	loaded = coord->node;

	/* get plugin of extent item */
	iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	result = iplug->s.file.capture(&key, &uf_coord, page, how_to_write(&uf_coord, &key));
	assert("vs-429378", result != -E_REPEAT);
	zrelse(loaded);
	done_lh(&lh);
	return result;
}

#if REISER4_USE_EFLUSH
static int inode_has_eflushed_jnodes(struct inode * inode)
{
	reiser4_tree * tree = &get_super_private(inode->i_sb)->tree;
	int ret;

	RLOCK_TREE(tree);
	ret = (radix_tree_tagged(jnode_tree_by_inode(inode), EFLUSH_TAG_ANONYMOUS) ||
	       radix_tree_tagged(jnode_tree_by_inode(inode), EFLUSH_TAG_CAPTURED));
	RUNLOCK_TREE(tree);
	return ret;
}
# else
#define inode_has_eflushed_jnodes(inode) (0)
#endif

/* Check mapping for existence of not captured dirty pages. This returns !0 if either page tree contains pages tagged
   PAGECACHE_TAG_REISER4_MOVED or if eflushed jnode tree is not empty */
static int
inode_has_anonymous_pages(struct inode *inode)
{
	return (mapping_tagged(inode->i_mapping, PAGECACHE_TAG_REISER4_MOVED) ||
		inode_has_eflushed_jnodes(inode));
}

static int
capture_page_and_create_extent(struct page *page)
{
	int result;
	struct inode *inode;

	assert("vs-1084", page->mapping && page->mapping->host);
	inode = page->mapping->host;
	assert("vs-1139", file_is_built_of_extents(inode));
	/* page belongs to file */
	assert("vs-1393", inode->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT));

	/* page capture may require extent creation (if it does not exist yet) and stat data's update (number of blocks
	   changes on extent creation) */
	grab_space_enable ();
	result = reiser4_grab_space(2 * estimate_one_insert_into_item(tree_by_inode(inode)), BA_CAN_COMMIT);
	if (likely(!result))
		result = find_or_create_extent(page);

	all_grabbed2free();
	if (result != 0)
		SetPageError(page);
	return result;
}

/* plugin->u.file.capturepage handler */
reiser4_internal int
capturepage_unix_file(struct page * page) {
	int result;

	page_cache_get(page);
	unlock_page(page);
	result = capture_page_and_create_extent(page);
	lock_page(page);
	page_cache_release(page);
	return result;
}

static void
redirty_inode(struct inode *inode)
{
	spin_lock(&inode_lock);
	inode->i_state |= I_DIRTY;
	spin_unlock(&inode_lock);
}

/*
 * Support for "anonymous" pages and jnodes.
 *
 * When file is write-accessed through mmap pages can be dirtied from the user
 * level. In this case kernel is not notified until one of following happens:
 *
 *     (1) msync()
 *
 *     (2) truncate() (either explicit or through unlink)
 *
 *     (3) VM scanner starts reclaiming mapped pages, dirtying them before
 *     starting write-back.
 *
 * As a result of (3) ->writepage may be called on a dirty page without
 * jnode. Such page is called "anonymous" in reiser4. Certain work-loads
 * (iozone) generate huge number of anonymous pages. Emergency flush handles
 * this situation by creating jnode for anonymous page, starting IO on the
 * page, and marking jnode with JNODE_KEEPME bit so that it's not throw out of
 * memory. Such jnode is also called anonymous.
 *
 * reiser4_sync_sb() method tries to insert anonymous pages and jnodes into
 * tree. This is done by capture_anonymous_*() functions below.
 *
 */

/* this returns 1 if it captured page */
static int
capture_anonymous_page(struct page *pg, int keepme)
{
	struct address_space *mapping;
	jnode *node;
	int result;

	if (PageWriteback(pg))
		/* FIXME: do nothing? */
		return 0;

	mapping = pg->mapping;

	lock_page(pg);
	/* page is guaranteed to be in the mapping, because we are operating under rw-semaphore. */
	assert("nikita-3336", pg->mapping == mapping);
	node = jnode_of_page(pg);
	unlock_page(pg);
	if (!IS_ERR(node)) {
		result = jload(node);
		assert("nikita-3334", result == 0);
		assert("nikita-3335", jnode_page(node) == pg);
		result = capture_page_and_create_extent(pg);
		if (result == 0) {
			/*
			 * node will be captured into atom by
			 * capture_page_and_create_extent(). Atom
			 * cannot commit (because we have open
			 * transaction handle), and node cannot be
			 * truncated, because we have non-exclusive
			 * access to the file.
			 */
			assert("nikita-3327", node->atom != NULL);
			JF_CLR(node, JNODE_KEEPME);
			result = 1;
		} else
			warning("nikita-3329",
				"Cannot capture anon page: %i", result);
		jrelse(node);
		jput(node);
	} else
		result = PTR_ERR(node);

	return result;
}


#define CAPTURE_APAGE_BURST      (1024)

static int
capture_anonymous_pages(struct address_space *mapping, pgoff_t *index, long *captured)
{
	int result;
	unsigned to_capture;
	struct pagevec pvec;
	unsigned found_pages;
	jnode *jvec[PAGEVEC_SIZE];
	unsigned found_jnodes;
	pgoff_t cur, end;
	unsigned count;
	reiser4_tree * tree;
	unsigned i;

	result = 0;

	ON_TRACE(TRACE_CAPTURE_ANONYMOUS,
		 "capture anonymous: oid %llu: start index %lu\n",
		 get_inode_oid(mapping->host), *index);

	to_capture = CAPTURE_APAGE_BURST;
	found_jnodes = 0;
	tree = &get_super_private(mapping->host->i_sb)->tree;

	do {
		pagevec_init(&pvec, 0);

		cur = *index;
		count = min(pagevec_space(&pvec), to_capture);

		/* find and capture "anonymous" pages */
		found_pages = pagevec_lookup_tag(&pvec, mapping, index, PAGECACHE_TAG_REISER4_MOVED, count);
		if (found_pages != 0) {
			ON_TRACE(TRACE_CAPTURE_ANONYMOUS,
				 "oid %llu: found %u moved pages in range starting from (%lu)\n",
				 get_inode_oid(mapping->host), found_pages, cur);

			for (i = 0; i < pagevec_count(&pvec); i ++) {
				/* tag PAGECACHE_TAG_REISER4_MOVED will be cleared by set_page_dirty_internal which is
				   called when jnode is captured */
				result = capture_anonymous_page(pvec.pages[i], 0);
				if (result == 1) {
					(*captured) ++;
					result = 0;
					to_capture --;
				} else if (result < 0) {
					warning("vs-1454", "failed for moved page: result=%i (captured=%u)\n",
						result, CAPTURE_APAGE_BURST - to_capture);
					break;
				} else {
					/* result == 0. capture_anonymous_page returns 0 for Writeback-ed page */
					;
				}
			}
			pagevec_release(&pvec);
			if (result)
				return result;

			end = *index;
		} else
			/* there are no more anonymous pages, continue with anonymous jnodes only */
			end = (pgoff_t)-1;

#if REISER4_USE_EFLUSH

		/* capture anonymous jnodes between cur and end */
		while (cur < end && to_capture > 0) {
			pgoff_t nr_jnodes;

			nr_jnodes = min(to_capture, (unsigned)PAGEVEC_SIZE);

			/* spin_lock_eflush(mapping->host->i_sb); */
			RLOCK_TREE(tree);

			found_jnodes = radix_tree_gang_lookup_tag(jnode_tree_by_inode(mapping->host),
								  (void **)&jvec, cur, nr_jnodes,
								  EFLUSH_TAG_ANONYMOUS);
			if (found_jnodes != 0) {
				for (i = 0; i < found_jnodes; i ++) {
					if (index_jnode(jvec[i]) < end) {
						jref(jvec[i]);
						cur = index_jnode(jvec[i]) + 1;
					} else {
						found_jnodes = i;
						break;
					}
				}

				if (found_jnodes != 0) {
					/* there are anonymous jnodes from given range */
					/* spin_unlock_eflush(mapping->host->i_sb); */
					RUNLOCK_TREE(tree);

					ON_TRACE(TRACE_CAPTURE_ANONYMOUS,
						 "oid %llu: found %u anonymous jnodes in range (%lu %lu)\n",
						 get_inode_oid(mapping->host), found_jnodes, cur, end - 1);

					/* start i/o for eflushed nodes */
					for (i = 0; i < found_jnodes; i ++)
						jstartio(jvec[i]);

					for (i = 0; i < found_jnodes; i ++) {
						result = jload(jvec[i]);
						if (result == 0) {
							result = capture_anonymous_page(jnode_page(jvec[i]), 0);
							if (result == 1) {
								(*captured) ++;
								result = 0;
								to_capture --;
							} else if (result < 0) {
								jrelse(jvec[i]);
								warning("nikita-3328",
									"failed for anonymous jnode: result=%i (captured=%u)\n",
									result, CAPTURE_APAGE_BURST - to_capture);
								break;
							} else {
								/* result == 0. capture_anonymous_page returns 0 for Writeback-ed page */
								;
							}
							jrelse(jvec[i]);
						} else {
							warning("vs-1454", "jload for anonymous jnode failed: captured %u, result=%i\n",
								result, CAPTURE_APAGE_BURST - to_capture);
							break;
						}
					}
					for (i = 0; i < found_jnodes; i ++)
						jput(jvec[i]);
					if (result)
						return result;
					continue;
				}
			}
			RUNLOCK_TREE(tree);
			/* spin_unlock_eflush(mapping->host->i_sb);*/
			ON_TRACE(TRACE_CAPTURE_ANONYMOUS,
				 "oid %llu: no anonymous jnodes are found\n", get_inode_oid(mapping->host));
			break;
		}
#endif /* REISER4_USE_EFLUSH */
	} while (to_capture && (found_pages || found_jnodes) && result == 0);

	if (result) {
		warning("vs-1454", "Cannot capture anon pages: result=%i (captured=%d)\n",
			result, CAPTURE_APAGE_BURST - to_capture);
		return result;
	}

	assert("vs-1678", to_capture <= CAPTURE_APAGE_BURST);
	if (to_capture == 0)
		/* there may be left more pages */
		redirty_inode(mapping->host);

	ON_TRACE(TRACE_CAPTURE_ANONYMOUS,
		 "capture anonymous: oid %llu: end index %lu, captured %u\n",
		 get_inode_oid(mapping->host), *index, CAPTURE_APAGE_BURST - to_capture);

	return 0;
}

/*
 * Commit atom of the jnode of a page.
 */
static int
sync_page(struct page *page)
{
	int result;
	do {
		jnode *node;
		txn_atom *atom;

		lock_page(page);
		node = jprivate(page);
		if (node != NULL)
			atom = UNDER_SPIN(jnode, node, jnode_get_atom(node));
		else
			atom = NULL;
		unlock_page(page);
		result = sync_atom(atom);
	} while (result == -E_REPEAT);
	assert("nikita-3485", ergo(result == 0,
				   get_current_context()->trans->atom == NULL));
	return result;
}

/*
 * Commit atoms of pages on @pages list.
 * call sync_page for each page from mapping's page tree
 */
static int
sync_page_list(struct inode *inode)
{
	int result;
	struct address_space *mapping;
	unsigned long from; /* start index for radix_tree_gang_lookup */
	unsigned int found; /* return value for radix_tree_gang_lookup */

	mapping = inode->i_mapping;
	from = 0;
	result = 0;
	read_lock_irq(&mapping->tree_lock);
	while (result == 0) {
		struct page *page;

		found = radix_tree_gang_lookup(&mapping->page_tree, (void **)&page, from, 1);
		assert("", found < 2);
		if (found == 0)
			break;

		/* page may not leave radix tree because it is protected from truncating by inode->i_sem downed by
		   sys_fsync */
		page_cache_get(page);
		read_unlock_irq(&mapping->tree_lock);

		from = page->index + 1;

		result = sync_page(page);

		page_cache_release(page);
		read_lock_irq(&mapping->tree_lock);
	}

	read_unlock_irq(&mapping->tree_lock);
	return result;
}

static int
commit_file_atoms(struct inode *inode)
{
	int               result;
	unix_file_info_t *uf_info;
	reiser4_context  *ctx;

	/*
	 * close current transaction
	 */

	ctx = get_current_context();
	txn_restart(ctx);

	uf_info = unix_file_inode_data(inode);

	/*
	 * finish extent<->tail conversion if necessary
	 */

	get_exclusive_access(uf_info);
	if (inode_get_flag(inode, REISER4_PART_CONV)) {
		result = finish_conversion(inode);
		if (result != 0) {
			drop_exclusive_access(uf_info);
			return result;
		}
	}

	/*
	 * find what items file is made from
	 */

	result = find_file_state(uf_info);
	drop_exclusive_access(uf_info);
	if (result != 0)
		return result;

	/*
	 * file state cannot change because we are under ->i_sem
	 */

	switch(uf_info->container) {
	case UF_CONTAINER_EXTENTS:
		result =
			/*
			 * when we are called by
			 * filemap_fdatawrite->
			 *    do_writepages()->
			 *       reiser4_writepages()
			 *
			 * inode->i_mapping->dirty_pages are spices into
			 * ->io_pages, leaving ->dirty_pages dirty.
			 *
			 * When we are called from
			 * reiser4_fsync()->sync_unix_file(), we have to
			 * commit atoms of all pages on the ->dirty_list.
			 *
			 * So for simplicity we just commit ->io_pages and
			 * ->dirty_pages.
			 */
			sync_page_list(inode);
		break;
	case UF_CONTAINER_TAILS:
		/*
		 * NOTE-NIKITA probably we can be smarter for tails. For now
		 * just commit all existing atoms.
		 */
		result = txnmgr_force_commit_all(inode->i_sb, 0);
		break;
	case UF_CONTAINER_EMPTY:
		result = 0;
		break;
	case UF_CONTAINER_UNKNOWN:
	default:
		result = -EIO;
		break;
	}

	/*
	 * commit current transaction: there can be captured nodes from
	 * find_file_state() and finish_conversion().
	 */
	txn_restart(ctx);
	return result;
}

/*
 * this file plugin method is called to capture into current atom all
 * "anonymous pages", that is, pages modified through mmap(2). For each such
 * page this function creates jnode, captures this jnode, and creates (or
 * modifies) extent. Anonymous pages are kept on the special inode list. Some
 * of them can be emergency flushed. To cope with this list of eflushed jnodes
 * from this inode is scanned.
 */
reiser4_internal int
capture_unix_file(struct inode *inode, const struct writeback_control *wbc, long *captured)
{
	int               result;
	unix_file_info_t *uf_info;
	pgoff_t index;

	if (!inode_has_anonymous_pages(inode))
		return 0;

	result = 0;
	index = 0;
	do {
		reiser4_context ctx;

		uf_info = unix_file_inode_data(inode);
		/*
		 * locking: creation of extent requires read-semaphore on
		 * file. _But_, this function can also be called in the
		 * context of write system call from
		 * balance_dirty_pages(). So, write keeps semaphore (possible
		 * in write mode) on file A, and this function tries to
		 * acquire semaphore on (possibly) different file B. A/B
		 * deadlock is on a way. To avoid this try-lock is used
		 * here. When invoked from sys_fsync() and sys_fdatasync(),
		 * this function is out of reiser4 context and may safely
		 * sleep on semaphore.
		 */
		if (is_in_reiser4_context()) {
			if (down_read_trylock(&uf_info->latch) == 0) {
				result = RETERR(-EBUSY);
				break;
			}
		} else
			down_read(&uf_info->latch);

		init_context(&ctx, inode->i_sb);
		/* avoid recursive calls to ->sync_inodes */
		ctx.nobalance = 1;
		assert("zam-760", lock_stack_isclean(get_current_lock_stack()));

		LOCK_CNT_INC(inode_sem_r);

		result = capture_anonymous_pages(inode->i_mapping, &index, captured);
		up_read(&uf_info->latch);
		LOCK_CNT_DEC(inode_sem_r);
		if (result != 0 || wbc->sync_mode != WB_SYNC_ALL) {
			reiser4_exit_context(&ctx);
			break;
		}
		result = commit_file_atoms(inode);
		reiser4_exit_context(&ctx);
	} while (result == 0 && inode_has_anonymous_pages(inode) /* FIXME: it should be: there are anonymous pages with
								    page->index >= index */);

	return result;
}

/*
 * ->sync() method for unix file.
 *
 * We are trying to be smart here. Instead of committing all atoms (original
 * solution), we scan dirty pages of this file and commit all atoms they are
 * part of.
 *
 * Situation is complicated by anonymous pages: i.e., extent-less pages
 * dirtied through mmap. Fortunately sys_fsync() first calls
 * filemap_fdatawrite() that will ultimately call reiser4_writepages(), insert
 * all missing extents and capture anonymous pages.
 */
reiser4_internal int
sync_unix_file(struct inode *inode, int datasync)
{
	int result;
	reiser4_context *ctx;

	ctx = get_current_context();
	assert("nikita-3486", ctx->trans->atom == NULL);
	result = commit_file_atoms(inode);
	assert("nikita-3484", ergo(result == 0, ctx->trans->atom == NULL));
	if (result == 0 && !datasync) {
		do {
			/* commit "meta-data"---stat data in our case */
			lock_handle lh;
			coord_t coord;
			reiser4_key key;

			coord_init_zero(&coord);
			init_lh(&lh);
			/* locate stat-data in a tree and return with znode
			 * locked */
			result = locate_inode_sd(inode, &key, &coord, &lh);
			if (result == 0) {
				jnode    *node;
				txn_atom *atom;

				node = jref(ZJNODE(coord.node));
				done_lh(&lh);
				txn_restart(ctx);
				LOCK_JNODE(node);
				atom = jnode_get_atom(node);
				UNLOCK_JNODE(node);
				result = sync_atom(atom);
				jput(node);
			} else
				done_lh(&lh);
		} while (result == -E_REPEAT);
	}
	return result;
}

/* plugin->u.file.readpage
   page must be not out of file. This is called either via page fault and in that case vp is struct file *file, or on
   truncate when last page of a file is to be read to perform its partial truncate and in that case vp is 0
*/
reiser4_internal int
readpage_unix_file(void *vp, struct page *page)
{
	int result;
	struct inode *inode;
	lock_handle lh;
	reiser4_key key;
	item_plugin *iplug;
	hint_t hint;
	coord_t *coord;
	struct file *file;


	reiser4_stat_inc(file.page_ops.readpage_calls);

	assert("vs-1062", PageLocked(page));
	assert("vs-1061", page->mapping && page->mapping->host);
	assert("vs-1078", (page->mapping->host->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT)));

	inode = page->mapping->host;

	file = vp;
	result = load_file_hint(file, &hint, &lh);
	if (result)
		return result;

	/* get key of first byte of the page */
	key_by_inode_unix_file(inode, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	/* look for file metadata corresponding to first byte of page */
	unlock_page(page);
	result = find_file_item(&hint, &key, ZNODE_READ_LOCK, 0/* ra_info */, inode);
	lock_page(page);
	if (result != CBK_COORD_FOUND) {
		/* this indicates file corruption */
		done_lh(&lh);
		return result;
	}

	if (PageUptodate(page)) {
		done_lh(&lh);
		unlock_page(page);
		return 0;
	}

	coord = &hint.coord.base_coord;
	result = zload(coord->node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	if (!hint.coord.valid)
		validate_extended_coord(&hint.coord, (loff_t) page->index << PAGE_CACHE_SHIFT);

	if (!coord_is_existing_unit(coord)) {
		/* this indicates corruption */
		warning("vs-280",
			"Looking for page %lu of file %llu (size %lli). "
			"No file items found (%d). "
			"File is corrupted?\n",
			page->index, get_inode_oid(inode), inode->i_size, result);
		zrelse(coord->node);
		done_lh(&lh);
		return RETERR(-EIO);
	}

	/* get plugin of found item or use plugin if extent if there are no
	   one */
	iplug = item_plugin_by_coord(coord);
	if (iplug->s.file.readpage)
		result = iplug->s.file.readpage(coord, page);
	else
		result = RETERR(-EINVAL);

	if (!result) {
		set_key_offset(&key, (loff_t) (page->index + 1) << PAGE_CACHE_SHIFT);
		/* FIXME should call set_hint() */
		unset_hint(&hint);
	} else
		unset_hint(&hint);
	zrelse(coord->node);
	done_lh(&lh);

	save_file_hint(file, &hint);

	assert("vs-979", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));

	return result;
}

/* returns 1 if file of that size (@new_size) has to be stored in unformatted
   nodes */
/* Audited by: green(2002.06.15) */
static int
should_have_notail(const unix_file_info_t *uf_info, loff_t new_size)
{
	if (!uf_info->tplug)
		return 1;
	return !uf_info->tplug->have_tail(unix_file_info_to_inode(uf_info),
					  new_size);

}

static reiser4_block_nr unix_file_estimate_read(struct inode *inode,
						loff_t count UNUSED_ARG)
{
    	/* We should reserve one block, because of updating of the stat data
	   item */
	assert("vs-1249", inode_file_plugin(inode)->estimate.update == estimate_update_common);
	return estimate_update_common(inode);
}

/* plugin->u.file.read

   the read method for the unix_file plugin

*/
reiser4_internal ssize_t
read_unix_file(struct file *file, char *buf, size_t read_amount, loff_t *off)
{
	int result;
	struct inode *inode;
	flow_t f;
	lock_handle lh;
	hint_t hint;
	coord_t *coord;
	size_t read;
	reiser4_block_nr needed;
	int (*read_f) (struct file *, flow_t *, hint_t *);
	unix_file_info_t *uf_info;
	loff_t size;

	if (unlikely(!read_amount))
		return 0;

	inode = file->f_dentry->d_inode;
	assert("vs-972", !inode_get_flag(inode, REISER4_NO_SD));

	uf_info = unix_file_inode_data(inode);
	get_nonexclusive_access(uf_info);

	size = i_size_read(inode);
	if (*off >= size) {
		/* position to read from is past the end of file */
		drop_access(uf_info);
		return 0;
	}

	if (*off + read_amount > size)
		read_amount = size - *off;

	/* we have nonexclusive access (NA) obtained. File's container may not change until we drop NA. If possible -
	   calculate read function beforehand */
 	switch(uf_info->container) {
	case UF_CONTAINER_EXTENTS:
		read_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.read;
		break;

	case UF_CONTAINER_TAILS:
		/* this is read-ahead for tails-only files */
		result = reiser4_file_readahead(file, *off, read_amount);
		if (result) {
			drop_access(uf_info);
			return result;
		}

		read_f = item_plugin_by_id(FORMATTING_ID)->s.file.read;
		break;

	case UF_CONTAINER_UNKNOWN:
		read_f = 0;
		break;

	case UF_CONTAINER_EMPTY:
	default:
		warning("vs-1297", "File (ino %llu) has unexpected state: %d\n", get_inode_oid(inode), uf_info->container);
		drop_access(uf_info);
		return RETERR(-EIO);
	}

	needed = unix_file_estimate_read(inode, read_amount); /* FIXME: tree_by_inode(inode)->estimate_one_insert */
	result = reiser4_grab_space(needed, BA_CAN_COMMIT);
	if (result != 0) {
		drop_access(uf_info);
		return result;
	}

	/* build flow */
	assert("vs-1250", inode_file_plugin(inode)->flow_by_inode == flow_by_inode_unix_file);
	result = flow_by_inode_unix_file(inode, buf, 1 /* user space */ , read_amount, *off, READ_OP, &f);
	if (unlikely(result)) {
		drop_access(uf_info);
		return result;
	}

	/* get seal and coord sealed with it from reiser4 private data of struct file.  The coord will tell us where our
	   last read of this file finished, and the seal will help to determine if that location is still valid.
	*/
	coord = &hint.coord.base_coord;
	result = load_file_hint(file, &hint, &lh);

	while (f.length && result == 0) {
		result = find_file_item(&hint, &f.key, ZNODE_READ_LOCK, NULL, inode);
		if (cbk_errored(result))
			/* error happened */
			break;

		if (coord->between != AT_UNIT)
			/* there were no items corresponding to given offset */
			break;

		/*coord_clear_iplug(coord);*/
		hint.coord.valid = 0;
		result = zload(coord->node);
		if (unlikely(result))
			break;

		validate_extended_coord(&hint.coord, get_key_offset(&f.key));

		/* call item's read method */
		if (!read_f)
			read_f = item_plugin_by_coord(coord)->s.file.read;
		result = read_f(file, &f, &hint);
		zrelse(coord->node);
		done_lh(&lh);
	}

	done_lh(&lh);
	save_file_hint(file, &hint);

	read = read_amount - f.length;
	if (read)
		/* something was read. Update stat data */
		update_atime(inode);

	drop_access(uf_info);

	/* update position in a file */
	*off += read;

	/* return number of read bytes or error code if nothing is read */
	return read ?: result;
}

typedef int (*write_f_t)(struct inode *, flow_t *, hint_t *, int grabbed, write_mode_t);

/* This searches for write position in the tree and calls write method of
   appropriate item to actually copy user data into filesystem. This loops
   until all the data from flow @f are written to a file. */
static loff_t
append_and_or_overwrite(struct file *file, struct inode *inode, flow_t *flow)
{
	int result;
	lock_handle lh;
	hint_t hint;
	loff_t to_write;
	write_f_t write_f;
	file_container_t cur_container, new_container;
	znode *loaded;
	unix_file_info_t *uf_info;

	assert("nikita-3031", schedulable());
	assert("vs-1109", get_current_context()->grabbed_blocks == 0);

	/* get seal and coord sealed with it from reiser4 private data of
	   struct file */
	result = load_file_hint(file, &hint, &lh);
	if (result)
		return result;

	uf_info = unix_file_inode_data(inode);

	to_write = flow->length;
	while (flow->length) {
		assert("vs-1123", get_current_context()->grabbed_blocks == 0);

		{
			size_t count;

			count = PAGE_CACHE_SIZE;

			if (count > flow->length)
				count = flow->length;
			fault_in_pages_readable(flow->data, count);
		}

		if (to_write == flow->length) {
			/* it may happend that find_next_item will have to insert empty node to the tree (empty leaf
			   node between two extent items) */
			result = reiser4_grab_space_force(1 + estimate_one_insert_item(tree_by_inode(inode)), 0);
			if (result)
				return result;
		}
		/* look for file's metadata (extent or tail item) corresponding to position we write to */
		result = find_file_item(&hint, &flow->key, ZNODE_WRITE_LOCK, NULL/* ra_info */, inode);
		all_grabbed2free();
		if (IS_CBKERR(result)) {
			/* error occurred */
			done_lh(&lh);
			return result;
		}

		cur_container = uf_info->container;
		switch (cur_container) {
		case UF_CONTAINER_EMPTY:
			assert("vs-1196", get_key_offset(&flow->key) == 0);
			if (should_have_notail(uf_info, get_key_offset(&flow->key) + flow->length)) {
				new_container = UF_CONTAINER_EXTENTS;
				write_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.write;
			} else {
				new_container = UF_CONTAINER_TAILS;
				write_f = item_plugin_by_id(FORMATTING_ID)->s.file.write;
			}
			break;

		case UF_CONTAINER_EXTENTS:
			write_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.write;
			new_container = cur_container;
			break;

		case UF_CONTAINER_TAILS:
			if (should_have_notail(uf_info, get_key_offset(&flow->key) + flow->length)) {
				longterm_unlock_znode(&lh);
				if (!ea_obtained(uf_info))
					return RETERR(-E_REPEAT);
				result = tail2extent(uf_info);
				if (result)
					return result;
				unset_hint(&hint);
				continue;
			}
			write_f = item_plugin_by_id(FORMATTING_ID)->s.file.write;
			new_container = cur_container;
			break;

		default:
			longterm_unlock_znode(&lh);
			return RETERR(-EIO);
		}

		result = zload(lh.node);
		if (result) {
			longterm_unlock_znode(&lh);
			return result;
		}
		loaded = lh.node;

		result = write_f(inode,
				 flow,
				 &hint,
				 0/* not grabbed */,
				 how_to_write(&hint.coord, &flow->key));

		assert("nikita-3142", get_current_context()->grabbed_blocks == 0);
		if (cur_container == UF_CONTAINER_EMPTY && to_write != flow->length) {
			/* file was empty and we have written something and we are having exclusive access to the file -
			   change file state */
			assert("vs-1195", (new_container == UF_CONTAINER_TAILS ||
					   new_container == UF_CONTAINER_EXTENTS));
			uf_info->container = new_container;
		}
		zrelse(loaded);
		done_lh(&lh);
		if (result && result != -E_REPEAT)
			break;
		preempt_point();
	}
	if (result == -EEXIST)
		printk("write returns EEXIST!\n");
	save_file_hint(file, &hint);

	/* if nothing were written - there must be an error */
	assert("vs-951", ergo((to_write == flow->length), result < 0));
	assert("vs-1110", get_current_context()->grabbed_blocks == 0);

	return (to_write - flow->length) ? (to_write - flow->length) : result;
}

/* make flow and write data (@buf) to the file. If @buf == 0 - hole of size @count will be created. This is called with
   uf_info->latch either read- or write-locked */
static loff_t
write_flow(struct file *file, struct inode *inode, const char *buf, loff_t count, loff_t pos)
{
	int result;
	flow_t flow;

	assert("vs-1251", inode_file_plugin(inode)->flow_by_inode == flow_by_inode_unix_file);

	result = flow_by_inode_unix_file(inode,
					 (char *)buf, 1 /* user space */, count, pos, WRITE_OP, &flow);
	if (result)
		return result;

	return append_and_or_overwrite(file, inode, &flow);
}

reiser4_internal void
drop_access(unix_file_info_t *uf_info)
{
	if (uf_info->exclusive_use)
		drop_exclusive_access(uf_info);
	else
		drop_nonexclusive_access(uf_info);
}

reiser4_internal struct page *
unix_file_filemap_nopage(struct vm_area_struct *area, unsigned long address, int * unused)
{
	struct page *page;
	struct inode *inode;

	inode = area->vm_file->f_dentry->d_inode;

	/* block filemap_nopage if copy on capture is processing with a node of this file */
	down_read(&reiser4_inode_data(inode)->coc_sem);
	get_nonexclusive_access(unix_file_inode_data(inode));

	page = filemap_nopage(area, address, 0);

	drop_nonexclusive_access(unix_file_inode_data(inode));
	up_read(&reiser4_inode_data(inode)->coc_sem);
	return page;
}

static struct vm_operations_struct unix_file_vm_ops = {
	.nopage = unix_file_filemap_nopage,
};

/* This function takes care about @file's pages. First of all it checks if
   filesystems readonly and if so gets out. Otherwise, it throws out all
   pages of file if it was mapped for read and going to be mapped for write
   and consists of tails. This is done in order to not manage few copies
   of the data (first in page cache and second one in tails them selves)
   for the case of mapping files consisting tails.

   Here also tail2extent conversion is performed if it is allowed and file
   is going to be written or mapped for write. This functions may be called
   from write_unix_file() or mmap_unix_file(). */
static int
check_pages_unix_file(struct inode *inode)
{
	reiser4_invalidate_pages(inode->i_mapping, 0,
				 (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT);
	return unpack(inode, 0 /* not forever */);
}

/* plugin->u.file.mmap
   make sure that file is built of extent blocks. An estimation is in tail2extent */

/* This sets inode flags: file has mapping. if file is mmaped with VM_MAYWRITE - invalidate pages and convert. */
reiser4_internal int
mmap_unix_file(struct file *file, struct vm_area_struct *vma)
{
	int result;
	struct inode *inode;
	unix_file_info_t *uf_info;

	inode = file->f_dentry->d_inode;
	uf_info = unix_file_inode_data(inode);

	get_exclusive_access(uf_info);

	if (!IS_RDONLY(inode) && (vma->vm_flags & (VM_MAYWRITE | VM_SHARED))) {
		/* we need file built of extent items. If it is still built of tail items we have to convert it. Find
		   what items the file is built of */
		result = finish_conversion(inode);
		if (result) {
			drop_exclusive_access(uf_info);
			return result;
		}

		result = find_file_state(uf_info);
		if (result != 0) {
			drop_exclusive_access(uf_info);
			return result;
		}

		assert("vs-1648", (uf_info->container == UF_CONTAINER_TAILS ||
				   uf_info->container == UF_CONTAINER_EXTENTS ||
				   uf_info->container == UF_CONTAINER_EMPTY));
		if (uf_info->container == UF_CONTAINER_TAILS) {
			/* invalidate all pages and convert file from tails to extents */
			result = check_pages_unix_file(inode);
			if (result) {
				drop_exclusive_access(uf_info);
				return result;
			}
		}
	}

	result = generic_file_mmap(file, vma);
	if (result == 0) {
		/* mark file as having mapping. */
		inode_set_flag(inode, REISER4_HAS_MMAP);
		vma->vm_ops = &unix_file_vm_ops;
	}

	drop_exclusive_access(uf_info);
	return result;
}

static ssize_t
write_file(struct file *file, /* file to write to */
	   const char *buf, /* address of user-space buffer */
	   size_t count, /* number of bytes to write */
	   loff_t *off /* position in file to write to */)
{
	struct inode *inode;
	ssize_t written;	/* amount actually written so far */
	loff_t pos;		/* current location in the file */

	inode = file->f_dentry->d_inode;

	/* estimation for write is entrusted to write item plugins */
	pos = *off;

	if (inode->i_size < pos) {
		/* pos is set past real end of file */
		written = append_hole(inode, pos);
		if (written)
			return written;
		assert("vs-1081", pos == inode->i_size);
	}

	/* write user data to the file */
	written = write_flow(file, inode, buf, count, pos);
	if (written > 0)
		/* update position in a file */
		*off = pos + written;

	/* return number of written bytes, or error code */
	return written;
}

/* plugin->u.file.write */
reiser4_internal ssize_t
write_unix_file(struct file *file, /* file to write to */
		const char *buf, /* address of user-space buffer */
		size_t count, /* number of bytes to write */
		loff_t *off /* position in file to write to */)
{
	struct inode *inode;
	ssize_t written;	/* amount actually written so far */
	int result;

	if (unlikely(count == 0))
		return 0;

	inode = file->f_dentry->d_inode;
	assert("vs-947", !inode_get_flag(inode, REISER4_NO_SD));

	/* linux's VM requires this. See mm/vmscan.c:shrink_list() */
	current->backing_dev_info = inode->i_mapping->backing_dev_info;

	down(&inode->i_sem);
	written = generic_write_checks(file, off, &count, 0);
	if (written == 0) {
		unix_file_info_t *uf_info;

		uf_info = unix_file_inode_data(inode);

		if (inode_get_flag(inode, REISER4_HAS_MMAP)) {
			/* file has been mmaped before. If it is built of
			   tails - invalidate pages created so far and convert
			   to extents */
			get_exclusive_access(uf_info);
			written = finish_conversion(inode);
			if (written == 0)
				if (uf_info->container == UF_CONTAINER_TAILS)
					written = check_pages_unix_file(inode);

			drop_exclusive_access(uf_info);
		}
		if (written == 0) {
			int rep;
			int try_free_space = 1;

			for (rep = 0; ; ++ rep) {
				if (inode_get_flag(inode,
						   REISER4_PART_CONV)) {
					get_exclusive_access(uf_info);
					written = finish_conversion(inode);
					if (written != 0) {
						drop_access(uf_info);
						break;
					}
				} else if (inode->i_size == 0 || rep)
					get_exclusive_access(uf_info);
				else
					get_nonexclusive_access(uf_info);

				if (rep == 0) {
					/* UNIX behavior: clear suid bit on
					 * file modification. This cannot be
					 * done earlier, because removing suid
					 * bit captures blocks into
					 * transaction, which should be done
					 * after taking exclusive access on
					 * the file. */
					written = remove_suid(file->f_dentry);
					if (written != 0) {
						drop_access(uf_info);
						break;
					}
					grab_space_enable();
				}

				all_grabbed2free();
				written = write_file(file, buf, count, off);
				drop_access(uf_info);

				/* With no locks held we can commit atoms in
				 * attempt to recover free space. */
				if (written == -ENOSPC && try_free_space) {
					txnmgr_force_commit_all(inode->i_sb, 0);
					try_free_space = 0;
					continue;
				}

				if (written == -E_REPEAT)
					/* write_file required exclusive
					 * access (for tail2extent). It
					 * returned E_REPEAT so that we
					 * restart it with exclusive access */
					txn_restart_current();
				else
					break;
			}
		}
	}

	if ((file->f_flags & O_SYNC) || IS_SYNC(inode)) {
		txn_restart_current();
		result = sync_unix_file(inode, 0/* data and stat data */);
		if (result)
			warning("reiser4-7", "failed to sync file %llu",
				get_inode_oid(inode));
	}
	up(&inode->i_sem);
	current->backing_dev_info = 0;
	return written;
}

/* plugin->u.file.release() convert all extent items into tail items if
   necessary */
reiser4_internal int
release_unix_file(struct inode *object, struct file *file)
{
	unix_file_info_t *uf_info;
	int result;

	uf_info = unix_file_inode_data(object);
	result = 0;

	get_exclusive_access(uf_info);
	if (atomic_read(&file->f_dentry->d_count) == 1 &&
	    uf_info->container == UF_CONTAINER_EXTENTS &&
	    !should_have_notail(uf_info, object->i_size) &&
	    !rofs_inode(object)) {
		result = extent2tail(uf_info);
		if (result != 0) {
			warning("nikita-3233", "Failed to convert in %s (%llu)",
				__FUNCTION__, get_inode_oid(object));
			print_inode("inode", object);
		}
	}
	drop_exclusive_access(uf_info);
	return 0;
}

static void
set_file_notail(struct inode *inode)
{
	reiser4_inode *state;
	formatting_plugin   *tplug;

	state = reiser4_inode_data(inode);
	tplug = formatting_plugin_by_id(NEVER_TAILS_FORMATTING_ID);
	plugin_set_formatting(&state->pset, tplug);
	inode_set_plugin(inode,
			 formatting_plugin_to_plugin(tplug), PSET_FORMATTING);
}

/* if file is built of tails - convert it to extents */
static int
unpack(struct inode *inode, int forever)
{
	int            result = 0;
	unix_file_info_t *uf_info;


	uf_info = unix_file_inode_data(inode);
	assert("vs-1628", ea_obtained(uf_info));

	result = find_file_state(uf_info);
	assert("vs-1074", ergo(result == 0, uf_info->container != UF_CONTAINER_UNKNOWN));
	if (result == 0) {
		if (uf_info->container == UF_CONTAINER_TAILS)
			result = tail2extent(uf_info);
		if (result == 0 && forever)
			set_file_notail(inode);
		if (result == 0) {
			__u64 tograb;

			grab_space_enable();
			tograb = inode_file_plugin(inode)->estimate.update(inode);
			result = reiser4_grab_space(tograb, BA_CAN_COMMIT);
			if (result == 0)
				update_atime(inode);
		}
	}

	return result;
}

/* plugin->u.file.ioctl */
reiser4_internal int
ioctl_unix_file(struct inode *inode, struct file *filp UNUSED_ARG, unsigned int cmd, unsigned long arg UNUSED_ARG)
{
	int result;

	switch (cmd) {
	case REISER4_IOC_UNPACK:
		get_exclusive_access(unix_file_inode_data(inode));
		result = unpack(inode, 1 /* forever */);
		drop_exclusive_access(unix_file_inode_data(inode));
		break;

	default:
		result = RETERR(-ENOSYS);
		break;
	}
	return result;
}

/* plugin->u.file.get_block */
reiser4_internal int
get_block_unix_file(struct inode *inode,
		    sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	assert("vs-1091", create == 0);

	key_by_inode_unix_file(inode, (loff_t) block * current_blocksize, &key);

	init_lh(&lh);
	result = find_file_item_nohint(&coord, &lh, &key, ZNODE_READ_LOCK, inode);
	if (cbk_errored(result)) {
		done_lh(&lh);
		return result;
	}

	/*coord_clear_iplug(&coord);*/
	result = zload(coord.node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(&coord);
	if (iplug->s.file.get_block)
		result = iplug->s.file.get_block(&coord, block, bh_result);
	else
		result = RETERR(-EINVAL);

	zrelse(coord.node);
	done_lh(&lh);
	return result;
}

/* plugin->u.file.flow_by_inode
   initialize flow (key, length, buf, etc) */
reiser4_internal int
flow_by_inode_unix_file(struct inode *inode /* file to build flow for */ ,
			char *buf /* user level buffer */ ,
			int user  /* 1 if @buf is of user space, 0 - if it is kernel space */ ,
			loff_t size /* buffer size */ ,
			loff_t off /* offset to start operation(read/write) from */ ,
			rw_op op /* READ or WRITE */ ,
			flow_t *flow /* resulting flow */ )
{
	assert("nikita-1100", inode != NULL);

	flow->length = size;
	flow->data = buf;
	flow->user = user;
	flow->op = op;
	assert("nikita-1931", inode_file_plugin(inode) != NULL);
	assert("nikita-1932", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	/* calculate key of write position and insert it into flow->key */
	return key_by_inode_unix_file(inode, off, &flow->key);
}

/* plugin->u.file.key_by_inode */
reiser4_internal int
key_by_inode_unix_file(struct inode *inode, loff_t off, reiser4_key *key)
{
	return key_by_inode_and_offset_common(inode, off, key);
}

/* plugin->u.file.set_plug_in_sd = NULL
   plugin->u.file.set_plug_in_inode = NULL
   plugin->u.file.create_blank_sd = NULL */
/* plugin->u.file.delete */
/*
   plugin->u.file.add_link = add_link_common
   plugin->u.file.rem_link = NULL */

/* plugin->u.file.owns_item
   this is common_file_owns_item with assertion */
/* Audited by: green(2002.06.15) */
reiser4_internal int
owns_item_unix_file(const struct inode *inode	/* object to check against */ ,
		    const coord_t *coord /* coord to check */ )
{
	int result;

	result = owns_item_common(inode, coord);
	if (!result)
		return 0;
	if (item_type_by_coord(coord) != UNIX_FILE_METADATA_ITEM_TYPE)
		return 0;
	assert("vs-547",
	       item_id_by_coord(coord) == EXTENT_POINTER_ID ||
	       item_id_by_coord(coord) == FORMATTING_ID);
	return 1;
}

static int
setattr_truncate(struct inode *inode, struct iattr *attr)
{
	int result;
	int s_result;
	loff_t old_size;
	reiser4_tree *tree;

	inode_check_scale(inode, inode->i_size, attr->ia_size);

	old_size = inode->i_size;
	tree = tree_by_inode(inode);

	result = safe_link_grab(tree, BA_CAN_COMMIT);
	if (result == 0)
		result = safe_link_add(inode, SAFE_TRUNCATE);
	all_grabbed2free();
	if (result == 0)
		result = truncate_file_body(inode, attr->ia_size);
	if (result)
		warning("vs-1588", "truncate_file failed: oid %lli, old size %lld, new size %lld, retval %d",
			get_inode_oid(inode), old_size, attr->ia_size, result);

	s_result = safe_link_grab(tree, BA_CAN_COMMIT);
	if (s_result == 0)
		s_result = safe_link_del(inode, SAFE_TRUNCATE);
	if (s_result != 0) {
		warning("nikita-3417", "Cannot kill safelink %lli: %i",
			get_inode_oid(inode), s_result);
	}
	safe_link_release(tree);
	all_grabbed2free();
	return result;
}

/* plugin->u.file.setattr method */
/* This calls inode_setattr and if truncate is in effect it also takes
   exclusive inode access to avoid races */
reiser4_internal int
setattr_unix_file(struct inode *inode,	/* Object to change attributes */
		  struct iattr *attr /* change description */ )
{
	int result;

	if (attr->ia_valid & ATTR_SIZE) {
		/* truncate does reservation itself and requires exclusive
		 * access obtained */
		unix_file_info_t *ufo;

		ufo = unix_file_inode_data(inode);
		get_exclusive_access(ufo);
		result = setattr_truncate(inode, attr);
		drop_exclusive_access(ufo);
	} else
		result = setattr_common(inode, attr);

	return result;
}

/* plugin->u.file.can_add_link = common_file_can_add_link */
/* VS-FIXME-HANS: why does this always resolve to extent pointer?  this wrapper serves what purpose?  get rid of it. */
/* plugin->u.file.readpages method */
reiser4_internal void
readpages_unix_file(struct file *file, struct address_space *mapping,
		    struct list_head *pages)
{
	reiser4_file_fsdata *fsdata;
	item_plugin *iplug;

	/* FIXME: readpages_unix_file() only supports files built of extents. */
	if (unix_file_inode_data(mapping->host)->container != UF_CONTAINER_EXTENTS)
		return;

	fsdata = reiser4_get_file_fsdata(file);
	iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	iplug->s.file.readpages(fsdata->reg.coord, mapping, pages);
	return;
}

/* plugin->u.file.init_inode_data */
reiser4_internal void
init_inode_data_unix_file(struct inode *inode,
			  reiser4_object_create_data *crd, int create)
{
	unix_file_info_t *data;

	data = unix_file_inode_data(inode);
	data->container = create ? UF_CONTAINER_EMPTY : UF_CONTAINER_UNKNOWN;
	init_rwsem(&data->latch);
	data->tplug = inode_formatting_plugin(inode);
	data->exclusive_use = 0;

#if REISER4_DEBUG
	data->ea_owner = 0;
#endif
	init_inode_ordering(inode, crd, create);
}

/* VS-FIXME-HANS: what is pre deleting all about? */
/* plugin->u.file.pre_delete */
reiser4_internal int
pre_delete_unix_file(struct inode *inode)
{
	/* FIXME: put comment here */
	/*if (inode->i_size == 0)
	  return 0;*/
	return truncate_file_body(inode, 0/* size */);
}

/* Reads @count bytes from @file and calls @actor for every page read. This is
   needed for loop back devices support. */
reiser4_internal ssize_t sendfile_common (
	struct file *file, loff_t *ppos, size_t count, read_actor_t actor, void __user *target)
{
	file_plugin *fplug;
	struct inode *inode;
	read_descriptor_t desc;
	struct page *page = NULL;
	int ret = 0;

	assert("umka-3108", file != NULL);

	inode = file->f_dentry->d_inode;

	desc.error = 0;
	desc.written = 0;
	desc.arg.data = target;
	desc.count = count;

	fplug = inode_file_plugin(inode);
	if (fplug->readpage == NULL)
		return RETERR(-EINVAL);

	while (desc.count != 0) {
		unsigned long read_request_size;
		unsigned long index;
		unsigned long offset;
		loff_t file_size = i_size_read(inode);

		if (*ppos >= file_size)
			break;

		index = *ppos >> PAGE_CACHE_SHIFT;
		offset = *ppos & ~PAGE_CACHE_MASK;

		page_cache_readahead(inode->i_mapping, &file->f_ra, file, offset);

		/* determine valid read request size. */
		read_request_size = PAGE_CACHE_SIZE - offset;
		if (read_request_size > desc.count)
			read_request_size = desc.count;
		if (*ppos + read_request_size >= file_size) {
			read_request_size = file_size - *ppos;
			if (read_request_size == 0)
				break;
		}
		page = grab_cache_page(inode->i_mapping, index);
		if (unlikely(page == NULL)) {
			desc.error = RETERR(-ENOMEM);
			break;
		}

		if (PageUptodate(page))
			/* process locked, up-to-date  page by read actor */
			goto actor;

		ret = fplug->readpage(file, page);
		if (ret != 0) {
			SetPageError(page);
			ClearPageUptodate(page);
			desc.error = ret;
			goto fail_locked_page;
		}

		lock_page(page);
		if (!PageUptodate(page)) {
			desc.error = RETERR(-EIO);
			goto fail_locked_page;
		}

	actor:
		ret = actor(&desc, page, offset, read_request_size);
		unlock_page(page);
		page_cache_release(page);

		(*ppos) += ret;

		if (ret != read_request_size)
			break;
	}

	if (0) {
	fail_locked_page:
		unlock_page(page);
		page_cache_release(page);
	}

	update_atime(inode);

	if (desc.written)
		return desc.written;
	return desc.error;
}

reiser4_internal ssize_t sendfile_unix_file(struct file *file, loff_t *ppos, size_t count,
					    read_actor_t actor, void __user *target)
{
	ssize_t ret;
	struct inode *inode;
	unix_file_info_t *ufo;

	inode = file->f_dentry->d_inode;
	ufo = unix_file_inode_data(inode);

	down(&inode->i_sem);
	inode_set_flag(inode, REISER4_HAS_MMAP);
	up(&inode->i_sem);

	get_nonexclusive_access(ufo);
	ret = sendfile_common(file, ppos, count, actor, target);
	drop_nonexclusive_access(ufo);
	return ret;
}

reiser4_internal int prepare_write_unix_file(struct file *file, struct page *page,
					     unsigned from, unsigned to)
{
	unix_file_info_t *uf_info;
	int ret;

	uf_info = unix_file_inode_data(file->f_dentry->d_inode);
	get_exclusive_access(uf_info);
	ret = find_file_state(uf_info);
	if (ret == 0) {
		if (uf_info->container == UF_CONTAINER_TAILS)
			ret = -EINVAL;
		else
			ret = prepare_write_common(file, page, from, to);
	}
	drop_exclusive_access(uf_info);
	return ret;
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
