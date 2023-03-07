/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AMDKCL_LINUX_RBTREE_H
#define AMDKCL_LINUX_RBTREE_H

#include <linux/rbtree.h>

#ifndef RB_ROOT_CACHED
/*
 * Leftmost-cached rbtrees.
 *
 * We do not cache the rightmost node based on footprint
 * size vs number of potential users that could benefit
 * from O(1) rb_last(). Just not worth it, users that want
 * this feature can always implement the logic explicitly.
 * Furthermore, users that want to cache both pointers may
 * find it a bit asymmetric, but that's ok.
 */
struct rb_root_cached {
        struct rb_root rb_root;
        struct rb_node *rb_leftmost;
};

#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }
#define rb_first_cached(root) (root)->rb_leftmost

static inline struct rb_node *
rb_erase_cached(struct rb_node *node, struct rb_root_cached *root)
{
        struct rb_node *leftmost = NULL;

        if (root->rb_leftmost == node)
                leftmost = root->rb_leftmost = rb_next(node);

        rb_erase(node, &root->rb_root);

        return leftmost;
}

static inline void rb_insert_color_cached(struct rb_node *node,
                                          struct rb_root_cached *root,
                                          bool leftmost)
{
        if (leftmost)
                root->rb_leftmost = node;
        rb_insert_color(node, &root->rb_root);
}
#endif

#ifndef HAVE_RB_ADD_CACHED
/*
 * The below helper functions use 2 operators with 3 different
 * calling conventions. The operators are related like:
 *
 *      comp(a->key,b) < 0  := less(a,b)
 *      comp(a->key,b) > 0  := less(b,a)
 *      comp(a->key,b) == 0 := !less(a,b) && !less(b,a)
 *
 * If these operators define a partial order on the elements we make no
 * guarantee on which of the elements matching the key is found. See
 * rb_find().
 *
 * The reason for this is to allow the find() interface without requiring an
 * on-stack dummy object, which might not be feasible due to object size.
 */

/**
 * rb_add_cached() - insert @node into the leftmost cached tree @tree
 * @node: node to insert
 * @tree: leftmost cached tree to insert @node into
 * @less: operator defining the (partial) node order
 *
 * Returns @node when it is the new leftmost, or NULL.
 */
static __always_inline struct rb_node *
rb_add_cached(struct rb_node *node, struct rb_root_cached *tree,
              bool (*less)(struct rb_node *, const struct rb_node *))
{
        struct rb_node **link = &tree->rb_root.rb_node;
        struct rb_node *parent = NULL;
        bool leftmost = true;

        while (*link) {
                parent = *link;
                if (less(node, parent)) {
                        link = &parent->rb_left;
                } else {
                        link = &parent->rb_right;
                        leftmost = false;
                }
        }

        rb_link_node(node, parent, link);
        rb_insert_color_cached(node, tree, leftmost);

        return leftmost ? node : NULL;
}
#endif

#endif
