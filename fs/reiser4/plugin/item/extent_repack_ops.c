/* Copyright 2003 by Hans Reiser. */

#include "item.h"
#include "../../key.h"
#include "../../super.h"
#include "../../carry.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../../emergency_flush.h"
#include "../../prof.h"
#include "../../flush.h"
#include "../../tap.h"
#include "../object.h"

#include "../../repacker.h"
#include "extent.h"

static int get_reiser4_inode_by_tap (struct inode ** result, tap_t * tap)
{
	reiser4_key ext_key;

	unit_key_by_coord(tap->coord, &ext_key);
	return get_reiser4_inode_by_key(result, &ext_key);
}

static jnode * get_jnode_by_mapping (struct inode * inode, long index)
{
	struct page * page;
	jnode * node;

	page = grab_cache_page(inode->i_mapping, index);
	if (page == NULL)
		return ERR_PTR(-ENOMEM);
	node = jnode_of_page(page);
	unlock_page(page);
	page_cache_release(page);
	return node;
}

static int mark_jnode_for_repacking (jnode * node)
{
	int ret = 0;

	LOCK_JNODE(node);
	ret = try_capture(node, ZNODE_WRITE_LOCK, 0, 0/* no can_coc */);
	if (ret) {
		UNLOCK_JNODE(node);
		return ret;
	}

	jnode_make_dirty_locked(node);
	UNLOCK_JNODE(node);
	JF_SET(node, JNODE_REPACK);

	ret = jload(node);
	if (ret == 0) {
		struct page * page;

		page = jnode_page(node);
		lock_page(page);
		set_page_dirty_internal(page, 0);
		unlock_page(page);
		jrelse(node);
	}

	return ret;
}

/*
   Mark jnodes of given extent for repacking.
   @tap : lock, coord and load status for the tree traversal position,
   @max_nr_marked: a maximum number of nodes which can be marked for repacking,
   @return: error code if < 0, number of marked nodes otherwise.
*/
reiser4_internal int mark_extent_for_repacking (tap_t * tap, int max_nr_marked)
{
	coord_t * coord = tap->coord;
	reiser4_extent *ext;
	int nr_marked;
	struct inode * inode;
	unsigned long index, pos_in_extent;
	reiser4_block_nr width, start;
	int ret;

	ext = extent_by_coord(coord);

	if (state_of_extent(ext) == HOLE_EXTENT)
		return 0;

	width = extent_get_width(ext);
	start = extent_get_start(ext);
	index = extent_unit_index(coord);

	ret = get_reiser4_inode_by_tap(&inode, tap);
	if (ret)
		return ret;

	for (nr_marked = 0, pos_in_extent = 0;
	     nr_marked < max_nr_marked && pos_in_extent < width; pos_in_extent ++)
	{
		jnode * node;

		node = get_jnode_by_mapping(inode, index + pos_in_extent);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			break;
		}

		/* Freshly created jnode has no block number set. */
		if (node->blocknr == 0) {
			reiser4_block_nr block;
			block = start + pos_in_extent;
			jnode_set_block(node, &block);

			node->parent_item_id = EXTENT_POINTER_ID;
		}

		if (!JF_ISSET(node, JNODE_REPACK)) {
			do {
				/* Check whether the node is already read. */
				if (!JF_ISSET(node, JNODE_PARSED)) {
					ret = jstartio(node);
					if (ret)
						break;
				}
				ret = mark_jnode_for_repacking(node);
				if (ret)
					break;
				nr_marked ++;
			} while (0);
		}
		jput(node);
		if (ret)
			break;
	}

	iput(inode);
	if (ret)
		return ret;
	return nr_marked;
}

/* Check should the repacker relocate this node. */
static int relocatable (jnode * check)
{
	return !JF_ISSET(check, JNODE_OVRWR) && !JF_ISSET(check, JNODE_RELOC);
}

static int replace_end_of_extent (coord_t * coord, reiser4_block_nr end_part_start,
				  reiser4_block_nr end_part_width, int * all_replaced)
{
	reiser4_extent * ext;
	reiser4_block_nr ext_start;
	reiser4_block_nr ext_width;

	reiser4_item_data item;
	reiser4_extent new_ext, replace_ext;
	reiser4_block_nr replace_ext_width;
	reiser4_key key;

	int ret;

	assert ("zam-959", item_is_extent(coord));

	ext = extent_by_coord(coord);
	ext_start = extent_get_start(ext);
	ext_width = extent_get_width(ext);

	assert ("zam-960", end_part_width <= ext_width);

	replace_ext_width = ext_width - end_part_width;
	if (replace_ext_width == 0) {
		set_extent(ext, end_part_start, end_part_width);
		znode_make_dirty(coord->node);
		/* End part of extent is equal to the whole extent. */
		* all_replaced = 1;
		return 0;
	}

	set_extent(&replace_ext, ext_start, replace_ext_width);
	set_extent(&new_ext, end_part_start, end_part_width);

	unit_key_by_coord(coord, &key);
	set_key_offset(&key, get_key_offset(&key) + replace_ext_width * current_blocksize);

	{
		reiser4_context * ctx = get_current_context();
		reiser4_super_info_data * sinfo = get_super_private(ctx->super);
		__u64 estimated;
		__u64 were_grabbed;

		were_grabbed = ctx->grabbed_blocks;
		estimated = estimate_one_insert_item(&get_super_private(ctx->super)->tree);

		/* grab space for operations on internal levels. */
		ret = reiser4_grab_space(
			estimated, BA_FORCE | BA_RESERVED | BA_PERMANENT | BA_FORMATTED);
		if (ret)
			return ret;

		ret =  replace_extent(
			coord, znode_lh(coord->node), &key,
			init_new_extent(&item, &new_ext, 1), &replace_ext,
			COPI_DONT_SHIFT_LEFT, 0);

		/* release grabbed space if it was not used. */
		assert ("zam-988", ctx->grabbed_blocks >= were_grabbed);
		grabbed2free(ctx, sinfo, ctx->grabbed_blocks - were_grabbed);
	}

	return ret;
}

static int make_new_extent_at_end (coord_t * coord, reiser4_block_nr width, int * all_replaced)
{
	reiser4_extent * ext;
	reiser4_block_nr ext_start;
	reiser4_block_nr ext_width;
	reiser4_block_nr new_ext_start;

	assert ("zam-961", item_is_extent(coord));

	ext = extent_by_coord(coord);
	ext_start = extent_get_start(ext);
	ext_width = extent_get_width(ext);

	assert ("zam-962", width < ext_width);

	if (state_of_extent(ext) == ALLOCATED_EXTENT)
		new_ext_start = ext_start + ext_width - width;
	else
		new_ext_start = ext_start;

	return replace_end_of_extent(coord, new_ext_start, width, all_replaced);
}

static void parse_extent(coord_t * coord, reiser4_block_nr * start, reiser4_block_nr * width, long * ind)
{
	reiser4_extent * ext;

	ext   = extent_by_coord(coord);
	*start = extent_get_start(ext);
	*width = extent_get_width(ext);
	*ind   = extent_unit_index(coord);
}

static int skip_not_relocatable_extent(struct inode * inode, coord_t * coord, int * done)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_start;
	jnode * check = NULL;
	int ret = 0;

	assert("zam-985", state_of_extent(extent_by_coord(coord)));
	parse_extent(coord, &ext_start, &ext_width, &ext_index);

	for (reloc_start = ext_width - 1; reloc_start >= 0; reloc_start --) {
		check = get_jnode_by_mapping(inode, reloc_start + ext_index);
		if (IS_ERR(check))
			return PTR_ERR(check);

		if (check->blocknr == 0) {
			reiser4_block_nr block;
			block = ext_start + reloc_start;
			jnode_set_block(check, &block);

			check->parent_item_id = EXTENT_POINTER_ID;
		}

		if (relocatable(check)) {
			jput(check);
			if (reloc_start < ext_width - 1)
				ret = make_new_extent_at_end(coord, ext_width - reloc_start - 1, done);
			return ret;
		}
		jput(check);
	}
	*done = 1;
	return 0;
}


static int relocate_extent (struct inode * inode, coord_t * coord, reiser4_blocknr_hint * hint,
			    int *done, reiser4_block_nr * len)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_ind;
	reiser4_block_nr new_ext_width, new_ext_start, new_block;
	int unallocated_flg;
	int ret = 0;

	parse_extent(coord, &ext_start, &ext_width, &ext_index);
	assert("zam-974", *len != 0);

	unallocated_flg = (state_of_extent(extent_by_coord(coord)) == UNALLOCATED_EXTENT);
	hint->block_stage = unallocated_flg ? BLOCK_UNALLOCATED : BLOCK_FLUSH_RESERVED;

	new_ext_width = *len;
	ret = reiser4_alloc_blocks(hint, &new_ext_start, &new_ext_width, BA_PERMANENT);
	if (ret)
		return ret;

	hint->blk = new_ext_start;
	if (!unallocated_flg) {
		reiser4_block_nr dealloc_ext_start;

		dealloc_ext_start = ext_start + ext_width - new_ext_width;
		ret = reiser4_dealloc_blocks(&dealloc_ext_start, &new_ext_width, 0,
					     BA_DEFER | BA_PERMANENT);
		if (ret)
			return ret;
	}

	new_block = new_ext_start;
	for (reloc_ind = ext_width - new_ext_width; reloc_ind < ext_width; reloc_ind ++)
	{
		jnode * check;

		check = get_jnode_by_mapping(inode, ext_index + reloc_ind);
		if (IS_ERR(check))
			return PTR_ERR(check);

		assert("zam-975", relocatable(check));
		assert("zam-986", check->blocknr != 0);

		jnode_set_block(check, &new_block);
		check->parent_item_id = EXTENT_POINTER_ID;
		new_block ++;

		JF_SET(check, JNODE_RELOC);
		JF_SET(check, JNODE_REPACK);

		jput(check);
	}

	ret = replace_end_of_extent(coord, new_ext_start, new_ext_width, done);
	*len = new_ext_width;
	return ret;
}

static int find_relocatable_extent (struct inode * inode, coord_t * coord,
				    int * nr_reserved, reiser4_block_nr * len)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_end;
	jnode * check = NULL;
	int ret = 0;

	*len = 0;
	parse_extent(coord, &ext_start, &ext_width, &ext_index);

	for (reloc_end = ext_width - 1;
	     reloc_end >= 0 && *nr_reserved > 0; reloc_end --)
	{
		assert("zam-980", get_current_context()->grabbed_blocks >= *nr_reserved);

		check = get_jnode_by_mapping(inode, reloc_end + ext_index);
		if (IS_ERR(check))
			return PTR_ERR(check);

		if (check->blocknr == 0) {
			reiser4_block_nr block;
			block = ext_start + reloc_end;
			jnode_set_block(check, &block);
		}

		if (!relocatable(check)) {
			assert("zam-973", reloc_end < ext_width - 1);
			goto out;
		}
		/* add node to transaction. */
		ret = mark_jnode_for_repacking(check);
		if (ret)
			goto out;		;
		jput(check);

		(*len) ++;
		(*nr_reserved) --;
	}
	if (0) {
	out:
		jput(check);
	}
	return ret;
}

static int find_and_relocate_end_of_extent (
	struct inode * inode, coord_t * coord,
	struct repacker_cursor * cursor, int * done)
{
	reiser4_block_nr len;
	int ret;

	ret = skip_not_relocatable_extent(inode, coord, done);
	if (ret || (*done))
		return ret;

	ret = find_relocatable_extent(inode, coord, &cursor->count, &len);
	if (ret)
		return ret;
	if (len == 0) {
		*done = 1;
		return 0;
	}

	ret = relocate_extent(inode, coord, &cursor->hint, done, &len);
	if (ret)
		return ret;
	cursor->stats.jnodes_dirtied += (long)len;
	return 0;
}

/* process (relocate) unformatted nodes in backward direction: from the end of extent to the its start.  */
reiser4_internal int
process_extent_backward_for_repacking (tap_t * tap, struct repacker_cursor * cursor)
{
	coord_t * coord = tap->coord;
	reiser4_extent *ext;
	struct inode * inode = NULL;
	int done = 0;
	int ret;

	assert("zam-985", cursor->count > 0);
	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == HOLE_EXTENT)
		return 0;

	ret = get_reiser4_inode_by_tap(&inode, tap);

	while (!ret && !done)
		ret = find_and_relocate_end_of_extent(inode, coord, cursor, &done);

	iput(inode);
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
