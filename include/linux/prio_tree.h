#ifndef _LINUX_PRIO_TREE_H
#define _LINUX_PRIO_TREE_H
/*
 * Dummy version of include/linux/prio_tree.h, just for this patch:
 * no radix priority search tree whatsoever, just implement interfaces
 * using the old lists.
 */

struct prio_tree_root {
	struct list_head	list;
};

struct prio_tree_iter {
	int			not_used_yet;
};

#define INIT_PRIO_TREE_ROOT(ptr)	\
do {					\
	INIT_LIST_HEAD(&(ptr)->list);	\
} while (0)				\

static inline int prio_tree_empty(const struct prio_tree_root *root)
{
	return list_empty(&root->list);
}

#endif /* _LINUX_PRIO_TREE_H */
