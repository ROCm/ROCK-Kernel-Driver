/* COPYRIGHT 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../../flush.h" /* just for jnode_tostring */
#include "../object.h"

#include <linux/quotaops.h>

static inline reiser4_extent *
ext_by_offset(const znode *node, int offset)
{
	reiser4_extent *ext;

	ext = (reiser4_extent *)(zdata(node) + offset);
	return ext;
}

static inline reiser4_extent *
ext_by_ext_coord(const uf_coord_t *uf_coord)
{
	reiser4_extent *ext;

	ext = ext_by_offset(uf_coord->base_coord.node, uf_coord->extension.extent.ext_offset);
	assert("vs-1650", extent_get_start(ext) == extent_get_start(&uf_coord->extension.extent.extent));
	assert("vs-1651", extent_get_width(ext) == extent_get_width(&uf_coord->extension.extent.extent));
	return ext;
}

#if REISER4_DEBUG
static int
coord_extension_is_ok(const uf_coord_t *uf_coord)
{
	const coord_t *coord;
	const extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	return WITH_DATA(coord->node, (uf_coord->valid == 1 &&
				       coord_is_iplug_set(coord) &&
				       item_is_extent(coord) &&
				       ext_coord->nr_units == nr_units_extent(coord) &&
				       ext == extent_by_coord(coord) &&
				       ext_coord->width == extent_get_width(ext) &&
				       coord->unit_pos < ext_coord->nr_units &&
				       ext_coord->pos_in_unit < ext_coord->width &&
				       extent_get_start(ext) == extent_get_start(&ext_coord->extent) &&
				       extent_get_width(ext) == extent_get_width(&ext_coord->extent)));
}

#endif

/* @coord is set either to the end of last extent item of a file
   (coord->node is a node on the twig level) or to a place where first
   item of file has to be inserted to (coord->node is leaf
   node). Calculate size of hole to be inserted. If that hole is too
   big - only part of it is inserted */
static int
add_hole(coord_t *coord, lock_handle *lh, const reiser4_key *key /* key of position in a file for write */)
{
	int result;
	znode *loaded;
	reiser4_extent *ext, new_ext;
	reiser4_block_nr hole_width;
	reiser4_item_data item;
	reiser4_key hole_key;

	/*coord_clear_iplug(coord);*/
	result = zload(coord->node);
	if (result)
		return result;
	loaded = coord->node;

	if (znode_get_level(coord->node) == LEAF_LEVEL) {
		/* there are no items of this file yet. First item will be
		   hole extent inserted here */

		/* @coord must be set for inserting of new item */
		assert("vs-711", coord_is_between_items(coord));

		hole_key = *key;
		set_key_offset(&hole_key, 0ull);

		hole_width = ((get_key_offset(key) + current_blocksize - 1) >>
			      current_blocksize_bits);
		assert("vs-710", hole_width > 0);

		/* compose body of hole extent */
		set_extent(&new_ext, HOLE_EXTENT_START, hole_width);

		result = insert_extent_by_coord(coord, init_new_extent(&item, &new_ext, 1), &hole_key, lh);
		zrelse(loaded);
		return result;
	}

	/* last item of file may have to be appended with hole */
	assert("vs-708", znode_get_level(coord->node) == TWIG_LEVEL);
	assert("vs-714", item_id_by_coord(coord) == EXTENT_POINTER_ID);

	/* make sure we are at proper item */
	assert("vs-918", keylt(key, max_key_inside_extent(coord, &hole_key)));

	/* key of first byte which is not addressed by this extent */
	append_key_extent(coord, &hole_key);

	if (keyle(key, &hole_key)) {
		/* there is already extent unit which contains position
		   specified by @key */
		zrelse(loaded);
		return 0;
	}

	/* extent item has to be appended with hole. Calculate length of that
	   hole */
	hole_width = ((get_key_offset(key) - get_key_offset(&hole_key) +
		       current_blocksize - 1) >> current_blocksize_bits);
	assert("vs-954", hole_width > 0);

	/* set coord after last unit */
	coord_init_after_item_end(coord);

	/* get last extent in the item */
	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == HOLE_EXTENT) {
		/* last extent of a file is hole extent. Widen that extent by
		   @hole_width blocks. Note that we do not worry about
		   overflowing - extent width is 64 bits */
		set_extent(ext, HOLE_EXTENT_START, extent_get_width(ext) + hole_width);
		znode_make_dirty(coord->node);
		zrelse(loaded);
		return 0;
	}

	/* append item with hole extent unit */
	assert("vs-713", (state_of_extent(ext) == ALLOCATED_EXTENT || state_of_extent(ext) == UNALLOCATED_EXTENT));

	/* compose body of hole extent */
	set_extent(&new_ext, HOLE_EXTENT_START, hole_width);

	result = insert_into_item(coord, lh, &hole_key, init_new_extent(&item, &new_ext, 1), 0 /*flags */ );
	zrelse(loaded);
	return result;
}

/* insert extent item (containing one unallocated extent of width 1) to place
   set by @coord */
static int
insert_first_block(uf_coord_t *uf_coord, const reiser4_key *key, reiser4_block_nr *block)
{
	int result;
	reiser4_extent ext;
	reiser4_item_data unit;

	/* make sure that we really write to first block */
	assert("vs-240", get_key_offset(key) == 0);

	/* extent insertion starts at leaf level */
	assert("vs-719", znode_get_level(uf_coord->base_coord.node) == LEAF_LEVEL);

	set_extent(&ext, UNALLOCATED_EXTENT_START, 1);
	result = insert_extent_by_coord(&uf_coord->base_coord, init_new_extent(&unit, &ext, 1), key, uf_coord->lh);
	if (result) {
		/* FIXME-VITALY: this is grabbed at file_write time. */
		/* grabbed2free ((__u64)1); */
		return result;
	}

	*block = fake_blocknr_unformatted();

	/* invalidate coordinate, research must be performed to continue because write will continue on twig level */
	uf_coord->valid = 0;
	return 0;
}

/* @coord is set to the end of extent item. Append it with pointer to one block - either by expanding last unallocated
   extent or by appending a new one of width 1 */
static int
append_one_block(uf_coord_t *uf_coord, reiser4_key *key, reiser4_block_nr *block)
{
	int result;
	reiser4_extent new_ext;
	reiser4_item_data unit;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	/* check correctness of position in the item */
	assert("vs-228", coord->unit_pos == coord_last_unit_pos(coord));
	assert("vs-1311", coord->between == AFTER_UNIT);
	assert("vs-1302", ext_coord->pos_in_unit == ext_coord->width - 1);
	assert("vs-883",
	       ( {
		       reiser4_key next;
		       keyeq(key, append_key_extent(coord, &next));
	       }));

	switch (state_of_extent(ext)) {
	case UNALLOCATED_EXTENT:
		set_extent(ext, UNALLOCATED_EXTENT_START, extent_get_width(ext) + 1);
		znode_make_dirty(coord->node);

		/* update coord extension */
		ext_coord->width ++;
		ON_DEBUG(extent_set_width(&uf_coord->extension.extent.extent, ext_coord->width));
		break;

	case HOLE_EXTENT:
	case ALLOCATED_EXTENT:
		/* append one unallocated extent of width 1 */
		set_extent(&new_ext, UNALLOCATED_EXTENT_START, 1);
		result = insert_into_item(coord, uf_coord->lh, key, init_new_extent(&unit, &new_ext, 1), 0 /* flags */ );
		/* FIXME: for now */
		uf_coord->valid = 0;
		if (result)
			return result;
		break;
	default:
		assert("", 0);
	}

	*block = fake_blocknr_unformatted();
	return 0;
}

/* @coord is set to hole unit inside of extent item, replace hole unit with an
   unit for unallocated extent of the width 1, and perhaps a hole unit before
   the unallocated unit and perhaps a hole unit after the unallocated unit. */
static int
plug_hole(uf_coord_t *uf_coord, reiser4_key *key)
{
	reiser4_extent *ext, new_exts[2],	/* extents which will be added after original
						 * hole one */
	 replace;		/* extent original hole extent will be replaced
				 * with */
	reiser4_block_nr width, pos_in_unit;
	reiser4_item_data item;
	int count;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	reiser4_key tmp_key;
	int return_inserted_position;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	width = ext_coord->width;
	pos_in_unit = ext_coord->pos_in_unit;

	if (width == 1) {
		set_extent(ext, UNALLOCATED_EXTENT_START, 1);
		znode_make_dirty(coord->node);
		return 0;
	} else if (pos_in_unit == 0) {
		if (coord->unit_pos) {
			if (state_of_extent(ext - 1) == UNALLOCATED_EXTENT) {
				extent_set_width(ext - 1, extent_get_width(ext - 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos --;
				ext_coord->width = extent_get_width(ext - 1);
				ext_coord->pos_in_unit = ext_coord->width - 1;
				ext_coord->ext_offset -= sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent = *extent_by_coord(coord));
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&replace, UNALLOCATED_EXTENT_START, 1);
		/* extent to be inserted */
		set_extent(&new_exts[0], HOLE_EXTENT_START, width - 1);

		/* have replace_extent to return with @coord and @uf_coord->lh set to unit which was replaced */
		return_inserted_position = 0;
		count = 1;
	} else if (pos_in_unit == width - 1) {
		if (coord->unit_pos < nr_units_extent(coord) - 1) {
			if (state_of_extent(ext + 1) == UNALLOCATED_EXTENT) {
				extent_set_width(ext + 1, extent_get_width(ext + 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos ++;
				ext_coord->width = extent_get_width(ext + 1);
				ext_coord->pos_in_unit = 0;
				ext_coord->ext_offset += sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent = *extent_by_coord(coord));
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&replace, HOLE_EXTENT_START, width - 1);
		/* extent to be inserted */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, 1);

		/* have replace_extent to return with @coord and @uf_coord->lh set to unit which was inserted */
		return_inserted_position = 1;
		count = 1;
	} else {
		/* extent for replace */
		set_extent(&replace, HOLE_EXTENT_START, pos_in_unit);
		/* extents to be inserted */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, 1);
		set_extent(&new_exts[1], HOLE_EXTENT_START, width - pos_in_unit - 1);

		/* have replace_extent to return with @coord and @uf_coord->lh set to first of units which were
		   inserted */
		return_inserted_position = 1;
		count = 2;
	}

	/* insert_into_item will insert new units after the one @coord is set
	   to. So, update key correspondingly */
	unit_key_by_coord(coord, &tmp_key);
	set_key_offset(&tmp_key, (get_key_offset(&tmp_key) + extent_get_width(&replace) * current_blocksize));

	uf_coord->valid = 0;
	return replace_extent(coord, uf_coord->lh, &tmp_key, init_new_extent(&item, new_exts, count), &replace, 0 /* flags */, return_inserted_position);
}

/* make unallocated node pointer in the position @uf_coord is set to */
static int
overwrite_one_block(uf_coord_t *uf_coord, reiser4_key *key, reiser4_block_nr *block, int *created,
		    struct inode *inode)
{
	int result;
	extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;
	oid_t oid;
	pgoff_t index;

	oid = get_key_objectid(key);
	index = get_key_offset(key) >> current_blocksize_bits;

	assert("vs-1312", uf_coord->base_coord.between == AT_UNIT);

	result = 0;
	*created = 0;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start(ext) + ext_coord->pos_in_unit;
		break;

	case HOLE_EXTENT:
		if (inode != NULL && DQUOT_ALLOC_BLOCK(inode, 1))
			return RETERR(-EDQUOT);
		result = plug_hole(uf_coord, key);
		if (!result) {
			*block = fake_blocknr_unformatted();
			*created = 1;
		} else {
			if (inode != NULL)
				DQUOT_FREE_BLOCK(inode, 1);
		}
		break;

	case UNALLOCATED_EXTENT:
		break;

	default:
		impossible("vs-238", "extent of unknown type found");
		result = RETERR(-EIO);
		break;
	}

	return result;
}

#if REISER4_DEBUG

/* after make extent uf_coord's lock handle must be set to node containing unit which was inserted/found */
static void
check_make_extent_result(int result, write_mode_t mode, const reiser4_key *key,
			 const lock_handle *lh, reiser4_block_nr block)
{
	coord_t coord;

	if (result != 0)
		return;

	assert("vs-960", znode_is_write_locked(lh->node));
	zload(lh->node);
	result = lh->node->nplug->lookup(lh->node, key, FIND_EXACT, &coord);
	assert("vs-1502", result == NS_FOUND);
	assert("vs-1656", coord_is_existing_unit(&coord));

	if (blocknr_is_fake(&block)) {
		assert("vs-1657", state_of_extent(extent_by_coord(&coord)) == UNALLOCATED_EXTENT);
	} else if (block == 0) {
		assert("vs-1660", mode == OVERWRITE_ITEM);
		assert("vs-1657", state_of_extent(extent_by_coord(&coord)) == UNALLOCATED_EXTENT);
	} else {
		reiser4_key tmp;
		reiser4_block_nr pos_in_unit;

		assert("vs-1658", state_of_extent(extent_by_coord(&coord)) == ALLOCATED_EXTENT);
		unit_key_by_coord(&coord, &tmp);
		pos_in_unit = (get_key_offset(key) - get_key_offset(&tmp)) >> current_blocksize_bits;
		assert("vs-1659", block == extent_get_start(extent_by_coord(&coord)) + pos_in_unit);
	}
	zrelse(lh->node);
}

#endif

/* when @inode is not NULL, alloc quota before updating extent item */
static int
make_extent(reiser4_key *key, uf_coord_t *uf_coord, write_mode_t mode,
	    reiser4_block_nr *block, int *created, struct inode *inode)
{
	int result;
	oid_t oid;
	pgoff_t index;

	oid = get_key_objectid(key);
	index = get_key_offset(key) >> current_blocksize_bits;

	assert("vs-960", znode_is_write_locked(uf_coord->base_coord.node));
	assert("vs-1334", znode_is_loaded(uf_coord->base_coord.node));

	DISABLE_NODE_CHECK;

	*block = 0;
	switch (mode) {
	case FIRST_ITEM:
		/* new block will be inserted into file. Check quota */
		if (inode != NULL && DQUOT_ALLOC_BLOCK(inode, 1))
			return RETERR(-EDQUOT);

		/* create first item of the file */
		result = insert_first_block(uf_coord, key, block);
		if (result && inode != NULL)
			DQUOT_FREE_BLOCK(inode, 1);
		*created = 1;
		break;

	case APPEND_ITEM:
		/* new block will be inserted into file. Check quota */
		if (inode != NULL && DQUOT_ALLOC_BLOCK(inode, 1))
			return RETERR(-EDQUOT);

		/* FIXME: item plugin should be initialized
		   item_plugin_by_coord(&uf_coord->base_coord);*/
		assert("vs-1316", coord_extension_is_ok(uf_coord));
		result = append_one_block(uf_coord, key, block);
		if (result && inode != NULL)
			DQUOT_FREE_BLOCK(inode, 1);
		*created = 1;
		break;

	case OVERWRITE_ITEM:
		/* FIXME: item plugin should be initialized
		   item_plugin_by_coord(&uf_coord->base_coord);*/
		assert("vs-1316", coord_extension_is_ok(uf_coord));
		result = overwrite_one_block(uf_coord, key, block, created, inode);
		break;

	default:
		assert("vs-1346", 0);
		result = RETERR(-E_REPEAT);
		break;
	}

	ENABLE_NODE_CHECK;

	ON_DEBUG(check_make_extent_result(result, mode, key, uf_coord->lh, *block));

	return result;
}

/* drop longterm znode lock before calling balance_dirty_pages. balance_dirty_pages may cause transaction to close,
   therefore we have to update stat data if necessary */
static int
extent_balance_dirty_pages(struct address_space *mapping, const flow_t *f,
			   hint_t *hint)
{
	return item_balance_dirty_pages(mapping, f, hint, 0, 0/* do not set hint */);
}

/* estimate and reserve space which may be required for writing one page of file */
static int
reserve_extent_write_iteration(struct inode *inode, reiser4_tree *tree)
{
	int result;

	grab_space_enable();
	/* one unformatted node and one insertion into tree and one stat data update may be involved */
	result = reiser4_grab_space(1 + /* Hans removed reservation for balancing here. */
				    /* if extent items will be ever used by plugins other than unix file plugin - estimate update should instead be taken by
				       inode_file_plugin(inode)->estimate.update(inode)
				    */
				    estimate_update_common(inode),
				    0/* flags */);
	return result;
}

static void
write_move_coord(coord_t *coord, uf_coord_t *uf_coord, write_mode_t mode, int full_page)
{
	extent_coord_extension_t *ext_coord;

	assert("vs-1339", ergo(mode == OVERWRITE_ITEM, coord->between == AT_UNIT));
	assert("vs-1341", ergo(mode == FIRST_ITEM, uf_coord->valid == 0));

	if (uf_coord->valid == 0)
		return;

	ext_coord = &uf_coord->extension.extent;

	if (mode == APPEND_ITEM) {
		assert("vs-1340", coord->between == AFTER_UNIT);
		assert("vs-1342", coord->unit_pos == ext_coord->nr_units - 1);
		assert("vs-1343", ext_coord->pos_in_unit == ext_coord->width - 2);
		assert("vs-1344", state_of_extent(ext_by_ext_coord(uf_coord)) == UNALLOCATED_EXTENT);
		ON_DEBUG(ext_coord->extent = *ext_by_ext_coord(uf_coord));
		ext_coord->pos_in_unit ++;
		if (!full_page)
			coord->between = AT_UNIT;
		return;
	}

	assert("vs-1345", coord->between == AT_UNIT);

	if (!full_page)
		return;
	if (ext_coord->pos_in_unit == ext_coord->width - 1) {
		/* last position in the unit */
		if (coord->unit_pos == ext_coord->nr_units - 1) {
			/* last unit in the item */
			uf_coord->valid = 0;
		} else {
			/* move to the next unit */
			coord->unit_pos ++;
			ext_coord->ext_offset += sizeof(reiser4_extent);
			ON_DEBUG(ext_coord->extent = *ext_by_offset(coord->node, ext_coord->ext_offset));
			ext_coord->width = extent_get_width(ext_by_offset(coord->node, ext_coord->ext_offset));
			ext_coord->pos_in_unit = 0;
		}
	} else
		ext_coord->pos_in_unit ++;
}

static void
set_hint_unlock_node(hint_t *hint, flow_t *f, znode_lock_mode mode)
{
	if (hint->coord.valid) {
		set_hint(hint, &f->key, mode);
	} else {
		unset_hint(hint);
	}
	longterm_unlock_znode(hint->coord.lh);
}

static int
write_is_partial(struct inode *inode, loff_t file_off, unsigned page_off, unsigned count)
{
	if (count == inode->i_sb->s_blocksize)
		return 0;
	if (page_off == 0 && file_off + count >= inode->i_size)
		return 0;
	return 1;
}

/* this initialize content of page not covered by write */
static void
zero_around(struct page *page, int from, int count)
{
	char *data;

	data = kmap_atomic(page, KM_USER0);
	memset(data, 0, from);
	memset(data + from + count, 0, PAGE_CACHE_SIZE - from - count);
	flush_dcache_page(page);
	kunmap_atomic(data, KM_USER0);
}

/* write flow's data into file by pages */
static int
extent_write_flow(struct inode *inode, flow_t *flow, hint_t *hint,
		  int grabbed, /* 0 if space for operation is not reserved yet, 1 - otherwise */
		  write_mode_t mode)
{
	int result;
	loff_t file_off;
	unsigned long page_nr;
	unsigned long page_off, count;
	struct page *page;
	jnode *j;
	uf_coord_t *uf_coord;
	coord_t *coord;
	oid_t oid;
	reiser4_tree *tree;
	reiser4_key page_key;
	reiser4_block_nr blocknr;
	int created;


	assert("nikita-3139", !inode_get_flag(inode, REISER4_NO_SD));
	assert("vs-885", current_blocksize == PAGE_CACHE_SIZE);
	assert("vs-700", flow->user == 1);
	assert("vs-1352", flow->length > 0);

	tree = tree_by_inode(inode);
	oid = get_inode_oid(inode);
	uf_coord = &hint->coord;
	coord = &uf_coord->base_coord;

	/* position in a file to start write from */
	file_off = get_key_offset(&flow->key);
	/* index of page containing that offset */
	page_nr = (unsigned long)(file_off >> PAGE_CACHE_SHIFT);
	/* offset within the page */
	page_off = (unsigned long)(file_off & (PAGE_CACHE_SIZE - 1));

	/* key of first byte of page */
	page_key = flow->key;
	set_key_offset(&page_key, (loff_t)page_nr << PAGE_CACHE_SHIFT);
	do {
		if (!grabbed) {
			result = reserve_extent_write_iteration(inode, tree);
			if (result)
				break;
		}
		/* number of bytes to be written to page */
		count = PAGE_CACHE_SIZE - page_off;
		if (count > flow->length)
			count = flow->length;

		write_page_log(inode->i_mapping, page_nr);

		result = make_extent(&page_key, uf_coord, mode, &blocknr, &created, inode/* check quota */);
		if (result) {
			goto exit1;
		}

		/* look for jnode and create it if it does not exist yet */
		j = find_get_jnode(tree, inode->i_mapping, oid, page_nr);
		if (IS_ERR(j)) {
			result = PTR_ERR(j);
			goto exit1;
		}
		LOCK_JNODE(j);
		if (created) {
			/* extent corresponding to this jnode was just created */
			assert("vs-1504", *jnode_get_block(j) == 0);
			JF_SET(j, JNODE_CREATED);
			/* new block is added to file. Update inode->i_blocks and inode->i_bytes. FIXME:
			   inode_set/get/add/sub_bytes is used to be called by quota macros */
			/*inode_add_bytes(inode, PAGE_CACHE_SIZE);*/
		}
		if (*jnode_get_block(j) == 0) {
			jnode_set_block(j, &blocknr);
		} else {
			assert("vs-1508", !blocknr_is_fake(&blocknr));
			assert("vs-1507", ergo(blocknr, *jnode_get_block(j) == blocknr));
		}
		UNLOCK_JNODE(j);

		/* get page looked and attached to jnode */
		page = jnode_get_page_locked(j, GFP_KERNEL);
		if (IS_ERR(page)) {
			result = PTR_ERR(page);
			goto exit2;
		}

		page_cache_get(page);

		if (!PageUptodate(page)) {
			if (mode == OVERWRITE_ITEM) {
				/* this page may be either an anonymous page (a page which was dirtied via mmap,
				   writepage-ed and for which extent pointer was just created. In this case jnode is
				   eflushed) or correspod to not page cached block (in which case created == 0). In
				   either case we have to read this page if it is being overwritten partially */
				if (write_is_partial(inode, file_off, page_off, count) &&
				    (created == 0 || JF_ISSET(j, JNODE_EFLUSH))) {
					result = page_io(page, j, READ, GFP_KERNEL);
					if (result)
						goto exit3;
					lock_page(page);
					if (!PageUptodate(page))
						goto exit3;
				} else {
					zero_around(page, page_off, count);
				}
			} else {
				/* new page added to the file. No need to carry about data it might contain. Zero
				   content of new page around write area */
				assert("vs-1681", !JF_ISSET(j, JNODE_EFLUSH));
				zero_around(page, page_off, count);
			}
		}

		UNDER_SPIN_VOID(jnode, j, eflush_del(j, 1));

		move_flow_forward(flow, count);
		write_move_coord(coord, uf_coord, mode, page_off + count == PAGE_CACHE_SIZE);
		set_hint_unlock_node(hint, flow, ZNODE_WRITE_LOCK);

		assert("vs-1503", UNDER_SPIN(jnode, j, (!JF_ISSET(j, JNODE_EFLUSH) && jnode_page(j) == page)));
		assert("nikita-3033", schedulable());
		if (!lock_stack_isclean(get_current_lock_stack()))
			print_clog();
		assert("nikita-2104", lock_stack_isclean(get_current_lock_stack()));

		/* copy user data into page */
		result = __copy_from_user((char *)kmap(page) + page_off, flow->data - count, count);
		kunmap(page);
		if (unlikely(result)) {
			/* FIXME: write(fd, 0, 10); to empty file will write no data but file will get increased
			   size. */
			result = RETERR(-EFAULT);
			goto exit3;
		}

		set_page_dirty_internal(page, 0);
		SetPageUptodate(page);
		if (!PageReferenced(page))
			SetPageReferenced(page);

		unlock_page(page);
		page_cache_release(page);

		/* FIXME: possible optimization: if jnode is not dirty yet - it gets into clean list in try_capture and
		   then in jnode_mark_dirty gets moved to dirty list. So, it would be more optimal to put jnode directly
		   to dirty list */
		LOCK_JNODE(j);
		result = try_capture(j, ZNODE_WRITE_LOCK, 0, 1/* can_coc */);
		if (result)
			goto exit2;
		jnode_make_dirty_locked(j);
		UNLOCK_JNODE(j);

		jput(j);

		/* throttle the writer */
		result = extent_balance_dirty_pages(inode->i_mapping, flow, hint);
		if (!grabbed)
			all_grabbed2free();
		if (result) {
			reiser4_stat_inc(extent.bdp_caused_repeats);
			break;
		}

		page_off = 0;
		page_nr ++;
		file_off += count;
		set_key_offset(&page_key, (loff_t)page_nr << PAGE_CACHE_SHIFT);

		continue;

	exit3:
		unlock_page(page);
		page_cache_release(page);
	exit2:
		if (created)
			inode_sub_bytes(inode, PAGE_CACHE_SIZE);
		jput(j);
	exit1:
		if (!grabbed)
			all_grabbed2free();
		break;

		/* hint is unset by make_page_extent when first extent of a
		   file was inserted: in that case we can not use coord anymore
		   because we are to continue on twig level but are still at
		   leaf level
		*/
	} while (flow->length && uf_coord->valid == 1);

	return result;
}

/* estimate and reserve space which may be required for appending file with hole stored in extent */
static int
extent_hole_reserve(reiser4_tree *tree)
{
	/* adding hole may require adding a hole unit into extent item and stat data update */
	grab_space_enable();
	return reiser4_grab_space(estimate_one_insert_into_item(tree) * 2, 0);
}

static int
extent_write_hole(struct inode *inode, flow_t *flow, hint_t *hint, int grabbed)
{
	int result;
	loff_t new_size;
	coord_t *coord;

	coord = &hint->coord.base_coord;
	if (!grabbed) {
		result = extent_hole_reserve(znode_get_tree(coord->node));
		if (result)
			return result;
	}

	new_size = get_key_offset(&flow->key) + flow->length;
	set_key_offset(&flow->key, new_size);
	flow->length = 0;
	result = add_hole(coord, hint->coord.lh, &flow->key);
	hint->coord.valid = 0;
	if (!result) {
		done_lh(hint->coord.lh);
		INODE_SET_FIELD(inode, i_size, new_size);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		result = reiser4_update_sd(inode);
	}
	if (!grabbed)
		all_grabbed2free();
	return result;
}

/*
  plugin->s.file.write
  It can be called in two modes:
  1. real write - to write data from flow to a file (@flow->data != 0)
  2. expanding truncate (@f->data == 0)
*/
reiser4_internal int
write_extent(struct inode *inode, flow_t *flow, hint_t *hint,
	     int grabbed, /* extent's write may be called from plain unix file write and from tail conversion. In first
			     case (grabbed == 0) space is not reserved forehand, so, it must be done here. When it is
			     being called from tail conversion - space is reserved already for whole operation which may
			     involve several calls to item write. In this case space reservation will not be done
			     here */
	     write_mode_t mode)
{
	if (flow->data)
		/* real write */
		return extent_write_flow(inode, flow, hint, grabbed, mode);

	/* expanding truncate. add_hole requires f->key to be set to new end of file */
	return extent_write_hole(inode, flow, hint, grabbed);
}

/* move coord one page forward. Return 1 if coord is moved out of item */
static int
read_move_coord(coord_t *coord, extent_coord_extension_t *ext_coord)
{
	if (ext_coord->pos_in_unit == ext_coord->width - 1) {
		/* last position in the unit */
		if (coord->unit_pos == ext_coord->nr_units - 1) {
			/* last unit in the item */
			return 1;
		} else {
			/* move to the next unit */
			coord->unit_pos ++;
			ext_coord->ext_offset += sizeof(reiser4_extent);
			ON_DEBUG(ext_coord->extent = *ext_by_offset(coord->node, ext_coord->ext_offset));
			ext_coord->width = extent_get_width(ext_by_offset(coord->node, ext_coord->ext_offset));
			ext_coord->pos_in_unit = 0;
		}
	} else
		ext_coord->pos_in_unit ++;
	return 0;
}

static void
call_page_cache_readahead(struct address_space *mapping, struct file *file, unsigned long page_nr,
			  const uf_coord_t *uf_coord)
{
	reiser4_file_fsdata *fsdata;
	uf_coord_t ra_coord;

	fsdata = reiser4_get_file_fsdata(file);
	ra_coord = *uf_coord;
	ra_coord.extension.extent.expected_page = page_nr;
	fsdata->reg.coord = &ra_coord;

	page_cache_readahead(mapping, &file->f_ra, file, page_nr);
	fsdata->reg.coord = 0;
}

#if REISER4_TRACE
static void
print_ext_coord(const char *s, uf_coord_t *uf_coord)
{
	reiser4_key key;
	extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;

	item_key_by_coord(&uf_coord->base_coord, &key);
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);
	printk("%s: item key [%llu, %llu], nr_units %d, cur extent [%llu, %llu], unit_pos %d, pos_in_unit %Lu\n",
	       s, get_key_objectid(&key), get_key_offset(&key),
	       ext_coord->nr_units,
	       extent_get_start(ext), extent_get_width(ext),
	       uf_coord->base_coord.unit_pos, ext_coord->pos_in_unit);
}
#endif

#if REISER4_DEBUG

/* return 1 if offset @off is inside of extent unit pointed to by @coord. Set pos_in_unit inside of unit
   correspondingly */
static int
offset_is_in_unit(const coord_t *coord, loff_t off)
{
	reiser4_key unit_key;
	__u64 unit_off;
	reiser4_extent *ext;

	ext = extent_by_coord(coord);

	unit_key_extent(coord, &unit_key);
	unit_off = get_key_offset(&unit_key);
	if (off < unit_off)
		return 0;
	if (off >= (unit_off + (current_blocksize * extent_get_width(ext))))
		return 0;
	return 1;
}

static int
coord_matches_key_extent(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key item_key;

	assert("vs-771", coord_is_existing_unit(coord));
	assert("vs-1258", keylt(key, append_key_extent(coord, &item_key)));
	assert("vs-1259", keyge(key, item_key_by_coord(coord, &item_key)));

	return offset_is_in_unit(coord, get_key_offset(key));
}

#endif /* REISER4_DEBUG */

/* Implements plugin->u.item.s.file.read operation for extent items. */
reiser4_internal int
read_extent(struct file *file, flow_t *flow,  hint_t *hint)
{
	int result;
	struct page *page;
	unsigned long page_nr;
	unsigned long page_off, count;
	struct inode *inode;
	__u64 file_off;
	uf_coord_t *uf_coord;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	uf_coord = &hint->coord;
	assert("vs-1318", coord_extension_is_ok(uf_coord));

	inode = file->f_dentry->d_inode;
	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;

	ON_TRACE(TRACE_EXTENTS, "read_extent start: ino %llu, size %llu, offset %llu, count %lld\n",
		 get_inode_oid(inode), inode->i_size, get_key_offset(&flow->key), flow->length);
	IF_TRACE(TRACE_EXTENTS, print_ext_coord("read_extent start", uf_coord));

	assert("vs-1353", current_blocksize == PAGE_CACHE_SIZE);
	assert("vs-572", flow->user == 1);
	assert("vs-1351", flow->length > 0);
	assert("vs-1119", znode_is_rlocked(coord->node));
	assert("vs-1120", znode_is_loaded(coord->node));
	assert("vs-1256", coord_matches_key_extent(coord, &flow->key));

	/* offset in a file to start read from */
	file_off = get_key_offset(&flow->key);
	/* index of page containing that offset */
	page_nr = (unsigned long)(file_off >> PAGE_CACHE_SHIFT);
	/* offset within the page */
	page_off = (unsigned long)(file_off & (PAGE_CACHE_SIZE - 1));

	count = PAGE_CACHE_SIZE - page_off;

	do {
		call_page_cache_readahead(inode->i_mapping, file, page_nr, uf_coord);

		/* this will return page if it exists and is uptodate, otherwise it will allocate page and call
		   extent_readpage to fill it */
		page = read_cache_page(inode->i_mapping, page_nr, readpage_extent, coord);
		if (IS_ERR(page))
			return PTR_ERR(page);

		/* number of bytes which can be read from the page */
		if (count > flow->length)
			count = flow->length;
		move_flow_forward(flow, count);
		if (page_off + count == PAGE_CACHE_SIZE)
			if (read_move_coord(coord, ext_coord))
				uf_coord->valid = 0;
		set_hint_unlock_node(hint, flow, ZNODE_READ_LOCK);

		wait_on_page_locked(page);
		if (!PageUptodate(page)) {
			page_detach_jnode(page, inode->i_mapping, page_nr);
			page_cache_release(page);
			warning("jmacd-97178", "extent_read: page is not up to date");
			return RETERR(-EIO);
		}

		/* If users can be writing to this page using arbitrary virtual addresses, take care about potential
		   aliasing before reading the page on the kernel side.
		*/
		if (mapping_writably_mapped(inode->i_mapping))
			flush_dcache_page(page);

		assert("nikita-3034", schedulable());


		/* AUDIT: We must page-in/prepare user area first to avoid deadlocks */
		result = __copy_to_user(flow->data - count, (char *)kmap(page) + page_off, count);
		kunmap(page);

		page_cache_release(page);
		if (unlikely(result))
			return RETERR(-EFAULT);

		result = hint_validate(hint, &flow->key, 0/* do not check key */, ZNODE_READ_LOCK);
		if (result)
			break;
		assert("vs-1318", coord_extension_is_ok(uf_coord));
		assert("vs-1263", coord_matches_key_extent(coord, &flow->key));
		page_off = 0;
		page_nr ++;
		count = PAGE_CACHE_SIZE;
	} while (flow->length && uf_coord->valid == 1);

	ON_TRACE(TRACE_EXTENTS, "read_extent done: left %lld\n", flow->length);
	IF_TRACE(TRACE_EXTENTS, print_ext_coord("read_extent done", uf_coord));

	return 0;
}

static int
move_coord_pages(coord_t *coord, extent_coord_extension_t *ext_coord, unsigned count)
{
	reiser4_extent *ext;

	ext_coord->expected_page += count;

	ext = ext_by_offset(coord->node, ext_coord->ext_offset);

	do {
		if (ext_coord->pos_in_unit + count < ext_coord->width) {
			ext_coord->pos_in_unit += count;
			break;
		}

		if (coord->unit_pos == ext_coord->nr_units - 1) {
			coord->between = AFTER_UNIT;
			return 1;
		}

		/* shift to next unit */
		count -= (ext_coord->width - ext_coord->pos_in_unit);
		coord->unit_pos ++;
		ext_coord->pos_in_unit = 0;
		ext_coord->ext_offset += sizeof(reiser4_extent);
		ext ++;
		ON_DEBUG(ext_coord->extent = *ext);
		ext_coord->width = extent_get_width(ext);
	} while (1);

	return 0;
}

static inline void
zero_page(struct page *page)
{
	char *kaddr = kmap_atomic(page, KM_USER0);

	xmemset(kaddr, 0, PAGE_CACHE_SIZE);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	SetPageUptodate(page);
	unlock_page(page);
}

static int
do_readpage_extent(reiser4_extent *ext, reiser4_block_nr pos, struct page *page)
{
	jnode *j;
	struct address_space *mapping;
	unsigned long index;
	oid_t oid;

	mapping = page->mapping;
	oid = get_inode_oid(mapping->host);
	index = page->index;

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		/*
		 * it is possible to have hole page with jnode, if page was
		 * eflushed previously.
		 */
		j = jfind(mapping, index);
		if (j == NULL) {
			zero_page(page);
			return 0;
		}
		LOCK_JNODE(j);
		if (!jnode_page(j)) {
			jnode_attach_page(j, page);
		} else {
			BUG_ON(jnode_page(j) != page);
			assert("vs-1504", jnode_page(j) == page);
		}

		UNLOCK_JNODE(j);
		break;

	case ALLOCATED_EXTENT:
		j = jnode_of_page(page);
		if (IS_ERR(j))
			return PTR_ERR(j);
		if (*jnode_get_block(j) == 0) {
			reiser4_block_nr blocknr;

			blocknr = extent_get_start(ext) + pos;
			jnode_set_block(j, &blocknr);
		} else
			assert("vs-1403", j->blocknr == extent_get_start(ext) + pos);
		break;

	case UNALLOCATED_EXTENT:
		j = jfind(mapping, index);
		assert("nikita-2688", j);
		assert("vs-1426", jnode_page(j) == NULL);

		UNDER_SPIN_VOID(jnode, j, jnode_attach_page(j, page));

		/* page is locked, it is safe to check JNODE_EFLUSH */
		assert("vs-1668", JF_ISSET(j, JNODE_EFLUSH));
		break;

	default:
		warning("vs-957", "extent_readpage: wrong extent\n");
		return RETERR(-EIO);
	}

	BUG_ON(j == 0);
	page_io(page, j, READ, GFP_NOIO);
	jput(j);
	return 0;
}

static int
readahead_readpage_extent(void *vp, struct page *page)
{
	int result;
	uf_coord_t *uf_coord;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	uf_coord = vp;
	coord = &uf_coord->base_coord;

	if (coord->between != AT_UNIT) {
		unlock_page(page);
		return RETERR(-EINVAL);
	}

	ext_coord = &uf_coord->extension.extent;
	if (ext_coord->expected_page != page->index) {
		/* read_cache_pages skipped few pages. Try to adjust coord to page */
		assert("vs-1269", page->index > ext_coord->expected_page);
		if (move_coord_pages(coord, ext_coord,  page->index - ext_coord->expected_page)) {
			/* extent pointing to this page is not here */
			unlock_page(page);
			return RETERR(-EINVAL);
		}

		assert("vs-1274", offset_is_in_unit(coord,
						    (loff_t)page->index << PAGE_CACHE_SHIFT));
		ext_coord->expected_page = page->index;
	}

	assert("vs-1281", page->index == ext_coord->expected_page);
	result = do_readpage_extent(ext_by_ext_coord(uf_coord), ext_coord->pos_in_unit, page);
	if (!result)
		move_coord_pages(coord, ext_coord, 1);
	return result;
}

/*
  plugin->u.item.s.file.readpages
*/
reiser4_internal void
readpages_extent(void *vp, struct address_space *mapping, struct list_head *pages)
{
	if (vp)
		read_cache_pages(mapping, pages, readahead_readpage_extent, vp);
}

/*
   plugin->s.file.readpage
   reiser4_read->unix_file_read->page_cache_readahead->reiser4_readpage->unix_file_readpage->extent_readpage
   or
   filemap_nopage->reiser4_readpage->readpage_unix_file->->readpage_extent

   At the beginning: coord->node is read locked, zloaded, page is
   locked, coord is set to existing unit inside of extent item (it is not necessary that coord matches to page->index)
*/
reiser4_internal int
readpage_extent(void *vp, struct page *page)
{
	uf_coord_t *uf_coord = vp;
	ON_DEBUG(coord_t *coord = &uf_coord->base_coord);
	ON_DEBUG(reiser4_key key);

	assert("vs-1040", PageLocked(page));
	assert("vs-1050", !PageUptodate(page));
	assert("vs-757", !jprivate(page) && !PagePrivate(page));
	assert("vs-1039", page->mapping && page->mapping->host);

	assert("vs-1044", znode_is_loaded(coord->node));
	assert("vs-758", item_is_extent(coord));
	assert("vs-1046", coord_is_existing_unit(coord));
	assert("vs-1045", znode_is_rlocked(coord->node));
	assert("vs-1047", page->mapping->host->i_ino == get_key_objectid(item_key_by_coord(coord, &key)));
	assert("vs-1320", coord_extension_is_ok(uf_coord));

	return do_readpage_extent(ext_by_ext_coord(uf_coord), uf_coord->extension.extent.pos_in_unit, page);
}

/*
  plugin->s.file.capture

  At the beginning: coord.node is write locked, zloaded, page is not locked, coord is set to existing unit inside of
  extent item
*/
reiser4_internal int
capture_extent(reiser4_key *key, uf_coord_t *uf_coord, struct page *page, write_mode_t mode)
{
	jnode *j;
	int result;
	reiser4_block_nr blocknr;
	int created;
	int check_quota;

	ON_TRACE(TRACE_EXTENTS, "WP: index %lu, count %d..", page->index, page_count(page));

	assert("vs-1051", page->mapping && page->mapping->host);
	assert("nikita-3139", !inode_get_flag(page->mapping->host, REISER4_NO_SD));
	assert("vs-864", znode_is_wlocked(uf_coord->base_coord.node));
	assert("vs-1398", get_key_objectid(key) == get_inode_oid(page->mapping->host));

	/* FIXME: assume for now that quota is only checked on write */
	check_quota = 0;
	result = make_extent(key, uf_coord, mode, &blocknr, &created, check_quota ? page->mapping->host : NULL);
	if (result) {
		done_lh(uf_coord->lh);
		return result;
	}

	lock_page(page);
	j = jnode_of_page(page);
	if (IS_ERR(j)) {
		unlock_page(page);
		done_lh(uf_coord->lh);
		return PTR_ERR(j);
	}
	set_page_dirty_internal(page, 0);
	unlock_page(page);

	LOCK_JNODE(j);
	if (created) {
		/* extent corresponding to this jnode was just created */
		assert("vs-1504", *jnode_get_block(j) == 0);
		JF_SET(j, JNODE_CREATED);
		/* new block is added to file. Update inode->i_blocks and inode->i_bytes. FIXME:
		   inode_set/get/add/sub_bytes is used to be called by quota macros */
		inode_add_bytes(page->mapping->host, PAGE_CACHE_SIZE);
	}

	if (*jnode_get_block(j) == 0)
		jnode_set_block(j, &blocknr);
	else {
		assert("vs-1508", !blocknr_is_fake(&blocknr));
		assert("vs-1507", ergo(blocknr, *jnode_get_block(j) == blocknr));
	}
	UNLOCK_JNODE(j);

	done_lh(uf_coord->lh);

	LOCK_JNODE(j);
	result = try_capture(j, ZNODE_WRITE_LOCK, 0, 1/* can_coc */);
	if (result != 0)
		reiser4_panic("nikita-3324", "Cannot capture jnode: %i", result);
	jnode_make_dirty_locked(j);
	UNLOCK_JNODE(j);
	jput(j);

	if (created)
		reiser4_update_sd(page->mapping->host);
		/* warning about failure of this is issued already */

	ON_TRACE(TRACE_EXTENTS, "OK\n");
	return 0;
}

/*
  plugin->u.item.s.file.get_block
*/
reiser4_internal int
get_block_address_extent(const coord_t *coord, sector_t block, struct buffer_head *bh)
{
	reiser4_extent *ext;

	assert("vs-1321", coord_is_existing_unit(coord));

	ext = extent_by_coord(coord);

	if (state_of_extent(ext) != ALLOCATED_EXTENT)
		/* FIXME: bad things may happen if it is unallocated extent */
		bh->b_blocknr = 0;
	else {
		reiser4_key key;

		unit_key_by_coord(coord, &key);
		assert("vs-1645", block >= get_key_offset(&key) >> current_blocksize_bits);
		assert("vs-1646", block < (get_key_offset(&key) >> current_blocksize_bits) + extent_get_width(ext));
		bh->b_blocknr = extent_get_start(ext) + (block - (get_key_offset(&key) >> current_blocksize_bits));
	}
	return 0;
}

/*
  plugin->u.item.s.file.append_key
  key of first byte which is the next to last byte by addressed by this extent
*/
reiser4_internal reiser4_key *
append_key_extent(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(key) + extent_size(coord, nr_units_extent(coord)));

	assert("vs-610", get_key_offset(key) && (get_key_offset(key) & (current_blocksize - 1)) == 0);
	return key;
}

/* plugin->u.item.s.file.init_coord_extension */
reiser4_internal void
init_coord_extension_extent(uf_coord_t *uf_coord, loff_t lookuped)
{
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	reiser4_key key;
	loff_t offset;

	assert("vs-1295", uf_coord->valid == 0);

	coord = &uf_coord->base_coord;
	assert("vs-1288", coord_is_iplug_set(coord));
	assert("vs-1327", znode_is_loaded(coord->node));

	if (coord->between != AFTER_UNIT && coord->between != AT_UNIT)
		return;

	ext_coord = &uf_coord->extension.extent;
	ext_coord->nr_units = nr_units_extent(coord);
	ext_coord->ext_offset = (char *)extent_by_coord(coord) - zdata(coord->node);
	ext_coord->width = extent_get_width(extent_by_coord(coord));
	ON_DEBUG(ext_coord->extent = *extent_by_coord(coord));
	uf_coord->valid = 1;

	/* pos_in_unit is the only uninitialized field in extended coord */
	if (coord->between == AFTER_UNIT) {
		assert("vs-1330", coord->unit_pos == nr_units_extent(coord) - 1);

		ext_coord->pos_in_unit = ext_coord->width - 1;
	} else {
		/* AT_UNIT */
		unit_key_by_coord(coord, &key);
		offset = get_key_offset(&key);

		assert("vs-1328", offset <= lookuped);
		assert("vs-1329", lookuped < offset + ext_coord->width * current_blocksize);
		ext_coord->pos_in_unit = ((lookuped - offset) >> current_blocksize_bits);
	}
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
