#ifndef _LINUX_PRIO_TREE_H
#define _LINUX_PRIO_TREE_H

struct prio_tree_node {
	struct prio_tree_node	*left;
	struct prio_tree_node	*right;
	struct prio_tree_node	*parent;
};

struct prio_tree_root {
	struct prio_tree_node	*prio_tree_node;
	unsigned int 		index_bits;
};

struct prio_tree_iter {
	struct prio_tree_node	*cur;
	unsigned long		mask;
	unsigned long		value;
	int			size_level;
};

#define INIT_PRIO_TREE_ROOT(ptr)	\
do {					\
	(ptr)->prio_tree_node = NULL;	\
	(ptr)->index_bits = 1;		\
} while (0)

#define INIT_PRIO_TREE_NODE(ptr)				\
do {								\
	(ptr)->left = (ptr)->right = (ptr)->parent = (ptr);	\
} while (0)

#define INIT_PRIO_TREE_ITER(ptr)	\
do {					\
	(ptr)->cur = NULL;		\
	(ptr)->mask = 0UL;		\
	(ptr)->value = 0UL;		\
	(ptr)->size_level = 0;		\
} while (0)

#define prio_tree_entry(ptr, type, member) \
       ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

static inline int prio_tree_empty(const struct prio_tree_root *root)
{
	return root->prio_tree_node == NULL;
}

static inline int prio_tree_root(const struct prio_tree_node *node)
{
	return node->parent == node;
}

static inline int prio_tree_left_empty(const struct prio_tree_node *node)
{
	return node->left == node;
}

static inline int prio_tree_right_empty(const struct prio_tree_node *node)
{
	return node->right == node;
}

#endif /* _LINUX_PRIO_TREE_H */
