/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/node/node.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree_walk.h"
#include "tree_mod.h"
#include "carry.h"
#include "tree.h"
#include "super.h"

#include <linux/types.h>	/* for __u??  */

/* Extents on the twig level (EOTTL) handling.

   EOTTL poses some problems to the tree traversal, that are better
   explained by example.

   Suppose we have block B1 on the twig level with the following items:

   0. internal item I0 with key (0:0:0:0) (locality, key-type, object-id, offset)
   1. extent item E1 with key (1:4:100:0), having 10 blocks of 4k each
   2. internal item I2 with key (10:0:0:0)

   We are trying to insert item with key (5:0:0:0). Lookup finds node
   B1, and then intra-node lookup is done. This lookup finished on the
   E1, because the key we are looking for is larger than the key of E1
   and is smaller than key the of I2.

   Here search is stuck.

   After some thought it is clear what is wrong here: extents on the
   twig level break some basic property of the *search* tree (on the
   pretext, that they restore property of balanced tree).

   Said property is the following: if in the internal node of the search
   tree we have [ ... Key1 Pointer Key2 ... ] then, all data that are or
   will be keyed in the tree with the Key such that Key1 <= Key < Key2
   are accessible through the Pointer.

   This is not true, when Pointer is Extent-Pointer, simply because
   extent cannot expand indefinitely to the right to include any item
   with

     Key1 <= Key <= Key2.

   For example, our E1 extent is only responsible for the data with keys

     (1:4:100:0) <= key <= (1:4:100:0xffffffffffffffff), and

   so, key range

     ( (1:4:100:0xffffffffffffffff), (10:0:0:0) )

   is orphaned: there is no way to get there from the tree root.

   In other words, extent pointers are different than normal child
   pointers as far as search tree is concerned, and this creates such
   problems.

   Possible solution for this problem is to insert our item into node
   pointed to by I2. There are some problems through:

   (1) I2 can be in a different node.
   (2) E1 can be immediately followed by another extent E2.

   (1) is solved by calling reiser4_get_right_neighbor() and accounting
   for locks/coords as necessary.

   (2) is more complex. Solution here is to insert new empty leaf node
   and insert internal item between E1 and E2 pointing to said leaf
   node. This is further complicated by possibility that E2 is in a
   different node, etc.

   Problems:

   (1) if there was internal item I2 immediately on the right of an
   extent E1 we and we decided to insert new item S1 into node N2
   pointed to by I2, then key of S1 will be less than smallest key in
   the N2. Normally, search key checks that key we are looking for is in
   the range of keys covered by the node key is being looked in. To work
   around of this situation, while preserving useful consistency check
   new flag CBK_TRUST_DK was added to the cbk falgs bitmask. This flag
   is automatically set on entrance to the coord_by_key() and is only
   cleared when we are about to enter situation described above.

   (2) If extent E1 is immediately followed by another extent E2 and we
   are searching for the key that is between E1 and E2 we only have to
   insert new empty leaf node when coord_by_key was called for
   insertion, rather than just for lookup. To distinguish these cases,
   new flag CBK_FOR_INSERT was added to the cbk falgs bitmask. This flag
   is automatically set by coord_by_key calls performed by
   insert_by_key() and friends.

   (3) Insertion of new empty leaf node (possibly) requires
   balancing. In any case it requires modification of node content which
   is only possible under write lock. It may well happen that we only
   have read lock on the node where new internal pointer is to be
   inserted (common case: lookup of non-existent stat-data that fells
   between two extents). If only read lock is held, tree traversal is
   restarted with lock_level modified so that next time we hit this
   problem, write lock will be held. Once we have write lock, balancing
   will be performed.






*/

/* look to the right of @coord. If it is an item of internal type - 1 is
   returned. If that item is in right neighbor and it is internal - @coord and
   @lh are switched to that node: move lock handle, zload right neighbor and
   zrelse znode coord was set to at the beginning
*/
/* Audited by: green(2002.06.15) */
static int
is_next_item_internal(coord_t * coord)
{
	if (coord->item_pos != node_num_items(coord->node) - 1) {
		/* next item is in the same node */
		coord_t right;

		coord_dup(&right, coord);
		check_me("vs-742", coord_next_item(&right) == 0);
		if (item_is_internal(&right)) {
			coord_dup(coord, &right);
			return 1;
		}
	}
	return 0;
}

/* inserting empty leaf after (or between) item of not internal type we have to
   know which right delimiting key corresponding znode has to be inserted with */
static reiser4_key *
rd_key(coord_t * coord, reiser4_key * key)
{
	coord_t dup;

	assert("nikita-2281", coord_is_between_items(coord));
	coord_dup(&dup, coord);

	RLOCK_DK(current_tree);

	if (coord_set_to_right(&dup) == 0)
		/* get right delimiting key from an item to the right of @coord */
		unit_key_by_coord(&dup, key);
	else
		/* use right delimiting key of parent znode */
		*key = *znode_get_rd_key(coord->node);

	RUNLOCK_DK(current_tree);
	return key;
}


ON_DEBUG(void check_dkeys(const znode *);)

/* this is used to insert empty node into leaf level if tree lookup can not go
   further down because it stopped between items of not internal type */
static int
add_empty_leaf(coord_t * insert_coord, lock_handle * lh, const reiser4_key * key, const reiser4_key * rdkey)
{
	int result;
	carry_pool pool;
	carry_level todo;
	carry_op *op;
	/*znode *parent_node;*/
	znode *node;
	reiser4_item_data item;
	carry_insert_data cdata;
	reiser4_tree *tree;

	init_carry_pool(&pool);
	init_carry_level(&todo, &pool);
	ON_STATS(todo.level_no = TWIG_LEVEL);
	assert("vs-49827", znode_contains_key_lock(insert_coord->node, key));

	tree = znode_get_tree(insert_coord->node);
	node = new_node(insert_coord->node, LEAF_LEVEL);
	if (IS_ERR(node))
		return PTR_ERR(node);

	/* setup delimiting keys for node being inserted */
	WLOCK_DK(tree);
	znode_set_ld_key(node, key);
	znode_set_rd_key(node, rdkey);
	ON_DEBUG(node->creator = current);
	ON_DEBUG(node->first_key = *key);
	WUNLOCK_DK(tree);

	ZF_SET(node, JNODE_ORPHAN);
	op = post_carry(&todo, COP_INSERT, insert_coord->node, 0);
	if (!IS_ERR(op)) {
		cdata.coord = insert_coord;
		cdata.key = key;
		cdata.data = &item;
		op->u.insert.d = &cdata;
		op->u.insert.type = COPT_ITEM_DATA;
		build_child_ptr_data(node, &item);
		item.arg = NULL;
		/* have @insert_coord to be set at inserted item after
		   insertion is done */
		todo.track_type = CARRY_TRACK_CHANGE;
		todo.tracked = lh;

		result = carry(&todo, 0);
		if (result == 0) {
			/*
			 * pin node in memory. This is necessary for
			 * znode_make_dirty() below.
			 */
			result = zload(node);
			if (result == 0) {
				lock_handle local_lh;

				/*
				 * if we inserted new child into tree we have
				 * to mark it dirty so that flush will be able
				 * to process it.
				 */
				init_lh(&local_lh);
				result = longterm_lock_znode(&local_lh, node,
							     ZNODE_WRITE_LOCK,
							     ZNODE_LOCK_LOPRI);
				if (result == 0) {
					znode_make_dirty(node);

					/* when internal item pointing to @node
					   was inserted into twig node
					   create_hook_internal did not connect
					   it properly because its right
					   neighbor was not known. Do it
					   here */
					WLOCK_TREE(tree);
					assert("nikita-3312", znode_is_right_connected(node));
					assert("nikita-2984", node->right == NULL);
					ZF_CLR(node, JNODE_RIGHT_CONNECTED);
					WUNLOCK_TREE(tree);
					result = connect_znode(insert_coord, node);
					if (result == 0)
						ON_DEBUG(check_dkeys(node));

					done_lh(lh);
					move_lh(lh, &local_lh);
					assert("vs-1676", node_is_empty(node));
					coord_init_first_unit(insert_coord, node);
				} else {
					warning("nikita-3136",
						"Cannot lock child");
					print_znode("child", node);
				}
				done_lh(&local_lh);
				zrelse(node);
			}
		}
	} else
		result = PTR_ERR(op);
	zput(node);
	done_carry_pool(&pool);
	return result;
}

/* handle extent-on-the-twig-level cases in tree traversal */
reiser4_internal int
handle_eottl(cbk_handle * h /* cbk handle */ ,
	     int *outcome /* how traversal should proceed */ )
{
	int result;
	reiser4_key key;
	coord_t *coord;

	coord = h->coord;

	if (h->level != TWIG_LEVEL || (coord_is_existing_item(coord) && item_is_internal(coord))) {
		/* Continue to traverse tree downward. */
		return 0;
	}
	/* strange item type found on non-stop level?!  Twig
	   horrors? */
	assert("vs-356", h->level == TWIG_LEVEL);
	assert("vs-357", ( {
			  coord_t lcoord;
			  coord_dup(&lcoord, coord);
			  check_me("vs-733", coord_set_to_left(&lcoord) == 0);
			  item_is_extent(&lcoord);}
	       ));

	if (*outcome == NS_FOUND) {
		/* we have found desired key on twig level in extent item */
		h->result = CBK_COORD_FOUND;
		reiser4_stat_inc(tree.cbk_found);
		*outcome = LOOKUP_DONE;
		return 1;
	}

	if (!(h->flags & CBK_FOR_INSERT)) {
		/* tree traversal is not for insertion. Just return
		   CBK_COORD_NOTFOUND. */
		h->result = CBK_COORD_NOTFOUND;
		*outcome = LOOKUP_DONE;
		return 1;
	}

	/* take a look at the item to the right of h -> coord */
	result = is_next_item_internal(coord);
	if (result == 0) {
		/* item to the right is also an extent one. Allocate a new node
		   and insert pointer to it after item h -> coord.

		   This is a result of extents being located at the twig
		   level. For explanation, see comment just above
		   is_next_item_internal().
		*/
		if (cbk_lock_mode(h->level, h) != ZNODE_WRITE_LOCK) {
			/* we got node read locked, restart coord_by_key to
			   have write lock on twig level */
			h->lock_level = TWIG_LEVEL;
			h->lock_mode = ZNODE_WRITE_LOCK;
			*outcome = LOOKUP_REST;
			return 1;
		}

		result = add_empty_leaf(coord, h->active_lh, h->key, rd_key(coord, &key));
		if (result) {
			h->error = "could not add empty leaf";
			h->result = result;
			*outcome = LOOKUP_DONE;
			return 1;
		}
		/* added empty leaf is locked, its parent node is unlocked,
		   coord is set as EMPTY */
		*outcome = LOOKUP_DONE;
		h->result = CBK_COORD_NOTFOUND;
		return 1;
		/*assert("vs-358", keyeq(h->key, item_key_by_coord(coord, &key)));*/
	} else {
		/* this is special case mentioned in the comment on
		   tree.h:cbk_flags. We have found internal item immediately
		   on the right of extent, and we are going to insert new item
		   there. Key of item we are going to insert is smaller than
		   leftmost key in the node pointed to by said internal item
		   (otherwise search wouldn't come to the extent in the first
		   place).

		   This is a result of extents being located at the twig
		   level. For explanation, see comment just above
		   is_next_item_internal().
		*/
		h->flags &= ~CBK_TRUST_DK;
	}
	assert("vs-362", item_is_internal(coord));
	return 0;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
