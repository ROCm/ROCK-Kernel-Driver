/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Routines and macros to:

   get_left_neighbor()

   get_right_neighbor()

   get_parent()

   get_first_child()

   get_last_child()

   various routines to walk the whole tree and do things to it like
   repack it, or move it to tertiary storage.  Please make them as
   generic as is reasonable.

*/

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "jnode.h"
#include "znode.h"
#include "tree_walk.h"
#include "tree.h"
#include "super.h"

/* These macros are used internally in tree_walk.c in attempt to make
   lock_neighbor() code usable to build lock_parent(), lock_right_neighbor,
   lock_left_neighbor */
#define GET_NODE_BY_PTR_OFFSET(node, off) (*(znode**)(((unsigned long)(node)) + (off)))
#define FIELD_OFFSET(name)  offsetof(znode, name)
#define PARENT_PTR_OFFSET FIELD_OFFSET(in_parent.node)
#define LEFT_PTR_OFFSET   FIELD_OFFSET(left)
#define RIGHT_PTR_OFFSET  FIELD_OFFSET(right)

/* This is the generic procedure to get and lock `generic' neighbor (left or
    right neighbor or parent). It implements common algorithm for all cases of
    getting lock on neighbor node, only znode structure field is different in
    each case. This is parameterized by ptr_offset argument, which is byte
    offset for the pointer to the desired neighbor within the current node's
    znode structure. This function should be called with the tree lock held */
static int
lock_neighbor(
		     /* resulting lock handle*/
		     lock_handle * result,
		     /* znode to lock */
		     znode * node,
		     /* pointer to neighbor (or parent) znode field offset, in bytes from
		        the base address of znode structure  */
		     int ptr_offset,
		     /* lock mode for longterm_lock_znode call */
		     znode_lock_mode mode,
		     /* lock request for longterm_lock_znode call */
		     znode_lock_request req,
		     /* GN_* flags */
		     int flags,
		     int rlocked)
{
	reiser4_tree *tree = znode_get_tree(node);
	znode *neighbor;
	int ret;

	assert("umka-236", node != NULL);
	assert("umka-237", tree != NULL);
	assert("umka-301", rw_tree_is_locked(tree));

	reiser4_stat_inc_at_level(znode_get_level(node), znode.lock_neighbor);

	if (flags & GN_TRY_LOCK)
		req |= ZNODE_LOCK_NONBLOCK;
	if (flags & GN_SAME_ATOM)
		req |= ZNODE_LOCK_DONT_FUSE;

	/* get neighbor's address by using of sibling link, quit while loop
	   (and return) if link is not available. */
	while (1) {
		reiser4_stat_inc_at_level(znode_get_level(node),
					  znode.lock_neighbor_iteration);
		neighbor = GET_NODE_BY_PTR_OFFSET(node, ptr_offset);

		/* return -E_NO_NEIGHBOR if parent or side pointer is NULL or if
		 * node pointed by it is not connected.
		 *
		 * However, GN_ALLOW_NOT_CONNECTED option masks "connected"
		 * check and allows passing reference to not connected znode to
		 * subsequent longterm_lock_znode() call.  This kills possible
		 * busy loop if we are trying to get longterm lock on locked but
		 * not yet connected parent node. */
		if (neighbor == NULL || !((flags & GN_ALLOW_NOT_CONNECTED)
					  || znode_is_connected(neighbor))) {
			return RETERR(-E_NO_NEIGHBOR);
		}

		/* protect it from deletion. */
		zref(neighbor);

		XUNLOCK_TREE(tree, rlocked);

		ret = longterm_lock_znode(result, neighbor, mode, req);

		/* The lock handle obtains its own reference, release the one from above. */
		zput(neighbor);

		XLOCK_TREE(tree, rlocked);

		/* restart if node we got reference to is being
		   invalidated. we should not get reference to this node
		   again.*/
		if (ret == -EINVAL)
			continue;
		if (ret)
			return ret;

		/* check if neighbor link still points to just locked znode;
		   the link could have been changed while the process slept. */
		if (neighbor == GET_NODE_BY_PTR_OFFSET(node, ptr_offset))
			return 0;

		/* znode was locked by mistake; unlock it and restart locking
		   process from beginning. */
		XUNLOCK_TREE(tree, rlocked);
		longterm_unlock_znode(result);
		XLOCK_TREE(tree, rlocked);
	}
}
/* get parent node with longterm lock, accepts GN* flags. */
reiser4_internal int
reiser4_get_parent_flags(lock_handle * result	/* resulting lock handle */,
			 znode * node /* child node */,
			 znode_lock_mode mode /* type of lock: read or write */,
			 int flags /* GN_* flags */)
{
	return UNDER_RW(tree, znode_get_tree(node), read,
			lock_neighbor(result, node, PARENT_PTR_OFFSET, mode,
				      ZNODE_LOCK_HIPRI, flags, 1));
}

/* A wrapper for reiser4_get_parent_flags(). */
reiser4_internal int
reiser4_get_parent(lock_handle * result	/* resulting lock
					   * handle */ ,
		   znode * node /* child node */ ,
		   znode_lock_mode mode /* type of lock: read or write */ ,
		   int only_connected_p	/* if this is true, parent is
					 * only returned when it is
					 * connected. If parent is
					 * unconnected, -E_NO_NEIGHBOR is
					 * returned. Normal users should
					 * pass 1 here. Only during carry
					 * we want to access still
					 * unconnected parents. */ )
{
	assert("umka-238", znode_get_tree(node) != NULL);

	return reiser4_get_parent_flags(result, node, mode,
					only_connected_p ? 0 : GN_ALLOW_NOT_CONNECTED);
}

/* wrapper function to lock right or left neighbor depending on GN_GO_LEFT
   bit in @flags parameter  */
/* Audited by: umka (2002.06.14) */
static inline int
lock_side_neighbor(lock_handle * result,
		   znode * node,
		   znode_lock_mode mode,
		   int flags, int rlocked)
{
	int ret;
	int ptr_offset;
	znode_lock_request req;

	if (flags & GN_GO_LEFT) {
		ptr_offset = LEFT_PTR_OFFSET;
		req = ZNODE_LOCK_LOPRI;
	} else {
		ptr_offset = RIGHT_PTR_OFFSET;
		req = ZNODE_LOCK_HIPRI;
	}

	ret = lock_neighbor(result, node, ptr_offset, mode, req, flags, rlocked);

	if (ret == -E_NO_NEIGHBOR)	/* if we walk left or right -E_NO_NEIGHBOR does not
				   * guarantee that neighbor is absent in the
				   * tree; in this case we return -ENOENT --
				   * means neighbor at least not found in
				   * cache */
		return RETERR(-ENOENT);

	return ret;
}

#if REISER4_DEBUG_SIBLING_LIST
int check_sibling_list(znode * node)
{
	znode *scan;
	znode *next;

	assert("nikita-3283", LOCK_CNT_GTZ(write_locked_tree));

	if (node == NULL)
		return 1;

	if (ZF_ISSET(node, JNODE_RIP))
		return 1;

	assert("nikita-3270", node != NULL);
	assert("nikita-3269", rw_tree_is_write_locked(znode_get_tree(node)));

	for (scan = node; znode_is_left_connected(scan); scan = next) {
		next = scan->left;
		if (next != NULL && !ZF_ISSET(next, JNODE_RIP)) {
			assert("nikita-3271", znode_is_right_connected(next));
			assert("nikita-3272", next->right == scan);
		} else
			break;
	}
	for (scan = node; znode_is_right_connected(scan); scan = next) {
		next = scan->right;
		if (next != NULL && !ZF_ISSET(next, JNODE_RIP)) {
			assert("nikita-3273", znode_is_left_connected(next));
			assert("nikita-3274", next->left == scan);
		} else
			break;
	}
	return 1;
}
#endif

/* Znode sibling pointers maintenence. */

/* Znode sibling pointers are established between any neighbored nodes which are
   in cache.  There are two znode state bits (JNODE_LEFT_CONNECTED,
   JNODE_RIGHT_CONNECTED), if left or right sibling pointer contains actual
   value (even NULL), corresponded JNODE_*_CONNECTED bit is set.

   Reiser4 tree operations which may allocate new znodes (CBK, tree balancing)
   take care about searching (hash table lookup may be required) of znode
   neighbors, establishing sibling pointers between them and setting
   JNODE_*_CONNECTED state bits. */

/* adjusting of sibling pointers and `connected' states for two
   neighbors; works if one neighbor is NULL (was not found). */

/* FIXME-VS: this is unstatic-ed to use in tree.c in prepare_twig_cut */
reiser4_internal void
link_left_and_right(znode * left, znode * right)
{
	assert("nikita-3275", check_sibling_list(left));
	assert("nikita-3275", check_sibling_list(right));

	if (left != NULL) {
		if (left->right == NULL) {
			left->right = right;
			ZF_SET(left, JNODE_RIGHT_CONNECTED);
		} else if (ZF_ISSET(left->right, JNODE_HEARD_BANSHEE)) {
			left->right->left = NULL;
			left->right = right;
			ZF_SET(left, JNODE_RIGHT_CONNECTED);
		} else
			/*
			 * there is a race condition in renew_sibling_link()
			 * and assertions below check that it is only one
			 * there. Thread T1 calls renew_sibling_link() without
			 * GN_NO_ALLOC flag. zlook() doesn't find neighbor
			 * node, but before T1 gets to the
			 * link_left_and_right(), another thread T2 creates
			 * neighbor node and connects it. check for
			 * left->right == NULL above protects T1 from
			 * overwriting correct left->right pointer installed
			 * by T2.
			 */
			assert("nikita-3302",
			       right == NULL || left->right == right);
	}
	if (right != NULL) {
		if (right->left == NULL) {
			right->left = left;
			ZF_SET(right, JNODE_LEFT_CONNECTED);
		} else if (ZF_ISSET(right->left, JNODE_HEARD_BANSHEE)) {
			right->left->right = NULL;
			right->left = left;
			ZF_SET(right, JNODE_LEFT_CONNECTED);
		} else
			assert("nikita-3303",
			       left == NULL || right->left == left);
	}
	assert("nikita-3275", check_sibling_list(left));
	assert("nikita-3275", check_sibling_list(right));
}

/* Audited by: umka (2002.06.14) */
static void
link_znodes(znode * first, znode * second, int to_left)
{
	if (to_left)
		link_left_and_right(second, first);
	else
		link_left_and_right(first, second);
}

/* getting of next (to left or to right, depend on gn_to_left bit in flags)
   coord's unit position in horizontal direction, even across node
   boundary. Should be called under tree lock, it protects nonexistence of
   sibling link on parent level, if lock_side_neighbor() fails with
   -ENOENT. */
static int
far_next_coord(coord_t * coord, lock_handle * handle, int flags)
{
	int ret;
	znode *node;
	reiser4_tree *tree;

	assert("umka-243", coord != NULL);
	assert("umka-244", handle != NULL);

	handle->owner = NULL;	/* mark lock handle as unused */

	ret = (flags & GN_GO_LEFT) ? coord_prev_unit(coord) : coord_next_unit(coord);
	if (!ret)
		return 0;

	ret = lock_side_neighbor(handle, coord->node, ZNODE_READ_LOCK, flags, 0);
	if (ret)
		return ret;

	node = handle->node;
	tree = znode_get_tree(node);
	WUNLOCK_TREE(tree);

	coord_init_zero(coord);

	/* We avoid synchronous read here if it is specified by flag. */
	if ((flags & GN_ASYNC) && znode_page(handle->node) == NULL) {
		ret = jstartio(ZJNODE(handle->node));
		if (!ret)
			ret = -E_REPEAT;
		goto error_locked;
	}

	/* corresponded zrelse() should be called by the clients of
	   far_next_coord(), in place when this node gets unlocked. */
	ret = zload(handle->node);
	if (ret)
		goto error_locked;

	if (flags & GN_GO_LEFT)
		coord_init_last_unit(coord, node);
	else
		coord_init_first_unit(coord, node);

	if (0) {
 error_locked:
		longterm_unlock_znode(handle);
	}
	WLOCK_TREE(tree);
	return ret;
}

/* Very significant function which performs a step in horizontal direction
   when sibling pointer is not available.  Actually, it is only function which
   does it.
   Note: this function does not restore locking status at exit,
   caller should does care about proper unlocking and zrelsing */
static int
renew_sibling_link(coord_t * coord, lock_handle * handle, znode * child, tree_level level, int flags, int *nr_locked)
{
	int ret;
	int to_left = flags & GN_GO_LEFT;
	reiser4_block_nr da;
	/* parent of the neighbor node; we set it to parent until not sharing
	   of one parent between child and neighbor node is detected */
	znode *side_parent = coord->node;
	reiser4_tree *tree = znode_get_tree(child);
	znode *neighbor = NULL;

	assert("umka-245", coord != NULL);
	assert("umka-246", handle != NULL);
	assert("umka-247", child != NULL);
	assert("umka-303", tree != NULL);

	WLOCK_TREE(tree);
	ret = far_next_coord(coord, handle, flags);

	if (ret) {
		if (ret != -ENOENT) {
			WUNLOCK_TREE(tree);
			return ret;
		}
	} else {
		item_plugin *iplug;

		if (handle->owner != NULL) {
			(*nr_locked)++;
			side_parent = handle->node;
		}

		/* does coord object points to internal item? We do not
		   support sibling pointers between znode for formatted and
		   unformatted nodes and return -E_NO_NEIGHBOR in that case. */
		iplug = item_plugin_by_coord(coord);
		if (!item_is_internal(coord)) {
			link_znodes(child, NULL, to_left);
			WUNLOCK_TREE(tree);
			/* we know there can't be formatted neighbor */
			return RETERR(-E_NO_NEIGHBOR);
		}
		WUNLOCK_TREE(tree);

		iplug->s.internal.down_link(coord, NULL, &da);

		if (flags & GN_NO_ALLOC) {
			neighbor = zlook(tree, &da);
		} else {
			neighbor = zget(tree, &da, side_parent, level, GFP_KERNEL);
		}

		if (IS_ERR(neighbor)) {
			ret = PTR_ERR(neighbor);
			return ret;
		}

		if (neighbor)
			/* update delimiting keys */
			set_child_delimiting_keys(coord->node, coord, neighbor);

		WLOCK_TREE(tree);
	}

	if (likely(neighbor == NULL ||
		   (znode_get_level(child) == znode_get_level(neighbor) && child != neighbor)))
		link_znodes(child, neighbor, to_left);
	else {
		warning("nikita-3532",
			"Sibling nodes on the different levels: %i != %i\n",
			znode_get_level(child), znode_get_level(neighbor));
		ret = RETERR(-EIO);
	}

	WUNLOCK_TREE(tree);

	/* if GN_NO_ALLOC isn't set we keep reference to neighbor znode */
	if (neighbor != NULL && (flags & GN_NO_ALLOC))
		/* atomic_dec(&ZJNODE(neighbor)->x_count); */
		zput(neighbor);

	return ret;
}

/* This function is for establishing of one side relation. */
/* Audited by: umka (2002.06.14) */
static int
connect_one_side(coord_t * coord, znode * node, int flags)
{
	coord_t local;
	lock_handle handle;
	int nr_locked;
	int ret;

	assert("umka-248", coord != NULL);
	assert("umka-249", node != NULL);

	coord_dup_nocheck(&local, coord);

	init_lh(&handle);

	ret = renew_sibling_link(&local, &handle, node, znode_get_level(node), flags | GN_NO_ALLOC, &nr_locked);

	if (handle.owner != NULL) {
		/* complementary operations for zload() and lock() in far_next_coord() */
		zrelse(handle.node);
		longterm_unlock_znode(&handle);
	}

	/* we catch error codes which are not interesting for us because we
	   run renew_sibling_link() only for znode connection. */
	if (ret == -ENOENT || ret == -E_NO_NEIGHBOR)
		return 0;

	return ret;
}

/* if @child is not in `connected' state, performs hash searches for left and
   right neighbor nodes and establishes horizontal sibling links */
/* Audited by: umka (2002.06.14), umka (2002.06.15) */
reiser4_internal int
connect_znode(coord_t * parent_coord, znode * child)
{
	reiser4_tree *tree = znode_get_tree(child);
	int ret = 0;

	assert("zam-330", parent_coord != NULL);
	assert("zam-331", child != NULL);
	assert("zam-332", parent_coord->node != NULL);
	assert("umka-305", tree != NULL);

	/* it is trivial to `connect' root znode because it can't have
	   neighbors */
	if (znode_above_root(parent_coord->node)) {
		child->left = NULL;
		child->right = NULL;
		ZF_SET(child, JNODE_LEFT_CONNECTED);
		ZF_SET(child, JNODE_RIGHT_CONNECTED);
		return 0;
	}

	/* load parent node */
	coord_clear_iplug(parent_coord);
	ret = zload(parent_coord->node);

	if (ret != 0)
		return ret;

	/* protect `connected' state check by tree_lock */
	RLOCK_TREE(tree);

	if (!znode_is_right_connected(child)) {
		RUNLOCK_TREE(tree);
		/* connect right (default is right) */
		ret = connect_one_side(parent_coord, child, GN_NO_ALLOC);
		if (ret)
			goto zrelse_and_ret;

		RLOCK_TREE(tree);
	}

	ret = znode_is_left_connected(child);

	RUNLOCK_TREE(tree);

	if (!ret) {
		ret = connect_one_side(parent_coord, child, GN_NO_ALLOC | GN_GO_LEFT);
	} else
		ret = 0;

zrelse_and_ret:
	zrelse(parent_coord->node);

	return ret;
}

/* this function is like renew_sibling_link() but allocates neighbor node if
   it doesn't exist and `connects' it. It may require making two steps in
   horizontal direction, first one for neighbor node finding/allocation,
   second one is for finding neighbor of neighbor to connect freshly allocated
   znode. */
/* Audited by: umka (2002.06.14), umka (2002.06.15) */
static int
renew_neighbor(coord_t * coord, znode * node, tree_level level, int flags)
{
	coord_t local;
	lock_handle empty[2];
	reiser4_tree *tree = znode_get_tree(node);
	znode *neighbor = NULL;
	int nr_locked = 0;
	int ret;

	assert("umka-250", coord != NULL);
	assert("umka-251", node != NULL);
	assert("umka-307", tree != NULL);
	assert("umka-308", level <= tree->height);

	/* umka (2002.06.14)
	   Here probably should be a check for given "level" validness.
	   Something like assert("xxx-yyy", level < REAL_MAX_ZTREE_HEIGHT);
	*/

	coord_dup(&local, coord);

	ret = renew_sibling_link(&local, &empty[0], node, level, flags & ~GN_NO_ALLOC, &nr_locked);
	if (ret)
		goto out;

	/* tree lock is not needed here because we keep parent node(s) locked
	   and reference to neighbor znode incremented */
	neighbor = (flags & GN_GO_LEFT) ? node->left : node->right;

	ret = UNDER_RW(tree, tree, read, znode_is_connected(neighbor));

	if (ret) {
		ret = 0;
		goto out;
	}

	ret = renew_sibling_link(&local, &empty[nr_locked], neighbor, level, flags | GN_NO_ALLOC, &nr_locked);
	/* second renew_sibling_link() call is used for znode connection only,
	   so we can live with these errors */
	if (-ENOENT == ret || -E_NO_NEIGHBOR == ret)
		ret = 0;

out:

	for (--nr_locked; nr_locked >= 0; --nr_locked) {
		zrelse(empty[nr_locked].node);
		longterm_unlock_znode(&empty[nr_locked]);
	}

	if (neighbor != NULL)
		/* decrement znode reference counter without actually
		   releasing it. */
		atomic_dec(&ZJNODE(neighbor)->x_count);

	return ret;
}

/*
   reiser4_get_neighbor() -- lock node's neighbor.

   reiser4_get_neighbor() locks node's neighbor (left or right one, depends on
   given parameter) using sibling link to it. If sibling link is not available
   (i.e. neighbor znode is not in cache) and flags allow read blocks, we go one
   level up for information about neighbor's disk address. We lock node's
   parent, if it is common parent for both 'node' and its neighbor, neighbor's
   disk address is in next (to left or to right) down link from link that points
   to original node. If not, we need to lock parent's neighbor, read its content
   and take first(last) downlink with neighbor's disk address.  That locking
   could be done by using sibling link and lock_neighbor() function, if sibling
   link exists. In another case we have to go level up again until we find
   common parent or valid sibling link. Then go down
   allocating/connecting/locking/reading nodes until neighbor of first one is
   locked.

   @neighbor:  result lock handle,
   @node: a node which we lock neighbor of,
   @lock_mode: lock mode {LM_READ, LM_WRITE},
   @flags: logical OR of {GN_*} (see description above) subset.

   @return: 0 if success, negative value if lock was impossible due to an error
   or lack of neighbor node.
*/

/* Audited by: umka (2002.06.14), umka (2002.06.15) */
reiser4_internal int
reiser4_get_neighbor (
	lock_handle * neighbor, znode * node, znode_lock_mode lock_mode, int flags)
{
	reiser4_tree *tree = znode_get_tree(node);
	lock_handle path[REAL_MAX_ZTREE_HEIGHT];

	coord_t coord;

	tree_level base_level;
	tree_level h = 0;
	int ret;

	assert("umka-252", tree != NULL);
	assert("umka-253", neighbor != NULL);
	assert("umka-254", node != NULL);

	base_level = znode_get_level(node);

	assert("umka-310", base_level <= tree->height);

	coord_init_zero(&coord);

again:
	/* first, we try to use simple lock_neighbor() which requires sibling
	   link existence */
	ret = UNDER_RW(tree, tree, read,
		       lock_side_neighbor(neighbor, node, lock_mode, flags, 1));

	if (!ret) {
		/* load znode content if it was specified */
		if (flags & GN_LOAD_NEIGHBOR) {
			ret = zload(node);
			if (ret)
				longterm_unlock_znode(neighbor);
		}
		return ret;
	}

	/* only -ENOENT means we may look upward and try to connect
	   @node with its neighbor (if @flags allow us to do it) */
	if (ret != -ENOENT || !(flags & GN_CAN_USE_UPPER_LEVELS))
		return ret;

	/* before establishing of sibling link we lock parent node; it is
	   required by renew_neighbor() to work.  */
	init_lh(&path[0]);
	ret = reiser4_get_parent(&path[0], node, ZNODE_READ_LOCK, 1);
	if (ret)
		return ret;
	if (znode_above_root(path[0].node)) {
		longterm_unlock_znode(&path[0]);
		return RETERR(-E_NO_NEIGHBOR);
	}

	while (1) {
		znode *child = (h == 0) ? node : path[h - 1].node;
		znode *parent = path[h].node;

		reiser4_stat_inc_at_level(h + LEAF_LEVEL, sibling_search);
		ret = zload(parent);
		if (ret)
			break;

		ret = find_child_ptr(parent, child, &coord);

		if (ret) {
			zrelse(parent);
			break;
		}

		/* try to establish missing sibling link */
		ret = renew_neighbor(&coord, child, h + base_level, flags);

		zrelse(parent);

		switch (ret) {
		case 0:
			/* unlocking of parent znode prevents simple
			   deadlock situation */
			done_lh(&path[h]);

			/* depend on tree level we stay on we repeat first
			   locking attempt ...  */
			if (h == 0)
				goto again;

			/* ... or repeat establishing of sibling link at
			   one level below. */
			--h;
			break;

		case -ENOENT:
			/* sibling link is not available -- we go
			   upward. */
			init_lh(&path[h + 1]);
			ret = reiser4_get_parent(&path[h + 1], parent, ZNODE_READ_LOCK, 1);
			if (ret)
				goto fail;
			++h;
			if (znode_above_root(path[h].node)) {
				ret = RETERR(-E_NO_NEIGHBOR);
				goto fail;
			}
			break;

		case -E_DEADLOCK:
			/* there was lock request from hi-pri locker. if
			   it is possible we unlock last parent node and
			   re-lock it again. */
			while (check_deadlock()) {
				if (h == 0)
					goto fail;

				done_lh(&path[--h]);
			}

			break;

		default:	/* other errors. */
			goto fail;
		}
	}
fail:
	ON_DEBUG(check_lock_node_data(node));
	ON_DEBUG(check_lock_data());

	/* unlock path */
	do {
		longterm_unlock_znode(&path[h]);
		--h;
	} while (h + 1 != 0);

	return ret;
}

/* remove node from sibling list */
/* Audited by: umka (2002.06.14) */
reiser4_internal void
sibling_list_remove(znode * node)
{
	assert("umka-255", node != NULL);
	assert("zam-878", rw_tree_is_write_locked(znode_get_tree(node)));
	assert("nikita-3275", check_sibling_list(node));

	if (znode_is_right_connected(node) && node->right != NULL) {
		assert("zam-322", znode_is_left_connected(node->right));
		node->right->left = node->left;
	}
	if (znode_is_left_connected(node) && node->left != NULL) {
		assert("zam-323", znode_is_right_connected(node->left));
		node->left->right = node->right;
	}
	ZF_CLR(node, JNODE_LEFT_CONNECTED);
	ZF_CLR(node, JNODE_RIGHT_CONNECTED);
	ON_DEBUG(node->left = node->right = NULL);
	assert("nikita-3276", check_sibling_list(node));
}

/* disconnect node from sibling list */
reiser4_internal void
sibling_list_drop(znode * node)
{
	znode *right;
	znode *left;

	assert("nikita-2464", node != NULL);
	assert("nikita-3277", check_sibling_list(node));

	right = node->right;
	if (right != NULL) {
		assert("nikita-2465", znode_is_left_connected(right));
		right->left = NULL;
	}
	left = node->left;
	if (left != NULL) {
		assert("zam-323", znode_is_right_connected(left));
		left->right = NULL;
	}
	ZF_CLR(node, JNODE_LEFT_CONNECTED);
	ZF_CLR(node, JNODE_RIGHT_CONNECTED);
	ON_DEBUG(node->left = node->right = NULL);
}

/* Insert new node into sibling list. Regular balancing inserts new node
   after (at right side) existing and locked node (@before), except one case
   of adding new tree root node. @before should be NULL in that case. */
reiser4_internal void
sibling_list_insert_nolock(znode * new, znode * before)
{
	assert("zam-334", new != NULL);
	assert("nikita-3298", !znode_is_left_connected(new));
	assert("nikita-3299", !znode_is_right_connected(new));
	assert("nikita-3300", new->left == NULL);
	assert("nikita-3301", new->right == NULL);
	assert("nikita-3278", check_sibling_list(new));
	assert("nikita-3279", check_sibling_list(before));

	if (before != NULL) {
		assert("zam-333", znode_is_connected(before));
		new->right = before->right;
		new->left = before;
		if (before->right != NULL)
			before->right->left = new;
		before->right = new;
	} else {
		new->right = NULL;
		new->left = NULL;
	}
	ZF_SET(new, JNODE_LEFT_CONNECTED);
	ZF_SET(new, JNODE_RIGHT_CONNECTED);
	assert("nikita-3280", check_sibling_list(new));
	assert("nikita-3281", check_sibling_list(before));
}

struct tw_handle {
	/* A key for tree walking (re)start, updated after each successful tree
	 * node processing */
	reiser4_key            start_key;
	/* A tree traversal current position. */
	tap_t                  tap;
	/* An externally supplied pair of functions for formatted and
	 * unformatted nodes processing. */
	struct tree_walk_actor * actor;
	/* It is passed to actor functions as is. */
	void                 * opaque;
	/* A direction of a tree traversal: 1 if going from right to left. */
	int                    go_left:1;
	/* "Done" flag */
	int                    done:1;
	/* Current node was processed completely */
	int                    node_completed:1;
};

/* it locks the root node, handles the restarts inside */
static int lock_tree_root (lock_handle * lock, znode_lock_mode mode)
{
	int ret;

	reiser4_tree * tree = current_tree;
	lock_handle uber_znode_lock;
	znode * root;

	init_lh(&uber_znode_lock);
 again:

	ret = get_uber_znode(tree, mode, ZNODE_LOCK_HIPRI, &uber_znode_lock);
	if (ret)
		return ret;

	root = zget(tree, &tree->root_block, uber_znode_lock.node, tree->height, GFP_KERNEL);
	if (IS_ERR(root)) {
		done_lh(&uber_znode_lock);
		return PTR_ERR(root);
	}

	ret = longterm_lock_znode(lock, root, ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI);

	zput(root);
	done_lh(&uber_znode_lock);

	if (ret == -E_DEADLOCK)
		goto again;

	return ret;
}

/* Update the handle->start_key by the first key of the node is being
 * processed. */
static int update_start_key(struct tw_handle * h)
{
	int ret;

	ret = tap_load(&h->tap);
	if (ret == 0) {
		unit_key_by_coord(h->tap.coord, &h->start_key);
		tap_relse(&h->tap);
	}
	return ret;
}

/* Move tap to the next node, load it. */
static int go_next_node (struct tw_handle * h, lock_handle * lock, const coord_t * coord)
{
	int ret;

	assert ("zam-948", ergo (coord != NULL, lock->node == coord->node));

	tap_relse(&h->tap);

	ret = tap_move(&h->tap, lock);
	if (ret)
		return ret;

	ret = tap_load(&h->tap);
	if (ret)
		goto error;

	if (coord)
		coord_dup(h->tap.coord, coord);
	else {
		if (h->go_left)
			coord_init_last_unit(h->tap.coord, lock->node);
		else
			coord_init_first_unit(h->tap.coord, lock->node);
	}

	if (h->actor->process_znode != NULL) {
		ret = (h->actor->process_znode)(&h->tap, h->opaque);
		if (ret)
			goto error;
	}

	ret = update_start_key(h);

 error:
	done_lh(lock);
	return ret;
}

static void next_unit (struct tw_handle * h)
{
	if (h->go_left)
		h->node_completed = coord_prev_unit(h->tap.coord);
	else
		h->node_completed = coord_next_unit(h->tap.coord);
}


/* Move tree traversal position (which is embedded into tree_walk_handle) to the
 * parent of current node (h->lh.node). */
static int tw_up (struct tw_handle * h)
{
	coord_t coord;
	lock_handle lock;
	load_count load;
	int ret;

	init_lh(&lock);
	init_load_count(&load);

	do {
		ret = reiser4_get_parent(&lock, h->tap.lh->node, ZNODE_WRITE_LOCK, 0);
		if (ret)
			break;
		if (znode_above_root(lock.node)) {
			h->done = 1;
			break;
		}
		ret = incr_load_count_znode(&load, lock.node);
		if (ret)
			break;
		ret = find_child_ptr(lock.node, h->tap.lh->node, &coord);
		if (ret)
			break;
		ret = go_next_node(h, &lock, &coord);
		if (ret)
			break;
		next_unit(h);
	} while (0);

	done_load_count(&load);
	done_lh(&lock);

	return ret;
}

/* Move tree traversal position to the child of current node pointed by
 * h->tap.coord.  */
static int tw_down(struct tw_handle * h)
{
	reiser4_block_nr block;
	lock_handle lock;
	znode * child;
	item_plugin * iplug;
	tree_level level = znode_get_level(h->tap.lh->node);
	int ret;

	assert ("zam-943", item_is_internal(h->tap.coord));

	iplug = item_plugin_by_coord(h->tap.coord);
	iplug->s.internal.down_link(h->tap.coord, NULL, &block);
	init_lh(&lock);

	do {
		child = zget(current_tree, &block, h->tap.lh->node, level - 1, GFP_KERNEL);
		if (IS_ERR(child))
			return PTR_ERR(child);
		ret = connect_znode(h->tap.coord, child);
		if (ret)
			break;
		ret = longterm_lock_znode(&lock, child, ZNODE_WRITE_LOCK, 0);
		if (ret)
			break;
		set_child_delimiting_keys(h->tap.coord->node, h->tap.coord, child);
		ret = go_next_node (h, &lock, NULL);
	} while(0);

	zput(child);
	done_lh(&lock);
	return ret;
}
/* Traverse the reiser4 tree until either all tree traversing is done or an
 * error encountered (including recoverable ones as -E_DEADLOCK or -E_REPEAT).  The
 * @actor function is able to stop tree traversal by returning an appropriate
 * error code. */
static int tw_by_handle (struct tw_handle * h)
{
	int ret;
	lock_handle next_lock;

	ret = tap_load(&h->tap);
	if (ret)
		return ret;

	init_lh (&next_lock);

	while (!h->done) {
		tree_level level;

		if (h->node_completed) {
			h->node_completed = 0;
			ret = tw_up(h);
			if (ret)
				break;
			continue;
		}

		assert ("zam-944", coord_is_existing_unit(h->tap.coord));
		level = znode_get_level(h->tap.lh->node);

		if (level == LEAF_LEVEL) {
			h->node_completed = 1;
			continue;
		}

		if (item_is_extent(h->tap.coord)) {
			if (h->actor->process_extent != NULL) {
				ret = (h->actor->process_extent)(&h->tap, h->opaque);
				if (ret)
					break;
			}
			next_unit(h);
			continue;
		}

		ret = tw_down(h);
		if (ret)
			break;
	}

	done_lh(&next_lock);
	return ret;
}

/* Walk the reiser4 tree in parent-first order */
reiser4_internal int
tree_walk (const reiser4_key *start_key, int go_left, struct tree_walk_actor * actor, void * opaque)
{
	coord_t coord;
	lock_handle lock;
	struct tw_handle handle;

	int ret;

	assert ("zam-950", actor != NULL);

	handle.actor = actor;
	handle.opaque = opaque;
	handle.go_left = !!go_left;
	handle.done = 0;
	handle.node_completed = 0;

	init_lh(&lock);

	if (start_key == NULL) {
		if (actor->before) {
			ret = actor->before(opaque);
			if (ret)
				return ret;
		}

		ret = lock_tree_root(&lock, ZNODE_WRITE_LOCK);
		if (ret)
			return ret;
		ret = zload(lock.node);
		if (ret)
			goto done;

		if (go_left)
			coord_init_last_unit(&coord, lock.node);
		else
			coord_init_first_unit_nocheck(&coord, lock.node);

		zrelse(lock.node);
		goto no_start_key;
	} else
		handle.start_key = *start_key;

	do {
		if (actor->before) {
			ret = actor->before(opaque);
			if (ret)
				return ret;
		}

		ret = coord_by_key(current_tree, &handle.start_key, &coord, &lock, ZNODE_WRITE_LOCK,
				   FIND_MAX_NOT_MORE_THAN, TWIG_LEVEL, LEAF_LEVEL, 0, NULL);
		if (ret != CBK_COORD_FOUND)
			break;
	no_start_key:
		tap_init(&handle.tap, &coord, &lock, ZNODE_WRITE_LOCK);

		ret = update_start_key(&handle);
		if (ret) {
			tap_done(&handle.tap);
			break;
		}
		ret = tw_by_handle(&handle);
		tap_done (&handle.tap);

	} while (!handle.done && (ret == -E_DEADLOCK || ret == -E_REPEAT));

	done:
	done_lh(&lock);
	return ret;
}


/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
