/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../../carry.h"
#include "../../vfs_ops.h"

#include <linux/quotaops.h>
#include <asm/uaccess.h>
#include <linux/swap.h>
#include <linux/writeback.h>

/* plugin->u.item.b.max_key_inside */
reiser4_internal reiser4_key *
max_key_inside_tail(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(max_key()));
	return key;
}

/* plugin->u.item.b.can_contain_key */
reiser4_internal int
can_contain_key_tail(const coord_t *coord, const reiser4_key *key, const reiser4_item_data *data)
{
	reiser4_key item_key;

	if (item_plugin_by_coord(coord) != data->iplug)
		return 0;

	item_key_by_coord(coord, &item_key);
	if (get_key_locality(key) != get_key_locality(&item_key) ||
	    get_key_objectid(key) != get_key_objectid(&item_key)) return 0;

	return 1;
}

/* plugin->u.item.b.mergeable
   first item is of tail type */
/* Audited by: green(2002.06.14) */
reiser4_internal int
mergeable_tail(const coord_t *p1, const coord_t *p2)
{
	reiser4_key key1, key2;

	assert("vs-535", item_type_by_coord(p1) == UNIX_FILE_METADATA_ITEM_TYPE);
	assert("vs-365", item_id_by_coord(p1) == FORMATTING_ID);

	if (item_id_by_coord(p2) != FORMATTING_ID) {
		/* second item is of another type */
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) || get_key_type(&key1) != get_key_type(&key2)) {
		/* items of different objects */
		return 0;
	}
	if (get_key_offset(&key1) + nr_units_tail(p1) != get_key_offset(&key2)) {
		/* not adjacent items */
		return 0;
	}
	return 1;
}

reiser4_internal void show_tail(struct seq_file *m, coord_t *coord)
{
	seq_printf(m, "length: %i", item_length_by_coord(coord));
}

/* plugin->u.item.b.print
   plugin->u.item.b.check */

/* plugin->u.item.b.nr_units */
reiser4_internal pos_in_node_t
nr_units_tail(const coord_t *coord)
{
	return item_length_by_coord(coord);
}

/* plugin->u.item.b.lookup */
reiser4_internal lookup_result
lookup_tail(const reiser4_key *key, lookup_bias bias, coord_t *coord)
{
	reiser4_key item_key;
	__u64 lookuped, offset;
	unsigned nr_units;

	item_key_by_coord(coord, &item_key);
	offset = get_key_offset(item_key_by_coord(coord, &item_key));
	nr_units = nr_units_tail(coord);

	/* key we are looking for must be greater than key of item @coord */
	assert("vs-416", keygt(key, &item_key));

	/* offset we are looking for */
	lookuped = get_key_offset(key);

	if (lookuped >= offset && lookuped < offset + nr_units) {
		/* byte we are looking for is in this item */
		coord->unit_pos = lookuped - offset;
		coord->between = AT_UNIT;
		return CBK_COORD_FOUND;
	}

	/* set coord after last unit */
	coord->unit_pos = nr_units - 1;
	coord->between = AFTER_UNIT;
	return bias == FIND_MAX_NOT_MORE_THAN ? CBK_COORD_FOUND : CBK_COORD_NOTFOUND;
}

/* plugin->u.item.b.paste */
reiser4_internal int
paste_tail(coord_t *coord, reiser4_item_data *data, carry_plugin_info *info UNUSED_ARG)
{
	unsigned old_item_length;
	char *item;

	/* length the item had before resizing has been performed */
	old_item_length = item_length_by_coord(coord) - data->length;

	/* tail items never get pasted in the middle */
	assert("vs-363",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	       (coord->unit_pos == old_item_length - 1 &&
		coord->between == AFTER_UNIT) ||
	       (coord->unit_pos == 0 && old_item_length == 0 && coord->between == AT_UNIT));

	item = item_body_by_coord(coord);
	if (coord->unit_pos == 0)
		/* make space for pasted data when pasting at the beginning of
		   the item */
		xmemmove(item + data->length, item, old_item_length);

	if (coord->between == AFTER_UNIT)
		coord->unit_pos++;

	if (data->data) {
		assert("vs-554", data->user == 0 || data->user == 1);
		if (data->user) {
			assert("nikita-3035", schedulable());
			/* AUDIT: return result is not checked! */
			/* copy from user space */
			__copy_from_user(item + coord->unit_pos, data->data, (unsigned) data->length);
		} else
			/* copy from kernel space */
			xmemcpy(item + coord->unit_pos, data->data, (unsigned) data->length);
	} else {
		xmemset(item + coord->unit_pos, 0, (unsigned) data->length);
	}
	return 0;
}

/* plugin->u.item.b.fast_paste */

/* plugin->u.item.b.can_shift
   number of units is returned via return value, number of bytes via @size. For
   tail items they coincide */
reiser4_internal int
can_shift_tail(unsigned free_space, coord_t *source UNUSED_ARG,
	       znode *target UNUSED_ARG, shift_direction direction UNUSED_ARG, unsigned *size, unsigned want)
{
	/* make sure that that we do not want to shift more than we have */
	assert("vs-364", want > 0 && want <= (unsigned) item_length_by_coord(source));

	*size = min(want, free_space);
	return *size;
}

/* plugin->u.item.b.copy_units */
reiser4_internal void
copy_units_tail(coord_t *target, coord_t *source,
		unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space UNUSED_ARG)
{
	/* make sure that item @target is expanded already */
	assert("vs-366", (unsigned) item_length_by_coord(target) >= count);
	assert("vs-370", free_space >= count);

	if (where_is_free_space == SHIFT_LEFT) {
		/* append item @target with @count first bytes of @source */
		assert("vs-365", from == 0);

		xmemcpy((char *) item_body_by_coord(target) +
			item_length_by_coord(target) - count, (char *) item_body_by_coord(source), count);
	} else {
		/* target item is moved to right already */
		reiser4_key key;

		assert("vs-367", (unsigned) item_length_by_coord(source) == from + count);

		xmemcpy((char *) item_body_by_coord(target), (char *) item_body_by_coord(source) + from, count);

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		item_key_by_coord(source, &key);
		set_key_offset(&key, get_key_offset(&key) + from);

		node_plugin_by_node(target->node)->update_item_key(target, &key, 0 /*info */);
	}
}

/* plugin->u.item.b.create_hook */


/* item_plugin->b.kill_hook
   this is called when @count units starting from @from-th one are going to be removed
   */
reiser4_internal int
kill_hook_tail(const coord_t *coord, pos_in_node_t from,
	       pos_in_node_t count, struct carry_kill_data *kdata)
{
	reiser4_key key;
	loff_t start, end;

	assert("vs-1577", kdata);
	assert("vs-1579", kdata->inode);

	item_key_by_coord(coord, &key);
	start = get_key_offset(&key) + from;
	end = start + count;
	fake_kill_hook_tail(kdata->inode, start, end);
	return 0;
}

/* plugin->u.item.b.shift_hook */

/* helper for kill_units_tail and cut_units_tail */
static int
do_cut_or_kill(coord_t *coord, pos_in_node_t from, pos_in_node_t to,
	       reiser4_key *smallest_removed, reiser4_key *new_first)
{
	pos_in_node_t count;

	/* this method is only called to remove part of item */
	assert("vs-374", (to - from + 1) < item_length_by_coord(coord));
	/* tails items are never cut from the middle of an item */
	assert("vs-396", ergo(from != 0, to == coord_last_unit_pos(coord)));
	assert("vs-1558", ergo(from == 0, to < coord_last_unit_pos(coord)));

	count = to - from + 1;

	if (smallest_removed) {
		/* store smallest key removed */
		item_key_by_coord(coord, smallest_removed);
		set_key_offset(smallest_removed, get_key_offset(smallest_removed) + from);
	}
	if (new_first) {
		/* head of item is cut */
		assert("vs-1529", from == 0);

		item_key_by_coord(coord, new_first);
		set_key_offset(new_first, get_key_offset(new_first) + from + count);
	}

	if (REISER4_DEBUG)
		xmemset((char *) item_body_by_coord(coord) + from, 0, count);
	return count;
}

/* plugin->u.item.b.cut_units */
reiser4_internal int
cut_units_tail(coord_t *coord, pos_in_node_t from, pos_in_node_t to,
	       struct carry_cut_data *cdata UNUSED_ARG, reiser4_key *smallest_removed, reiser4_key *new_first)
{
	return do_cut_or_kill(coord, from, to, smallest_removed, new_first);
}

/* plugin->u.item.b.kill_units */
reiser4_internal int
kill_units_tail(coord_t *coord, pos_in_node_t from, pos_in_node_t to,
		struct carry_kill_data *kdata, reiser4_key *smallest_removed, reiser4_key *new_first)
{
	kill_hook_tail(coord, from, to - from + 1, kdata);
	return do_cut_or_kill(coord, from, to, smallest_removed, new_first);
}

/* plugin->u.item.b.unit_key */
reiser4_internal reiser4_key *
unit_key_tail(const coord_t *coord, reiser4_key *key)
{
	assert("vs-375", coord_is_existing_unit(coord));

	item_key_by_coord(coord, key);
	set_key_offset(key, (get_key_offset(key) + coord->unit_pos));

	return key;
}

/* plugin->u.item.b.estimate
   plugin->u.item.b.item_data_by_flow */

/* overwrite tail item or its part by use data */
static int
overwrite_tail(coord_t *coord, flow_t *f)
{
	unsigned count;

	assert("vs-570", f->user == 1);
	assert("vs-946", f->data);
	assert("vs-947", coord_is_existing_unit(coord));
	assert("vs-948", znode_is_write_locked(coord->node));
	assert("nikita-3036", schedulable());

	count = item_length_by_coord(coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	if (__copy_from_user((char *) item_body_by_coord(coord) + coord->unit_pos, f->data, count))
		return RETERR(-EFAULT);

	znode_make_dirty(coord->node);

	move_flow_forward(f, count);
	return 0;
}

/* tail redpage function. It is called from readpage_tail(). */
reiser4_internal int do_readpage_tail(uf_coord_t *uf_coord, struct page *page) {
	tap_t tap;
	int result;
	coord_t coord;
	lock_handle lh;

	int count, mapped;
	struct inode *inode;

	/* saving passed coord in order to do not move it by tap. */
	init_lh(&lh);
	copy_lh(&lh, uf_coord->lh);
	inode = page->mapping->host;
	coord_dup(&coord, &uf_coord->base_coord);

	tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

	if ((result = tap_load(&tap)))
		goto out_tap_done;

	/* lookup until page is filled up. */
	for (mapped = 0; mapped < PAGE_CACHE_SIZE; mapped += count) {
		void *pagedata;

		/* number of bytes to be copied to page. */
		count = item_length_by_coord(&coord) - coord.unit_pos;

		if (count > PAGE_CACHE_SIZE - mapped)
			count = PAGE_CACHE_SIZE - mapped;

		/* attaching @page to address space and getting data address. */
		pagedata = kmap_atomic(page, KM_USER0);

		/* copying tail body to page. */
		xmemcpy((char *)(pagedata + mapped),
			((char *)item_body_by_coord(&coord) + coord.unit_pos), count);

		flush_dcache_page(page);

		/* dettaching page from address space. */
		kunmap_atomic(page, KM_USER0);

		/* Getting next tail item. */
		if (mapped + count < PAGE_CACHE_SIZE) {

			/* unlocking page in order to avoid keep it locked durring tree lookup,
			   which takes long term locks. */
			unlock_page(page);

			/* getting right neighbour. */
			result = go_dir_el(&tap, RIGHT_SIDE, 0);

			/* lock page back */
			lock_page(page);

			/* page is uptodate due to another thread made it up to date. Getting
			   out of here. */
			if (PageUptodate(page)) {
				result = 0;
				goto out_unlock_page;
			}

			if (result) {
				/* check if there is no neighbour node. */
				if (result == -E_NO_NEIGHBOR) {
					result = 0;
					goto out_update_page;
				} else {
					goto out_tap_relse;
				}
			} else {
				/* check if found coord is not owned by file. */
				if (!inode_file_plugin(inode)->owns_item(inode, &coord)) {
					result = 0;
					goto out_update_page;
				}
			}
		}
	}

	/* making page up to date and releasing it. */
	SetPageUptodate(page);
	unlock_page(page);

	/* releasing tap */
	tap_relse(&tap);
	tap_done(&tap);

	return 0;

 out_update_page:
	SetPageUptodate(page);
 out_unlock_page:
	unlock_page(page);
 out_tap_relse:
	tap_relse(&tap);
 out_tap_done:
	tap_done(&tap);
	return result;
}

/*
   plugin->s.file.readpage
   reiser4_read->unix_file_read->page_cache_readahead->reiser4_readpage->unix_file_readpage->readpage_tail
   or
   filemap_nopage->reiser4_readpage->readpage_unix_file->->readpage_tail

   At the beginning: coord->node is read locked, zloaded, page is locked, coord is set to existing unit inside of tail
   item. */
reiser4_internal int
readpage_tail(void *vp, struct page *page)
{
	uf_coord_t *uf_coord = vp;
	ON_DEBUG(coord_t *coord = &uf_coord->base_coord);
	ON_DEBUG(reiser4_key key);

	assert("umka-2515", PageLocked(page));
	assert("umka-2516", !PageUptodate(page));
	assert("umka-2517", !jprivate(page) && !PagePrivate(page));
	assert("umka-2518", page->mapping && page->mapping->host);

	assert("umka-2519", znode_is_loaded(coord->node));
	assert("umka-2520", item_is_tail(coord));
	assert("umka-2521", coord_is_existing_unit(coord));
	assert("umka-2522", znode_is_rlocked(coord->node));
	assert("umka-2523", page->mapping->host->i_ino == get_key_objectid(item_key_by_coord(coord, &key)));

	return do_readpage_tail(uf_coord, page);
}

reiser4_internal int
item_balance_dirty_pages(struct address_space *mapping, const flow_t *f,
			 hint_t *hint, int back_to_dirty, int do_set_hint)
{
	int result;
	struct inode *inode;

	if (do_set_hint) {
		if (hint->coord.valid)
			set_hint(hint, &f->key, ZNODE_WRITE_LOCK);
		else
			unset_hint(hint);
		longterm_unlock_znode(hint->coord.lh);
	}

	inode = mapping->host;
	if (get_key_offset(&f->key) > inode->i_size) {
		assert("vs-1649", f->user == 1);
		INODE_SET_FIELD(inode, i_size, get_key_offset(&f->key));
	}
	if (f->user != 0) {
		/* this was writing data from user space. Update timestamps, therefore. Othrewise, this is tail
		   conversion where we should not update timestamps */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		result = reiser4_update_sd(inode);
		if (result)
			return result;
	}

	/* FIXME-VS: this is temporary: the problem is that bdp takes inodes
	   from sb's dirty list and it looks like nobody puts there inodes of
	   files which are built of tails */
	if (back_to_dirty)
		move_inode_out_from_sync_inodes_loop(mapping);

	reiser4_throttle_write(inode);
	return hint_validate(hint, &f->key, 0/* do not check key */, ZNODE_WRITE_LOCK);
}

/* drop longterm znode lock before calling balance_dirty_pages. balance_dirty_pages may cause transaction to close,
   therefore we have to update stat data if necessary */
static int formatting_balance_dirty_pages(struct address_space *mapping, const flow_t *f,
				    hint_t *hint)
{
	return item_balance_dirty_pages(mapping, f, hint, 1, 1/* set hint */);
}

/* calculate number of blocks which can be dirtied/added when flow is inserted and stat data gets updated and grab them.
   FIXME-VS: we may want to call grab_space with BA_CAN_COMMIT flag but that would require all that complexity with
   sealing coord, releasing long term lock and validating seal later */
static int
insert_flow_reserve(reiser4_tree *tree)
{
	grab_space_enable();
	return reiser4_grab_space(estimate_insert_flow(tree->height) + estimate_one_insert_into_item(tree), 0);
}

/* one block gets overwritten and stat data may get updated */
static int
overwrite_reserve(reiser4_tree *tree)
{
	grab_space_enable();
	return reiser4_grab_space(1 + estimate_one_insert_into_item(tree), 0);
}

/* plugin->u.item.s.file.write
   access to data stored in tails goes directly through formatted nodes */
reiser4_internal int
write_tail(struct inode *inode, flow_t *f, hint_t *hint,
	   int grabbed, /* tail's write may be called from plain unix file write and from tail conversion. In first
			   case (grabbed == 0) space is not reserved forehand, so, it must be done here. When it is
			   being called from tail conversion - space is reserved already for whole operation which may
			   involve several calls to item write. In this case space reservation will not be done here */
	   write_mode_t mode)
{
	int result;
	coord_t *coord;

	assert("vs-1338", hint->coord.valid == 1);

	coord = &hint->coord.base_coord;
	result = 0;
	while (f->length && hint->coord.valid == 1) {
		switch (mode) {
		case FIRST_ITEM:
		case APPEND_ITEM:
			/* check quota before appending data */
			if (DQUOT_ALLOC_SPACE_NODIRTY(inode, f->length)) {
				result = RETERR(-EDQUOT);
				break;
			}

			if (!grabbed)
				result = insert_flow_reserve(znode_get_tree(coord->node));
			if (!result)
				result = insert_flow(coord, hint->coord.lh, f);
			if (f->length)
				DQUOT_FREE_SPACE_NODIRTY(inode, f->length);
			break;

		case OVERWRITE_ITEM:
			if (!grabbed)
				result = overwrite_reserve(znode_get_tree(coord->node));
			if (!result)
				result = overwrite_tail(coord, f);
			break;

		default:
			impossible("vs-1031", "does this ever happen?");
			result = RETERR(-EIO);
			break;

		}

		if (result) {
			if (!grabbed)
				all_grabbed2free();
			break;
		}

		/* FIXME: do not rely on a coord yet */
		hint->coord.valid = 0;

		/* throttle the writer */
		result = formatting_balance_dirty_pages(inode->i_mapping, f, hint);
		if (!grabbed)
			all_grabbed2free();
		if (result) {
			// reiser4_stat_tail_add(bdp_caused_repeats);
			break;
		}
	}

	return result;
}

#if REISER4_DEBUG

static int
coord_matches_key_tail(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key item_key;

	assert("vs-1356", coord_is_existing_unit(coord));
	assert("vs-1354", keylt(key, append_key_tail(coord, &item_key)));
	assert("vs-1355", keyge(key, item_key_by_coord(coord, &item_key)));
	return get_key_offset(key) == get_key_offset(&item_key) + coord->unit_pos;

}

#endif

/* plugin->u.item.s.file.read */
reiser4_internal int
read_tail(struct file *file UNUSED_ARG, flow_t *f, hint_t *hint)
{
	unsigned count;
	int item_length;
	coord_t *coord;
	uf_coord_t *uf_coord;

	uf_coord = &hint->coord;
	coord = &uf_coord->base_coord;

	assert("vs-571", f->user == 1);
	assert("vs-571", f->data);
	assert("vs-967", coord && coord->node);
	assert("vs-1117", znode_is_rlocked(coord->node));
	assert("vs-1118", znode_is_loaded(coord->node));

	assert("nikita-3037", schedulable());
	assert("vs-1357", coord_matches_key_tail(coord, &f->key));

	/* calculate number of bytes to read off the item */
	item_length = item_length_by_coord(coord);
	count = item_length_by_coord(coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;


	/* FIXME: unlock long term lock ! */

	if (__copy_to_user(f->data, ((char *) item_body_by_coord(coord) + coord->unit_pos), count))
		return RETERR(-EFAULT);

	/* probably mark_page_accessed() should only be called if
	 * coord->unit_pos is zero. */
	mark_page_accessed(znode_page(coord->node));
	move_flow_forward(f, count);

	coord->unit_pos += count;
	if (item_length == coord->unit_pos) {
		coord->unit_pos --;
		coord->between = AFTER_UNIT;
	}

	return 0;
}

/*
   plugin->u.item.s.file.append_key
   key of first byte which is the next to last byte by addressed by this item
*/
reiser4_internal reiser4_key *
append_key_tail(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(key) + item_length_by_coord(coord));
	return key;
}

/* plugin->u.item.s.file.init_coord_extension */
reiser4_internal void
init_coord_extension_tail(uf_coord_t *uf_coord, loff_t lookuped)
{
	uf_coord->valid = 1;
}

/*
  plugin->u.item.s.file.get_block
*/
reiser4_internal int
get_block_address_tail(const coord_t *coord, sector_t block, struct buffer_head *bh)
{
	assert("nikita-3252",
	       znode_get_level(coord->node) == LEAF_LEVEL);

	bh->b_blocknr = *znode_get_block(coord->node);
	return 0;
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
