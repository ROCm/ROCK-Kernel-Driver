/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../tree.h"
#include "../../jnode.h"
#include "../../super.h"
#include "../../flush.h"
#include "../../carry.h"
#include "../object.h"

#include <linux/pagemap.h>

/* Return either first or last extent (depending on @side) of the item
   @coord is set to. Set @pos_in_unit either to first or to last block
   of extent. */
static reiser4_extent *
extent_utmost_ext(const coord_t *coord, sideof side, reiser4_block_nr *pos_in_unit)
{
	reiser4_extent *ext;

	if (side == LEFT_SIDE) {
		/* get first extent of item */
		ext = extent_item(coord);
		*pos_in_unit = 0;
	} else {
		/* get last extent of item and last position within it */
		assert("vs-363", side == RIGHT_SIDE);
		ext = extent_item(coord) + coord_last_unit_pos(coord);
		*pos_in_unit = extent_get_width(ext) - 1;
	}

	return ext;
}

/* item_plugin->f.utmost_child */
/* Return the child. Coord is set to extent item. Find jnode corresponding
   either to first or to last unformatted node pointed by the item */
reiser4_internal int
utmost_child_extent(const coord_t *coord, sideof side, jnode **childp)
{
	reiser4_extent *ext;
	reiser4_block_nr pos_in_unit;

	ext = extent_utmost_ext(coord, side, &pos_in_unit);

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		*childp = NULL;
		return 0;
	case ALLOCATED_EXTENT:
	case UNALLOCATED_EXTENT:
		break;
	default:
		/* this should never happen */
		assert("vs-1417", 0);
	}

	{
		reiser4_key key;
		reiser4_tree *tree;
		unsigned long index;

		if (side == LEFT_SIDE) {
			/* get key of first byte addressed by the extent */
			item_key_by_coord(coord, &key);
		} else {
			/* get key of byte which next after last byte addressed by the extent */
			append_key_extent(coord, &key);
		}

		assert("vs-544", (get_key_offset(&key) >> PAGE_CACHE_SHIFT) < ~0ul);
		/* index of first or last (depending on @side) page addressed
		   by the extent */
		index = (unsigned long) (get_key_offset(&key) >> PAGE_CACHE_SHIFT);
		if (side == RIGHT_SIDE)
			index --;

		tree = coord->node->zjnode.tree;
		*childp = jlookup(tree, get_key_objectid(&key), index);
	}

	return 0;
}

/* item_plugin->f.utmost_child_real_block */
/* Return the child's block, if allocated. */
reiser4_internal int
utmost_child_real_block_extent(const coord_t *coord, sideof side, reiser4_block_nr *block)
{
	reiser4_extent *ext;

	ext = extent_by_coord(coord);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start(ext);
		if (side == RIGHT_SIDE)
			*block += extent_get_width(ext) - 1;
		break;
	case HOLE_EXTENT:
	case UNALLOCATED_EXTENT:
		*block = 0;
		break;
	default:
		/* this should never happen */
		assert("vs-1418", 0);
	}

	return 0;
}

/* item_plugin->f.scan */
/* Performs leftward scanning starting from an unformatted node and its parent coordinate.
   This scan continues, advancing the parent coordinate, until either it encounters a
   formatted child or it finishes scanning this node.

   If unallocated, the entire extent must be dirty and in the same atom.  (Actually, I'm
   not sure this is last property (same atom) is enforced, but it should be the case since
   one atom must write the parent and the others must read the parent, thus fusing?).  In
   any case, the code below asserts this case for unallocated extents.  Unallocated
   extents are thus optimized because we can skip to the endpoint when scanning.

   It returns control to scan_extent, handles these terminating conditions, e.g., by
   loading the next twig.
*/
reiser4_internal int scan_extent(flush_scan * scan)
{
	coord_t coord;
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist;
	reiser4_block_nr unit_start;
	__u64 oid;
	reiser4_key key;
	int ret = 0, allocated, incr;
	reiser4_tree *tree;

	if (!jnode_check_dirty(scan->node)) {
		scan->stop = 1;
		return 0; /* Race with truncate, this node is already
			   * truncated. */
	}

	coord_dup(&coord, &scan->parent_coord);

	assert("jmacd-1404", !scan_finished(scan));
	assert("jmacd-1405", jnode_get_level(scan->node) == LEAF_LEVEL);
	assert("jmacd-1406", jnode_is_unformatted(scan->node));

	/* The scan_index variable corresponds to the current page index of the
	   unformatted block scan position. */
	scan_index = index_jnode(scan->node);

	assert("jmacd-7889", item_is_extent(&coord));

	ON_TRACE(TRACE_FLUSH_VERB, "%s scan starts %lu: %s\n",
		 (scanning_left(scan) ? "left" : "right"), scan_index, jnode_tostring(scan->node));

repeat:
	/* objectid of file */
	oid = get_key_objectid(item_key_by_coord(&coord, &key));

	ON_TRACE(TRACE_FLUSH_VERB, "%s scan index %lu: parent %p oid %llu\n",
		 (scanning_left(scan) ? "left" : "right"), scan_index, coord.node, oid);

	allocated = !extent_is_unallocated(&coord);
	/* Get the values of this extent unit: */
	unit_index = extent_unit_index(&coord);
	unit_width = extent_unit_width(&coord);
	unit_start = extent_unit_start(&coord);

	assert("jmacd-7187", unit_width > 0);
	assert("jmacd-7188", scan_index >= unit_index);
	assert("jmacd-7189", scan_index <= unit_index + unit_width - 1);

	/* Depending on the scan direction, we set different maximum values for scan_index
	   (scan_max) and the number of nodes that would be passed if the scan goes the
	   entire way (scan_dist).  Incr is an integer reflecting the incremental
	   direction of scan_index. */
	if (scanning_left(scan)) {
		scan_max = unit_index;
		scan_dist = scan_index - unit_index;
		incr = -1;
	} else {
		scan_max = unit_index + unit_width - 1;
		scan_dist = scan_max - unit_index;
		incr = +1;
	}

	tree = coord.node->zjnode.tree;

	/* If the extent is allocated we have to check each of its blocks.  If the extent
	   is unallocated we can skip to the scan_max. */
	if (allocated) {
		do {
			neighbor = jlookup(tree, oid, scan_index);
			if (neighbor == NULL)
				goto stop_same_parent;

			ON_TRACE(TRACE_FLUSH_VERB, "alloc scan index %lu: %s\n",
				 scan_index, jnode_tostring(neighbor));

			if (scan->node != neighbor && !scan_goto(scan, neighbor)) {
				/* @neighbor was jput() by scan_goto(). */
				goto stop_same_parent;
			}

			ret = scan_set_current(scan, neighbor, 1, &coord);
			if (ret != 0) {
				goto exit;
			}

			/* reference to @neighbor is stored in @scan, no need
			   to jput(). */
			scan_index += incr;

		} while (incr + scan_max != scan_index);

	} else {
		/* Optimized case for unallocated extents, skip to the end. */
		neighbor = jlookup(tree, oid, scan_max/*index*/);
		if (neighbor == NULL) {
			/* Race with truncate */
			scan->stop = 1;
			ret = 0;
			goto exit;
		}

		assert ("zam-1043", blocknr_is_fake(jnode_get_block(neighbor)));

		ON_TRACE(TRACE_FLUSH_VERB, "unalloc scan index %lu: %s\n", scan_index, jnode_tostring(neighbor));

		/* XXX commented assertion out, because it is inherently
		 * racy */
		/* assert("jmacd-3551", !jnode_check_flushprepped(neighbor)
		   && same_slum_check(neighbor, scan->node, 0, 0)); */

		ret = scan_set_current(scan, neighbor, scan_dist, &coord);
		if (ret != 0) {
			goto exit;
		}
	}

	if (coord_sideof_unit(&coord, scan->direction) == 0 && item_is_extent(&coord)) {
		/* Continue as long as there are more extent units. */

		scan_index =
		    extent_unit_index(&coord) + (scanning_left(scan) ? extent_unit_width(&coord) - 1 : 0);
		goto repeat;
	}

	if (0) {
stop_same_parent:

		/* If we are scanning left and we stop in the middle of an allocated
		   extent, we know the preceder immediately.. */
		/* middle of extent is (scan_index - unit_index) != 0. */
		if (scanning_left(scan) && (scan_index - unit_index) != 0) {
			/* FIXME(B): Someone should step-through and verify that this preceder
			   calculation is indeed correct. */
			/* @unit_start is starting block (number) of extent
			   unit. Flush stopped at the @scan_index block from
			   the beginning of the file, which is (scan_index -
			   unit_index) block within extent.
			*/
			if (unit_start) {
				/* skip preceder update when we are at hole */
				scan->preceder_blk = unit_start + scan_index - unit_index;
				check_preceder(scan->preceder_blk);
			}
		}

		/* In this case, we leave coord set to the parent of scan->node. */
		scan->stop = 1;

	} else {
		/* In this case, we are still scanning, coord is set to the next item which is
		   either off-the-end of the node or not an extent. */
		assert("jmacd-8912", scan->stop == 0);
		assert("jmacd-7812", (coord_is_after_sideof_unit(&coord, scan->direction)
				      || !item_is_extent(&coord)));
	}

	ret = 0;
exit:
	return ret;
}

/* ask block allocator for some blocks */
static void
extent_allocate_blocks(reiser4_blocknr_hint *preceder,
		       reiser4_block_nr wanted_count, reiser4_block_nr *first_allocated, reiser4_block_nr *allocated, block_stage_t block_stage)
{
	*allocated = wanted_count;
	preceder->max_dist = 0;	/* scan whole disk, if needed */

	/* that number of blocks (wanted_count) is either in UNALLOCATED or in GRABBED */
	preceder->block_stage = block_stage;

	/* FIXME: we do not handle errors here now */
	check_me("vs-420", reiser4_alloc_blocks (preceder, first_allocated, allocated, BA_PERMANENT) == 0);
	/* update flush_pos's preceder to last allocated block number */
	preceder->blk = *first_allocated + *allocated - 1;
}

/* when on flush time unallocated extent is to be replaced with allocated one it may happen that one unallocated extent
   will have to be replaced with set of allocated extents. In this case insert_into_item will be called which may have
   to add new nodes into tree. Space for that is taken from inviolable reserve (5%). */
static reiser4_block_nr
reserve_replace(void)
{
	reiser4_block_nr grabbed, needed;

	grabbed = get_current_context()->grabbed_blocks;
	needed = estimate_one_insert_into_item(current_tree);
	check_me("vpf-340", !reiser4_grab_space_force(needed, BA_RESERVED));
	return grabbed;
}

static void
free_replace_reserved(reiser4_block_nr grabbed)
{
	reiser4_context *ctx;

	ctx = get_current_context();
	grabbed2free(ctx, get_super_private(ctx->super),
		     ctx->grabbed_blocks - grabbed);
}

/* Block offset of first block addressed by unit */
reiser4_internal __u64
extent_unit_index(const coord_t *item)
{
	reiser4_key key;

	assert("vs-648", coord_is_existing_unit(item));
	unit_key_by_coord(item, &key);
	return get_key_offset(&key) >> current_blocksize_bits;
}

/* AUDIT shouldn't return value be of reiser4_block_nr type?
   Josh's answer: who knows?  Is a "number of blocks" the same type as "block offset"? */
reiser4_internal __u64
extent_unit_width(const coord_t *item)
{
	assert("vs-649", coord_is_existing_unit(item));
	return width_by_coord(item);
}

/* Starting block location of this unit */
reiser4_internal reiser4_block_nr
extent_unit_start(const coord_t *item)
{
	return extent_get_start(extent_by_coord(item));
}

/* replace allocated extent with two allocated extents */
static int
split_allocated_extent(coord_t *coord, reiser4_block_nr pos_in_unit)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent replace_ext;
	reiser4_extent append_ext;
	reiser4_key key;
	reiser4_item_data item;
	reiser4_block_nr grabbed;

	ext = extent_by_coord(coord);
	assert("vs-1410", state_of_extent(ext) == ALLOCATED_EXTENT);
	assert("vs-1411", extent_get_width(ext) > pos_in_unit);

	set_extent(&replace_ext, extent_get_start(ext), pos_in_unit);
	set_extent(&append_ext, extent_get_start(ext) + pos_in_unit, extent_get_width(ext) - pos_in_unit);

	/* insert_into_item will insert new unit after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + pos_in_unit * current_blocksize));

	ON_TRACE(TRACE_EXTENT_ALLOC,
		 "split [%llu %llu] -> [%llu %llu][%llu %llu]\n",
		 extent_get_start(ext), extent_get_width(ext),
		 extent_get_start(&replace_ext), extent_get_width(&replace_ext),
		 extent_get_start(&append_ext), extent_get_width(&append_ext));

	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node), &key, init_new_extent(&item, &append_ext, 1),
				&replace_ext, COPI_DONT_SHIFT_LEFT, 0/* return replaced position */);
	free_replace_reserved(grabbed);
	return result;
}

/* clear bit preventing node from being written bypassing extent allocation procedure */
static inline void
junprotect (jnode * node)
{
	assert("zam-837", !JF_ISSET(node, JNODE_EFLUSH));
	assert("zam-838", JF_ISSET(node, JNODE_EPROTECTED));

	JF_CLR(node, JNODE_EPROTECTED);
}

/* this is used to unprotect nodes which were protected before allocating but which will not be allocated either because
   space allocator allocates less blocks than were protected and/or if allocation of those nodes failed */
static void
unprotect_extent_nodes(flush_pos_t *flush_pos, __u64 count, capture_list_head *protected_nodes)
{
	jnode *node, *tmp;
	capture_list_head unprotected_nodes;
	txn_atom *atom;

	capture_list_init(&unprotected_nodes);

	atom = atom_locked_by_fq(pos_fq(flush_pos));
	assert("vs-1468", atom);

	assert("vs-1469", !capture_list_empty(protected_nodes));
	assert("vs-1474", count > 0);
	node = capture_list_back(protected_nodes);
	do {
		count --;
		junprotect(node);
		ON_DEBUG(
			LOCK_JNODE(node);
			count_jnode(atom, node, PROTECT_LIST, DIRTY_LIST, 0);
			UNLOCK_JNODE(node);
			);
		if (count == 0) {
			break;
		}
		tmp = capture_list_prev(node);
		node = tmp;
		assert("vs-1470", !capture_list_end(protected_nodes, node));
	} while (1);

	/* move back to dirty list */
	capture_list_split(protected_nodes, &unprotected_nodes, node);
	capture_list_splice(ATOM_DIRTY_LIST(atom, LEAF_LEVEL), &unprotected_nodes);

	UNLOCK_ATOM(atom);
}

extern int getjevent(void);

/* remove node from atom's list and put to the end of list @jnodes */
static void
protect_reloc_node(capture_list_head *jnodes, jnode *node)
{
	assert("zam-836", !JF_ISSET(node, JNODE_EPROTECTED));
	assert("vs-1216", jnode_is_unformatted(node));
	assert("vs-1477", spin_atom_is_locked(node->atom));
	assert("nikita-3390", spin_jnode_is_locked(node));

	JF_SET(node, JNODE_EPROTECTED);
	capture_list_remove_clean(node);
	capture_list_push_back(jnodes, node);
	ON_DEBUG(count_jnode(node->atom, node, DIRTY_LIST, PROTECT_LIST, 0));
}

#define JNODES_TO_UNFLUSH (16)

/* @count nodes of file (objectid @oid) starting from @index are going to be allocated. Protect those nodes from
   e-flushing. Nodes which are eflushed already will be un-eflushed. There will be not more than JNODES_TO_UNFLUSH
   un-eflushed nodes. If a node is not found or flushprepped - stop protecting */
/* FIXME: it is likely that not flushprepped jnodes are on dirty capture list in sequential order.. */
static int
protect_extent_nodes(flush_pos_t *flush_pos, oid_t oid, unsigned long index, reiser4_block_nr count,
		     reiser4_block_nr *protected, reiser4_extent *ext,
		     capture_list_head *protected_nodes)
{
	__u64           i;
	__u64           j;
	int             result;
	reiser4_tree   *tree;
	int             eflushed;
	jnode          *buf[JNODES_TO_UNFLUSH];
       	txn_atom       *atom;

	assert("nikita-3394", capture_list_empty(protected_nodes));

	tree = current_tree;

	atom = atom_locked_by_fq(pos_fq(flush_pos));
	assert("vs-1468", atom);

	assert("vs-1470", extent_get_width(ext) == count);
	eflushed = 0;
	*protected = 0;
	for (i = 0; i < count; ++i, ++index) {
		jnode  *node;

		node = jlookup(tree, oid, index);
		if (!node)
			break;

		if (jnode_check_flushprepped(node)) {
			atomic_dec(&node->x_count);
			break;
		}

		LOCK_JNODE(node);
		assert("vs-1476", atomic_read(&node->x_count) > 1);
		assert("nikita-3393", !JF_ISSET(node, JNODE_EPROTECTED));

		if (JF_ISSET(node, JNODE_EFLUSH)) {
			if (eflushed == JNODES_TO_UNFLUSH) {
				UNLOCK_JNODE(node);
 				atomic_dec(&node->x_count);
				break;
			}
			buf[eflushed] = node;
			eflushed ++;
			protect_reloc_node(protected_nodes, node);
			UNLOCK_JNODE(node);
		} else {
			assert("nikita-3384", node->atom == atom);
			protect_reloc_node(protected_nodes, node);
			assert("nikita-3383", !JF_ISSET(node, JNODE_EFLUSH));
			UNLOCK_JNODE(node);
			atomic_dec(&node->x_count);
		}

		(*protected) ++;
	}
	UNLOCK_ATOM(atom);

	/* start io for eflushed nodes */
	for (j = 0; j < eflushed; ++ j)
		jstartio(buf[j]);

	result = 0;
	for (j = 0; j < eflushed; ++ j) {
		if (result == 0) {
			result = emergency_unflush(buf[j]);
			if (result != 0) {
				warning("nikita-3179",
					"unflush failed: %i", result);
				print_jnode("node", buf[j]);
			}
		}
		jput(buf[j]);
	}
	if (result != 0) {
		/* unprotect all the jnodes we have protected so far */
		unprotect_extent_nodes(flush_pos, i, protected_nodes);
	}
	return result;
}

/* replace extent @ext by extent @replace. Try to merge @replace with previous extent of the item (if there is
   one). Return 1 if it succeeded, 0 - otherwise */
static int
try_to_merge_with_left(coord_t *coord, reiser4_extent *ext, reiser4_extent *replace)
{
	assert("vs-1415", extent_by_coord(coord) == ext);

	if (coord->unit_pos == 0 || state_of_extent(ext - 1) != ALLOCATED_EXTENT)
		/* @ext either does not exist or is not allocated extent */
		return 0;
	if (extent_get_start(ext - 1) + extent_get_width(ext - 1) != extent_get_start(replace))
		return 0;

	/* we can glue, widen previous unit */
	ON_TRACE(TRACE_EXTENT_ALLOC,
		 "wide previous [%llu %llu] ->",
		 extent_get_start(ext - 1), extent_get_width(ext - 1));

	extent_set_width(ext - 1, extent_get_width(ext - 1) + extent_get_width(replace));

	ON_TRACE(TRACE_EXTENT_ALLOC, " [%llu %llu] -> ", extent_get_start(ext - 1), extent_get_width(ext - 1));

	if (extent_get_width(ext) != extent_get_width(replace)) {
		/* make current extent narrower */
		ON_TRACE(TRACE_EXTENT_ALLOC, "narrow [%llu %llu] -> ", extent_get_start(ext), extent_get_width(ext));

		if (state_of_extent(ext) == ALLOCATED_EXTENT)
			extent_set_start(ext, extent_get_start(ext) + extent_get_width(replace));
		extent_set_width(ext, extent_get_width(ext) - extent_get_width(replace));

		ON_TRACE(TRACE_EXTENT_ALLOC, "[%llu %llu]\n", extent_get_start(ext), extent_get_width(ext));
	} else {
		/* current extent completely glued with its left neighbor, remove it */
		coord_t from, to;

		ON_TRACE(TRACE_EXTENT_ALLOC, "delete [%llu %llu]\n", extent_get_start(ext), extent_get_width(ext));

		coord_dup(&from, coord);
		from.unit_pos = nr_units_extent(coord) - 1;
		coord_dup(&to, &from);

		/* currently cut from extent can cut either from the beginning or from the end. Move place which got
		   freed after unit removal to end of item */
		xmemmove(ext, ext + 1, (from.unit_pos - coord->unit_pos) * sizeof(reiser4_extent));
		/* wipe part of item which is going to be cut, so that node_check will not be confused */
		ON_DEBUG(xmemset(extent_item(coord) + from.unit_pos, 0, sizeof (reiser4_extent)));
		cut_node_content(&from, &to, NULL, NULL, NULL);
	}
	znode_make_dirty(coord->node);
	/* move coord back */
	coord->unit_pos --;
	return 1;
}

/* replace extent (unallocated or allocated) pointed by @coord with extent @replace (allocated). If @replace is shorter
   than @coord - add padding extent */
static int
conv_extent(coord_t *coord, reiser4_extent *replace)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent padd_ext;
	reiser4_block_nr start, width, new_width;
	reiser4_block_nr grabbed;
	reiser4_item_data item;
	reiser4_key key;
	extent_state state;

	ext = extent_by_coord(coord);
	state = state_of_extent(ext);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	new_width = extent_get_width(replace);

	assert("vs-1458", state == UNALLOCATED_EXTENT || state == ALLOCATED_EXTENT);
	assert("vs-1459", width >= new_width);

	if (try_to_merge_with_left(coord, ext, replace)) {
		/* merged @replace with left neighbor. Current unit is either removed or narrowed */
		assert("nikita-3563", znode_at_read(coord->node));
		return 0;
	}

	if (width == new_width) {
		/* replace current extent with @replace */
		ON_TRACE(TRACE_EXTENT_ALLOC, "replace: [%llu %llu]->[%llu %llu]\n",
		       start, width,
		       extent_get_start(replace), extent_get_width(replace));

		*ext = *replace;
		znode_make_dirty(coord->node);
		assert("nikita-3563", znode_at_read(coord->node));
		return 0;
	}

	/* replace @ext with @replace and padding extent */
	set_extent(&padd_ext, state == ALLOCATED_EXTENT ? (start + new_width) : UNALLOCATED_EXTENT_START,
		   width - new_width);

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + new_width * current_blocksize));

	ON_TRACE(TRACE_EXTENT_ALLOC,
		 "replace: [%llu %llu]->[%llu %llu][%llu %llu]\n",
		 start, width,
		 extent_get_start(replace), extent_get_width(replace),
		 extent_get_start(&padd_ext), extent_get_width(&padd_ext));

	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node), &key, init_new_extent(&item, &padd_ext, 1),
				replace, COPI_DONT_SHIFT_LEFT, 0/* return replaced position */);

	assert("nikita-3563", znode_at_read(coord->node));
	free_replace_reserved(grabbed);
	return result;
}

/* for every jnode from @protected_nodes list assign block number and mark it RELOC and FLUSH_QUEUED. Attach whole
   @protected_nodes list to flush queue's prepped list */
static void
assign_real_blocknrs(flush_pos_t *flush_pos, reiser4_block_nr first, reiser4_block_nr count,
		     extent_state state, capture_list_head *protected_nodes)
{
	jnode *node;
	txn_atom *atom;
	flush_queue_t *fq;
	int i;

	fq = pos_fq(flush_pos);
	atom = atom_locked_by_fq(fq);
	assert("vs-1468", atom);

	i = 0;
	for_all_type_safe_list(capture, protected_nodes, node) {
		LOCK_JNODE(node);
		assert("vs-1132", ergo(state == UNALLOCATED_EXTENT, blocknr_is_fake(jnode_get_block(node))));
		assert("vs-1475", node->atom == atom);
		assert("vs-1476", atomic_read(&node->x_count) > 0);
		JF_CLR(node, JNODE_FLUSH_RESERVED);
		jnode_set_block(node, &first);
		unformatted_make_reloc(node, fq);
		/*XXXX*/ON_DEBUG(count_jnode(node->atom, node, PROTECT_LIST, FQ_LIST, 0));
		junprotect(node);
		assert("", NODE_LIST(node) == FQ_LIST);
		UNLOCK_JNODE(node);
		first ++;
		i ++;
	}

	capture_list_splice(ATOM_FQ_LIST(fq), protected_nodes);
	/*XXX*/
	assert("vs-1687", count == i);
	if (state == UNALLOCATED_EXTENT)
		dec_unalloc_unfm_ptrs(count);
	UNLOCK_ATOM(atom);
}

static void
make_node_ovrwr(capture_list_head *jnodes, jnode *node)
{
	LOCK_JNODE(node);

	assert ("zam-917", !JF_ISSET(node, JNODE_RELOC));
	assert ("zam-918", !JF_ISSET(node, JNODE_OVRWR));

	JF_SET(node, JNODE_OVRWR);
	capture_list_remove_clean(node);
	capture_list_push_back(jnodes, node);
	ON_DEBUG(count_jnode(node->atom, node, DIRTY_LIST, OVRWR_LIST, 0));

	UNLOCK_JNODE(node);
}

/* put nodes of one extent (file objectid @oid, extent width @width) to overwrite set. Starting from the one with index
   @index. If end of slum is detected (node is not found or flushprepped) - stop iterating and set flush position's
   state to POS_INVALID */
static void
mark_jnodes_overwrite(flush_pos_t *flush_pos, oid_t oid, unsigned long index, reiser4_block_nr width)
{
	unsigned long i;
	reiser4_tree *tree;
	jnode *node;
	txn_atom *atom;
	capture_list_head jnodes;

	capture_list_init(&jnodes);

	tree = current_tree;

	atom = atom_locked_by_fq(pos_fq(flush_pos));
	assert("vs-1478", atom);

	for (i = flush_pos->pos_in_unit; i < width; i ++, index ++) {
		node = jlookup(tree, oid, index);
		if (!node) {
			flush_pos->state = POS_INVALID;

			ON_TRACE(TRACE_EXTENT_ALLOC, "node not found: (oid %llu, index %lu)\n", oid, index);

			break;
		}
		if (jnode_check_flushprepped(node)) {
			flush_pos->state = POS_INVALID;
			atomic_dec(&node->x_count);

			ON_TRACE(TRACE_EXTENT_ALLOC, "flushprepped: (oid %llu, index %lu)\n", oid, index);

			break;
		}
		make_node_ovrwr(&jnodes, node);
		atomic_dec(&node->x_count);
	}

	capture_list_splice(ATOM_OVRWR_LIST(atom), &jnodes);
	UNLOCK_ATOM(atom);
}

/* this is called by handle_pos_on_twig to proceed extent unit flush_pos->coord is set to. It is to prepare for flushing
   sequence of not flushprepped nodes (slum). It supposes that slum starts at flush_pos->pos_in_unit position within the
   extent. Slum gets to relocate set if flush_pos->leaf_relocate is set to 1 and to overwrite set otherwise */
reiser4_internal int
alloc_extent(flush_pos_t *flush_pos)
{
	coord_t *coord;
	reiser4_extent *ext;
	reiser4_extent replace_ext;
	oid_t oid;
	reiser4_block_nr protected;
	reiser4_block_nr start;
	__u64 index;
	__u64 width;
	extent_state state;
	int result;
	reiser4_block_nr first_allocated;
	__u64 allocated;
	reiser4_key key;
	block_stage_t block_stage;

	assert("vs-1468", flush_pos->state == POS_ON_EPOINT);
	assert("vs-1469", coord_is_existing_unit(&flush_pos->coord) && item_is_extent(&flush_pos->coord));

	coord = &flush_pos->coord;

	check_pos(flush_pos);

	ext = extent_by_coord(coord);
	state = state_of_extent(ext);
	if (state == HOLE_EXTENT) {
		flush_pos->state = POS_INVALID;
		return 0;
	}

	item_key_by_coord(coord, &key);
	oid = get_key_objectid(&key);
	index = extent_unit_index(coord) + flush_pos->pos_in_unit;
	start = extent_get_start(ext);
	width = extent_get_width(ext);

	assert("vs-1457", width > flush_pos->pos_in_unit);

	if (flush_pos->leaf_relocate || state == UNALLOCATED_EXTENT) {
		protected_jnodes jnodes;

		/* relocate */
		if (flush_pos->pos_in_unit) {
			/* split extent unit into two */
			result = split_allocated_extent(coord, flush_pos->pos_in_unit);
			check_pos(flush_pos);
			flush_pos->pos_in_unit = 0;
			return result;
		}
		ON_TRACE(TRACE_EXTENT_ALLOC,
			 "ALLOC: relocate: (oid %llu, index %llu) [%llu %llu] - ",
			 oid, index, start, width);

		/* Prevent nodes from e-flushing before allocating disk space for them. Nodes which were eflushed will be
		   read from their temporary locations (but not more than certain limit: JNODES_TO_UNFLUSH) and that
		   disk space will be freed. */

		protected_jnodes_init(&jnodes);

		result = protect_extent_nodes(flush_pos, oid, index, width, &protected, ext, &jnodes.nodes);
		check_pos(flush_pos);
		if (result) {
  			warning("vs-1469", "Failed to protect extent. Should not happen\n");
			protected_jnodes_done(&jnodes);
			return result;
		}
		if (protected == 0) {
			ON_TRACE(TRACE_EXTENT_ALLOC, "nothing todo\n");
			flush_pos->state = POS_INVALID;
			flush_pos->pos_in_unit = 0;
			protected_jnodes_done(&jnodes);
			return 0;
		}

		if (state == ALLOCATED_EXTENT)
			/* all protected nodes are not flushprepped, therefore
			 * they are counted as flush_reserved */
			block_stage = BLOCK_FLUSH_RESERVED;
		else
			block_stage = BLOCK_UNALLOCATED;

		/* allocate new block numbers for protected nodes */
		extent_allocate_blocks(pos_hint(flush_pos), protected, &first_allocated, &allocated, block_stage);
		check_pos(flush_pos);

		ON_TRACE(TRACE_EXTENT_ALLOC, "allocated: (first %llu, cound %llu) - ", first_allocated, allocated);

		if (allocated != protected)
			/* unprotect nodes which will not be
			 * allocated/relocated on this iteration */
			unprotect_extent_nodes(flush_pos, protected - allocated,
					       &jnodes.nodes);
		check_pos(flush_pos);
		if (state == ALLOCATED_EXTENT) {
			/* on relocating - free nodes which are going to be
			 * relocated */
			reiser4_dealloc_blocks(&start, &allocated, BLOCK_ALLOCATED, BA_DEFER);
		}

		check_pos(flush_pos);
		/* assign new block numbers to protected nodes */
		assign_real_blocknrs(flush_pos, first_allocated, allocated, state, &jnodes.nodes);

		check_pos(flush_pos);
		protected_jnodes_done(&jnodes);

		/* send to log information about which blocks were allocated for what */
		write_current_logf(ALLOC_EXTENT_LOG,
				   "alloc: oid: %llu, index: %llu, state %d, width: %llu. "
				   "prot: %llu. got [%llu %llu]",
				   oid, index, state, width, protected, first_allocated, allocated);

		/* prepare extent which will replace current one */
		set_extent(&replace_ext, first_allocated, allocated);

		/* adjust extent item */
		result = conv_extent(coord, &replace_ext);
		check_pos(flush_pos);
		if (result != 0 && result != -ENOMEM) {
  			warning("vs-1461", "Failed to allocate extent. Should not happen\n");
			return result;
		}
	} else {
		/* overwrite */
		ON_TRACE(TRACE_EXTENT_ALLOC,
			 "ALLOC: overwrite: (oid %llu, index %llu) [%llu %llu]\n",
			 oid, index, start, width);
		mark_jnodes_overwrite(flush_pos, oid, index, width);
	}
	flush_pos->pos_in_unit = 0;
	check_pos(flush_pos);
	return 0;
}

/* if @key is glueable to the item @coord is set to */
static int
must_insert(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key last;

	if (item_id_by_coord(coord) == EXTENT_POINTER_ID && keyeq(append_key_extent(coord, &last), key))
		return 0;
	return 1;
}

  /* copy extent @copy to the end of @node. It may have to either insert new item after the last one, or append last item,
   or modify last unit of last item to have greater width */
static int
put_unit_to_end(znode *node, const reiser4_key *key, reiser4_extent *copy_ext)
{
	int result;
	coord_t coord;
	cop_insert_flag flags;
	reiser4_extent *last_ext;
	reiser4_item_data data;

	/* set coord after last unit in an item */
	coord_init_last_unit(&coord, node);
	coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;
	if (must_insert(&coord, key)) {
		result = insert_by_coord(&coord, init_new_extent(&data, copy_ext, 1), key, 0 /*lh */ , flags);

	} else {
		/* try to glue with last unit */
		last_ext = extent_by_coord(&coord);
		if (state_of_extent(last_ext) &&
		    extent_get_start(last_ext) + extent_get_width(last_ext) == extent_get_start(copy_ext)) {
			/* widen last unit of node */
			extent_set_width(last_ext, extent_get_width(last_ext) + extent_get_width(copy_ext));
			znode_make_dirty(node);
			return 0;
		}

		/* FIXME: put an assertion here that we can not merge last unit in @node and new unit */
		result = insert_into_item(&coord, 0 /*lh */ , key, init_new_extent(&data, copy_ext, 1), flags);
	}

	assert("vs-438", result == 0 || result == -E_NODE_FULL);
	return result;
}

/* @coord is set to extent unit */
reiser4_internal squeeze_result
squalloc_extent(znode *left, const coord_t *coord, flush_pos_t *flush_pos, reiser4_key *stop_key)
{
	reiser4_extent *ext;
	__u64 index;
	__u64 width;
	reiser4_block_nr start;
	extent_state state;
	oid_t oid;
	reiser4_block_nr first_allocated;
	__u64 allocated;
	__u64 protected;
	reiser4_extent copy_extent;
	reiser4_key key;
	int result;
	block_stage_t block_stage;

	assert("vs-1457", flush_pos->pos_in_unit == 0);
	assert("vs-1467", coord_is_leftmost_unit(coord));
	assert("vs-1467", item_is_extent(coord));

	ext = extent_by_coord(coord);
	index = extent_unit_index(coord);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	state = state_of_extent(ext);
	unit_key_by_coord(coord, &key);
	oid = get_key_objectid(&key);

	if (flush_pos->leaf_relocate || state == UNALLOCATED_EXTENT) {
		protected_jnodes jnodes;

		ON_TRACE(TRACE_EXTENT_ALLOC, "SQUALLOC: relocate: (oid %llu, index %llu) [%llu %llu] - ",
			 oid, index, start, width);

		/* relocate */
		protected_jnodes_init(&jnodes);
		result = protect_extent_nodes(flush_pos, oid, index, width, &protected, ext, &jnodes.nodes);
		if (result) {
  			warning("vs-1469", "Failed to protect extent. Should not happen\n");
			protected_jnodes_done(&jnodes);
			return result;
		}
		if (protected == 0) {
			flush_pos->state = POS_INVALID;
			protected_jnodes_done(&jnodes);
			return 0;
		}

		if (state == ALLOCATED_EXTENT)
			/* all protected nodes are not flushprepped, therefore
			 * they are counted as flush_reserved */
			block_stage = BLOCK_FLUSH_RESERVED;
		else
			block_stage = BLOCK_UNALLOCATED;

		/* allocate new block numbers for protected nodes */
		extent_allocate_blocks(pos_hint(flush_pos), protected, &first_allocated, &allocated, block_stage);
		ON_TRACE(TRACE_EXTENT_ALLOC, "allocated: (first %llu, cound %llu) - ", first_allocated, allocated);
		if (allocated != protected)
			unprotect_extent_nodes(flush_pos, protected - allocated,
					       &jnodes.nodes);

		/* prepare extent which will be copied to left */
		set_extent(&copy_extent, first_allocated, allocated);

		result = put_unit_to_end(left, &key, &copy_extent);
		if (result == -E_NODE_FULL) {
			int target_block_stage;

			/* free blocks which were just allocated */
			ON_TRACE(TRACE_EXTENT_ALLOC,
				 "left is full, free (first %llu, count %llu)\n",
				 first_allocated, allocated);
			target_block_stage = (state == ALLOCATED_EXTENT) ? BLOCK_FLUSH_RESERVED : BLOCK_UNALLOCATED;
			reiser4_dealloc_blocks(&first_allocated, &allocated, target_block_stage, BA_PERMANENT);
			unprotect_extent_nodes(flush_pos, allocated, &jnodes.nodes);

			/* rewind the preceder. */
			flush_pos->preceder.blk = first_allocated;
			check_preceder(flush_pos->preceder.blk);

			protected_jnodes_done(&jnodes);
			return SQUEEZE_TARGET_FULL;
		}

		if (state == ALLOCATED_EXTENT) {
			/* free nodes which were relocated */
			reiser4_dealloc_blocks(&start, &allocated, BLOCK_ALLOCATED, BA_DEFER);
		}

		/* assign new block numbers to protected nodes */
		assign_real_blocknrs(flush_pos, first_allocated, allocated, state, &jnodes.nodes);
		protected_jnodes_done(&jnodes);

		set_key_offset(&key, get_key_offset(&key) + (allocated << current_blocksize_bits));
		ON_TRACE(TRACE_EXTENT_ALLOC,
			 "copied to left: [%llu %llu]\n", first_allocated, allocated);

		/* send to log information about which blocks were allocated for what */
		write_current_logf(ALLOC_EXTENT_LOG,
				   "sqalloc: oid: %llu, index: %llu, state %d, width: %llu. "
				   "prot: %llu. got [%llu %llu]",
				   oid, index, state, width, protected, first_allocated, allocated);
	} else {
		/* overwrite */
		ON_TRACE(TRACE_EXTENT_ALLOC,
			 "SQUALLOC: overwrite: (oid %llu, index %llu) [%llu %llu] - ", oid, index, start, width);

		/* overwrite: try to copy unit as it is to left neighbor and make all first not flushprepped nodes
		   overwrite nodes */
		set_extent(&copy_extent, start, width);
		result = put_unit_to_end(left, &key, &copy_extent);
		if (result == -E_NODE_FULL) {
			ON_TRACE(TRACE_EXTENT_ALLOC, "left is full\n");
			return SQUEEZE_TARGET_FULL;
		}
		mark_jnodes_overwrite(flush_pos, oid, index, width);
		set_key_offset(&key, get_key_offset(&key) + (width << current_blocksize_bits));
		ON_TRACE(TRACE_EXTENT_ALLOC, "copied to left\n");
	}
	*stop_key = key;
	return SQUEEZE_CONTINUE;
}

reiser4_internal int
key_by_offset_extent(struct inode *inode, loff_t off, reiser4_key *key)
{
	return key_by_inode_and_offset_common(inode, off, key);
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
