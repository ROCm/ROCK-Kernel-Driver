









/*

The use of atomic commits dramatically impacts the use of LRU as the
basis for page cleaning (though using it for clean page discarding is
still effective.)

The use of write clustering dramatically impacts the use of LRU as the
basis for page cleaning.

ReiserFS v4 uses both.

We will not use LRU in v4.0 of reiserfs, and then in later versions we
may gradually partially reintroduce it.

Optimizations to make on flush:

* block (re)allocation

* tail conversion

* extent formation

* node repacking

* wandering log definition


Memory Pressure:

There are kinds of memory pressure:

* general lack of memory for processes requesting it

* too much dirty memory

* dirty memory is too old and should be more permanently preserved on disk

* particular page needs freeing for DMA setup

[All programmers should understand that I expect strict observance of the
following taboo: you will not add an unnecessary copying of data, while coding
this.  I cannot understand why there is resistance to this, but I keep seeing
code which ignores this.]


Unlike clean pages, dirty memory must be written to disk before being
freed for other use.  It also may require processing that will require
more memory before it can be cleaned by writing it to disk.  This
processing makes it vulnerable to deadlocks in extreme cases.
Precisely reserving enough memory to allow that extra processing
without deadlock is often difficult.

reiser4 limits its usage of dirty pages to 75%, which is enough to
ensure that the extra processing will not cause the system to run out
of memory.  More safeguards are possible, including letting commit
choose to swap, but we will wait until this rather simple mechanism
has a problem in practice before elaborating it.

reiser4 supports the reiserfs_flush(page) command for cleaning pages

If the Linux VM system was properly designed, it would be based upon
memory sub-managers each reflecting some common principles of
responding to pressure that is in proportion to their size.  Linux
used to have multiple caches, these caches had no understand of how
large they were, they made no attempt to proportionalize the pressure
upon each cache, and their management was generally badly designed
without any effective mechanism for ensuring that the caches did not
get out of balance with each other.  From this it was concluded that
there should be only one unified cache, rather than designing an
effective mechanism for expressing to each subcache a sense of
pressure in proportion to the size of the subcache, and requiring that
the subcache embody some effective mechanism for responding to that
sense of pressure.

The unified cache is indeed better than badly designed multiple
caches.  It does however perform very poorly at handling caches of
objects that are not page sized.

Linus says it already has a subcache manager design, we just need to
use writepage.  Ok, fine, we will be the first subcache.

So, understand that in reiserfs, writepage does not write pages, it
pressures the reiserfs memory manager, and understand that the place a
page has on the various mm lists does not determine when it gets
written out, it merely determines when it triggers the next pressure
on the reiserfs memory manager.

What reiser4 does is interpret pressure on a page as pressure on a
subcache within reiserfs.

Write clustering, transaction commits, objects whose value to cache is
out of proportion to the number of bytes consumed by them, caches
whose working set size and pattern of access is known to the
application, and those occasions when other factors knowable to the
filesystem or application but not the OS generally are important to
deciding what to eject, and objects much smaller than a page with no
correlation of references for objects on the same page, or larger than
a page with a complete correlation between their pages, are good example of when cache
submanagers should be employed.

 */

/* You should read crypt.c and then return. */
/* You should read block_alloc.c and then return. */

current_leaf = find_leftmost_leaf_in_slum();
/* current_leaf is locked */
parent = parent(current_leaf);
/* parent is locked */

if (is_relocate_set(current_leaf))
{
  dirty(parent);
}

if (is_dirty(parent))
{
  squeeze_level_left(parent);
  /* this can create an enormous recursive chain that could overflow
   the kernel stack, hmmmm..... */
  flush_all_other_child_slums(parent, min_key(current_leaf));
}
else
{
  unlock(parent);
}
/* parent is unlocked by squeeze_level_left, and squeezing may have
   changed the parent of current_leaf */

parent = parent(current_leaf);
/* parent is locked */
if (leftmost_child(parent) == current_leaf)
     allocate(parent);

/* ok, now we are ready to proceed through slum on the leaf level */
next_leaf = get_right_neighbor_in_slum_level(current_leaf);
/* next_leaf is locked or null */

/* need to review locking in the below */
while(next_leaf)
{
  if (is_formatted(current_leaf) && is_formatted(next_leaf))
    {
      squeeze_left(current_leaf, next_leaf);
      if (is_empty(next_leaf))
	{
	  delete_node(next_leaf);
	  next_leaf = get_right_neighbor_in_slum_level(current_leaf);
	  check_and_handle_parent_change();
	  continue;
	}
    }
  if (is_unformatted(current_leaf))
    /* allocate or reallocate */
    allocate_extent_in_parent_if_needed(current_leaf);
  /* the above may change the parent */
  check_and_handle_parent_change();
  allocate(current_leaf);
  next_leaf = get_right_neighbor_in_slum_level(current_leaf);
  check_and_handle_parent_change();


}

/* this means squeeze it as well as allocate it */
handle_non_leaf_end_of_slum();

check_and_handle_parent_change()
{
if ( (new_parent = parent(current_leaf)) != parent)
  squeeze_left(parent, new_parent(parent));
 else
   return;

/* the line above can change who the parent is so retest... */
  if((new_parent = parent(current_leaf)) != parent)
    {
      parent = new_parent;
      if (leftmost_child(parent) != current_leaf)
	reiser_panic("reiser-2071: this needs recoding to handle this case");
      allocate_node(parent);
				/* allocating other ancestors left for josh */

      /* our new parent might not be well packed, and we want
	 it to be well packed even if our slum never reaches its edge
	 so we... */
      squeeze_left(parent, right_neighbor(parent));
    }
}


################################################################################


The Problem:

We need to know the relocate set in order to perform a left-to-right
parent-first allocate-and-squeeze traversal over a dirty sub-tree.  We
could make this decision during the allocate-and-squeeze pass, but in
that case we would discover a node is dirty when we have already
passed over its position in the parent-first ordering.  In otherwords,
we would discover this information too late to be useful.

The Several-Pass Solution:

It is possible to construct this relocate information at flush time by
scanning the tree, but it means at least two passes over the tree.
Using several passes has an advantage: we can then choose overwrite
when the "optimal location" is part of the atom's own preserve set.
This requires knowing the (partial) set of blocks being flushed before
allocation begins.  This strategy was initially proposed by Hans,
before we realized it would require multiple passes.

The Solution:

Maintain an active count of dirty children for each node.  This allows
us to mark a node dirty whenever its dirty count becomes >= 2 because
at that point overwriting the parent reduces the total number of
blocks written to disk.  How much of a counter is needed?  In order to
keep track of additions and subtractions to this count, a counter at
the same size as our znode's c_count field is needed.  If this value
were only ever incremented, then we could use a single bit (0 = no
dirty children, 1 = single dirty child, otherwise mark dirty &
relocate).  But since a node may have dirty children added while
flushes are active (it can happen, right?) this requires more than just
a bit.  I worry about the complexity of maintaining this dirty count,
but I fear that the parent-first allocation policy will not succeed
without knowing before-hand all the dirty nodes it must consider.

The Algorithm:

Given the above assumption, that nodes are marked dirty whenever they
should be relocated (i.e., that the flush algorithm does not make this
decision during as part of its passing over the tree).

Starting from some leaf node, find the greatest dirty ancestor, defined as the
least (i.e., lowest-level) ancestor of a node without a dirty parent.  The
greatest dirty ancestor will be overwritten, therefore its preceding node in
the parent-first order should not be considered.

    [Dead text: If the greatest dirty ancestor is NOT the leftmost child of
    its own parent (and not the root node), there may be a dirty
    parent-first-ordered node in a subtree to the left of this one.  In those
    cases, from the greatest dirty ancestor, find the leftmost in-memory
    descendant.  If the leftmost descendant is dirty, consider its left
    neighbor.  If the neighbor is also dirty, repeat the steps of this
    paragraph starting at that node (i.e., find the new greatest dirty
    ancestor).]

Pseudo-code for this is:

/* Starting from a node, find the greatest dirty ancestor. */
jnode* greatest_dirty_ancestor (jnode *node)
{
  while (! is_root_node (node)) {
     znode *parent = get_parent (node);

     if (! znode_is_dirty (parent)) {
         break;
     }

     node = parent;
  }
  return node;
}

Now we have found the greatest dirty ancestor from which to begin allocating
and squeezing.  From this point we will traverse all descendants of the
greatest dirty ancestor, in parent-first order, allocating blocks and
squeezing nodes in the following order.  Squeezing must be performed in a
bottom-up, left-to-right order, whereas allocation occurs in parent-first
order.  The following pseudo-code accomplishes both at once:

########################################################################

Problems with above to be addressed:

Nikita suggests squeezing all the formatted nodes of a twig before allocating
its extents, thereby increasing room for extents to "inflate".

########################################################################

/* A function to find the parent-first-preceder of a node, although
 * there may not be enough nodes in memory to actually compute this.
 * In that case, pick something else.  If node is leftmost child of
 * its parent, return its parent's block number.  Otherwise if node
 * is a leaf node, return its left neighbor.  Finally, return the
 * block number of the parent's left neighbor's rightmost descendent
 * (which may not be in memory).  In the actual implementation of the
 * parent-first traversal below, we can optimize this (because we
 * know the result most of the time). */
blocknr parent_first_preceder_of (jnode *node) { ... }

/* A parent-first recursive tree traversal, allocate and squeeze.
 * This is called on the greatest dirty ancestor of a region to be
 * flushed.
 */
void allocate_and_squeeze_parent_first (jnode *node)
{
  /* Stop recursion if its not dirty, meaning don't allocate children either.
   * Children might be dirty but there is an overwrite below this level
   * or else this node would be dirty. */
  if (! is_dirty (node)) {
    return;
  }

  /* Allocate (parent) first. */
  allocate_node (node, parent_first_preceder_of (node));

  if (jnode_is_unformatted (node)) {
    /* We got here because the parent (twig) of an unformatted node is
     * not being relocated.  Otherwise this recursion does not descend
     * to unformatted nodes. */
     return;
  }

  /* Recursive case: */
  if (jnode_get_level (node) > LEAF_LEVEL) {

    for (each_item_left_to_right (node)) {

      if (is_extent_item (item) && extent_item_is_dirty (item)) {
         allocate_extent_item (item);
      } else if (is_internal_item (item) && jnode_is_dirty (internal_item_child (item))) {
         allocate_and_squeeze_parent_first (internal_item_child (item));
      }
    }
  }

  /* Squeeze a node: note that this makes the "one big memcpy"
   * approach somewhat more difficult, but its still possible. */
  while (not_empty (node) && jnode_is_formatted (node->right) && is_dirty (node->right)) {

    item = first_item_of (node->right);

    if (is_extent_item (item) && extent_item_is_dirty (item)) {
       allocate_extent_item_into (item, node);
    } else if (can_shift_into (item, node)) {
       shift_item (item, node);
    }
  }
}

########################################################################
########################################################################

########################################################################
########################################################################

Hans says:

Relocate parent if leftmost child is also relocated

Relocate if leftmost-child of parent.

Ignore the "always relocate children if two children of a node are dirty"
idea.

Rather than scan left at the leaf level, why not jump to parent, check
left-most child dirty, and stop?

########################################################################
########################################################################

Dead pseudo code, older stuff:

########################################################################
########################################################################


Problem: The root of a subtree gets overwritten, so the subtree to the left
will not follow in parent-first order.  That would simplify things.  Killed
this code:

/* Starting at a node, find the greatest dirty parent, then see if it
 * has a preceding dirty node on the leaf of the subtree to its left. */
void find_maximal_dirty_ancestor (jnode *node)
{
 repeat:
  node = greatest_dirty_ancestor (node)

  /* End search at the root node or if the node is the leftmost child
   * of its parent, in which case the left-of-leftmost-descendent does
   * not precede it in parent first order, its parent does in that
   * case. */
  if (! is_root_node (node) && ! leftmost_child_of_parent (node)) {
     jnode *godown = node;

     while (jnode_get_level (godown) > LEAF_LEVEL) {
       /* Iterate downward as long as leftmost nodes in memory (note:
        * they don't have to be dirty). */
       jnode *child = leftmost_child (godown);

       if (child == NULL) {
         return node;
       }

       godown = child;
    }

    /* Reached the leftmost descendant of the maximal dirty node,
     * now see if its left is dirty.  Otherwise return. */
    if ((godown = godown->left) == NULL || ! jnode_is_dirty (godown)) {
      return node;
    }

    /* At this point, "godown" precedes "node" in the parent-first
     * traversal, so search for a new maximal dirty node. */
    node = godown;
    goto repeat;
  }
}

/* Allocate and squeeze starting at the greatest dirty ancestor
 * described above.  Repeat in rightward direction for adjacent
 * subtrees.
 */
void allocate_and_squeeze_parent_first (jnode *node)
{
  jnode *right;

repeat:
  /* Do one sub-tree */
  allocate_and_squeeze_parent_first_subtree (node);

  /* Now try to repeat to the right. */
  right = get_right_neighbor (node);

  if (right != NULL && jnode_is_dirty (right)) {
     node = greatest_dirty_ancestor (right);
     goto repeat;
  }
}


/* The crap below was my first attempt to write this iteratively. */





jnode *maximal_dirty_ancestor = ...;                   /* Computed using above algorithm */
jnode *left_edge[MAX_LEVELS];                          /* Vertical edge of left-to-right scan */
int    top_edge = jnode_get_level (node) - LEAF_LEVEL; /* Highest index to the left_edge array --
                                                        * by subtracting LEAF_LEVEL it becomes 0-origin */

/* Initialize left_edge array entries to NULL, set top edge */
left_edge[top_edge] = maximal_dirty_ancestor;

/* For each node above the leaf level, set the child in left_edge */
for (int level = top_edge; level >= 1; level -= 1) {

  jnode *parent = left_edge[level];

  /* Find its leftmost dirty child. */
  jnode *child  = leftmost_dirty_child (parent);

  /* Its possible that a dirty node could have no dirty children,
   * in which case leave the lower edges NULL. */
  if (child == NULL) { break; }

  left_edge[level-1] = child;
}

/* To store the lowest dirty entry in left_edge[]. */
int current_level = 0;

/* Allocate each node in the left edge. */
for (int level = top_edge; level >= 0 && left_edge[level] != NULL; level -= 1) {

  jnode *node = left_edge[level];

  /* Allocate this node... */
  allocate_node (node, parent_first_preceder_of (node));

  current_level = level;
}

/* Now starting with the current level, squeeze and allocate until finished. */
while (current_level <= top_level) {

  jnode *current_node = left_edge[current_level];

  if (jnode_is_formatted (current_node)) {

    do {

      /* Shift as much as possible. */
      while (node_has_room_to_shift_into (current_node)) {
        if (is_twig_level (current_node)) {
          shift_left (current_node, current_node->right);
        } else {
          allocate_extents_and_shift_left (current_node, current_node->right);
        }
      }

      /* Once it has been tightly packed, allocate it. */
      allocate_node (current_node, parent_first_preceder_of (node));

      current_node = current_node->right;
  }

  current_level += 1;
}
</pre>
