/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../key.h"
#include "../../super.h"
#include "../../carry.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../../emergency_flush.h"
#include "../../prof.h"
#include "../../flush.h"
#include "../object.h"


/* prepare structure reiser4_item_data. It is used to put one extent unit into tree */
/* Audited by: green(2002.06.13) */
reiser4_internal reiser4_item_data *
init_new_extent(reiser4_item_data *data, void *ext_unit, int nr_extents)
{
	if (REISER4_ZERO_NEW_NODE)
		memset(data, 0, sizeof(reiser4_item_data));

	data->data = ext_unit;
	/* data->data is kernel space */
	data->user = 0;
	data->length = sizeof(reiser4_extent) * nr_extents;
	data->arg = 0;
	data->iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	return data;
}

/* how many bytes are addressed by @nr first extents of the extent item */
reiser4_internal reiser4_block_nr
extent_size(const coord_t *coord, pos_in_node_t nr)
{
	pos_in_node_t i;
	reiser4_block_nr blocks;
	reiser4_extent *ext;

	ext = item_body_by_coord(coord);
	assert("vs-263", nr <= nr_units_extent(coord));

	blocks = 0;
	for (i = 0; i < nr; i++, ext++) {
		blocks += extent_get_width(ext);
	}

	return blocks * current_blocksize;
}

reiser4_internal extent_state
state_of_extent(reiser4_extent *ext)
{
	switch ((int) extent_get_start(ext)) {
	case 0:
		return HOLE_EXTENT;
	case 1:
		return UNALLOCATED_EXTENT;
	default:
		break;
	}
	return ALLOCATED_EXTENT;
}

reiser4_internal int
extent_is_unallocated(const coord_t *item)
{
	assert("jmacd-5133", item_is_extent(item));

	return state_of_extent(extent_by_coord(item)) == UNALLOCATED_EXTENT;
}

reiser4_internal int
extent_is_allocated(const coord_t *item)
{
	assert("jmacd-5133", item_is_extent(item));

	return state_of_extent(extent_by_coord(item)) == ALLOCATED_EXTENT;
}

/* set extent's start and width */
reiser4_internal void
set_extent(reiser4_extent *ext, reiser4_block_nr start, reiser4_block_nr width)
{
	extent_set_start(ext, start);
	extent_set_width(ext, width);
}

/* used in split_allocated_extent, conv_extent, plug_hole to insert 1 or 2 extent units (@exts_to_add) after the one
   @un_extent is set to. @un_extent itself is changed to @replace */
reiser4_internal int
replace_extent(coord_t *un_extent, lock_handle *lh,
	       reiser4_key *key, reiser4_item_data *exts_to_add, const reiser4_extent *replace, unsigned flags UNUSED_ARG,
	       int return_inserted_position /* if it is 1 - un_extent and lh are returned set to first of newly inserted
					       units, if it is 0 - un_extent and lh are returned set to unit which was
					       replaced */)
{
	int result;
	coord_t coord_after;
	lock_handle lh_after;
	tap_t watch;
	znode *orig_znode;
	ON_DEBUG(reiser4_extent orig_ext);	/* this is for debugging */

	assert("vs-990", coord_is_existing_unit(un_extent));
	assert("vs-1375", znode_is_write_locked(un_extent->node));
	assert("vs-1426", extent_get_width(replace) != 0);
	assert("vs-1427", extent_get_width((reiser4_extent *)exts_to_add->data) != 0);

	coord_dup(&coord_after, un_extent);
	init_lh(&lh_after);
	copy_lh(&lh_after, lh);
	tap_init(&watch, &coord_after, &lh_after, ZNODE_WRITE_LOCK);
	tap_monitor(&watch);

	ON_DEBUG(orig_ext = *extent_by_coord(un_extent));
	orig_znode = un_extent->node;

	/* make sure that key is set properly */
	if (REISER4_DEBUG) {
		reiser4_key tmp;

		unit_key_by_coord(un_extent, &tmp);
		set_key_offset(&tmp, get_key_offset(&tmp) + extent_get_width(replace) * current_blocksize);
		assert("vs-1080", keyeq(&tmp, key));
	}

	DISABLE_NODE_CHECK;

	/* set insert point after unit to be replaced */
	un_extent->between = AFTER_UNIT;

	result = insert_into_item(un_extent,
				  return_inserted_position ? lh : 0,
				  /*(flags == COPI_DONT_SHIFT_LEFT) ? 0 : lh,*/ key, exts_to_add, flags);
	if (!result) {
		/* now we have to replace the unit after which new units were inserted. Its position is tracked by
		   @watch */
		reiser4_extent *ext;

		if (coord_after.node != orig_znode) {
			coord_clear_iplug(&coord_after);
			result = zload(coord_after.node);
		}

		if (likely(!result)) {
			ext = extent_by_coord(&coord_after);

			assert("vs-987", znode_is_loaded(coord_after.node));
			assert("vs-988", !memcmp(ext, &orig_ext, sizeof (*ext)));

			*ext = *replace;
			znode_make_dirty(coord_after.node);

			if (coord_after.node != orig_znode)
				zrelse(coord_after.node);

			if (return_inserted_position == 0) {
				/* return un_extent and lh set to the same */
				assert("vs-1662", WITH_DATA(coord_after.node, !memcmp(replace, extent_by_coord(&coord_after), sizeof(reiser4_extent))));

				*un_extent = coord_after;
				done_lh(lh);
				copy_lh(lh, &lh_after);
			} else {
				/* return un_extent and lh set to first of inserted units */
				assert("vs-1663", WITH_DATA(un_extent->node, !memcmp(exts_to_add->data, extent_by_coord(un_extent), sizeof(reiser4_extent))));
				assert("vs-1664", lh->node == un_extent->node);
			}
		}
	}
	tap_done(&watch);

	ENABLE_NODE_CHECK;
	return result;
}

reiser4_internal lock_handle *
znode_lh(znode *node)
{
	assert("vs-1371", znode_is_write_locked(node));
	assert("vs-1372", znode_is_wlocked_once(node));
	return owners_list_front(&node->lock.owners);
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
