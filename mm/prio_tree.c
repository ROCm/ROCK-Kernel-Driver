/*
 * mm/prio_tree.c - priority search tree for mapping->i_mmap{,_shared}
 *
 * Copyright (C) 2004, Rajesh Venkatasubramanian <vrajesh@umich.edu>
 *
 * This file is released under the GPL v2.
 *
 * Based on the radix priority search tree proposed by Edward M. McCreight
 * SIAM Journal of Computing, vol. 14, no.2, pages 257-276, May 1985
 *
 * 02Feb2004	Initial version
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/prio_tree.h>

/*
 * A clever mix of heap and radix trees forms a radix priority search tree (PST)
 * which is useful for storing intervals, e.g, we can consider a vma as a closed
 * interval of file pages [offset_begin, offset_end], and store all vmas that
 * map a file in a PST. Then, using the PST, we can answer a stabbing query,
 * i.e., selecting a set of stored intervals (vmas) that overlap with (map) a
 * given input interval X (a set of consecutive file pages), in "O(log n + m)"
 * time where 'log n' is the height of the PST, and 'm' is the number of stored
 * intervals (vmas) that overlap (map) with the input interval X (the set of
 * consecutive file pages).
 *
 * In our implementation, we store closed intervals of the form [radix_index,
 * heap_index]. We assume that always radix_index <= heap_index. McCreight's PST
 * is designed for storing intervals with unique radix indices, i.e., each
 * interval have different radix_index. However, this limitation can be easily
 * overcome by using the size, i.e., heap_index - radix_index, as part of the
 * index, so we index the tree using [(radix_index,size), heap_index].
 *
 * When the above-mentioned indexing scheme is used, theoretically, in a 32 bit
 * machine, the maximum height of a PST can be 64. We can use a balanced version
 * of the priority search tree to optimize the tree height, but the balanced
 * tree proposed by McCreight is too complex and memory-hungry for our purpose.
 */

/*
 * The following macros are used for implementing prio_tree for i_mmap{_shared}
 */

#define RADIX_INDEX(vma)  ((vma)->vm_pgoff)
#define VMA_SIZE(vma)	  (((vma)->vm_end - (vma)->vm_start) >> PAGE_SHIFT)
/* avoid overflow */
#define HEAP_INDEX(vma)	  ((vma)->vm_pgoff + (VMA_SIZE(vma) - 1))

#define GET_INDEX_VMA(vma, radix, heap)		\
do {						\
	radix = RADIX_INDEX(vma);		\
	heap = HEAP_INDEX(vma);			\
} while (0)

#define GET_INDEX(node, radix, heap)		\
do { 						\
	struct vm_area_struct *__tmp = 		\
	  prio_tree_entry(node, struct vm_area_struct, shared.prio_tree_node);\
	GET_INDEX_VMA(__tmp, radix, heap); 	\
} while (0)

static unsigned long index_bits_to_maxindex[BITS_PER_LONG];

void __init prio_tree_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(index_bits_to_maxindex) - 1; i++)
		index_bits_to_maxindex[i] = (1UL << (i + 1)) - 1;
	index_bits_to_maxindex[ARRAY_SIZE(index_bits_to_maxindex) - 1] = ~0UL;
}

/*
 * Maximum heap_index that can be stored in a PST with index_bits bits
 */
static inline unsigned long prio_tree_maxindex(unsigned int bits)
{
	return index_bits_to_maxindex[bits - 1];
}

/*
 * Extend a priority search tree so that it can store a node with heap_index
 * max_heap_index. In the worst case, this algorithm takes O((log n)^2).
 * However, this function is used rarely and the common case performance is
 * not bad.
 */
static struct prio_tree_node *prio_tree_expand(struct prio_tree_root *root,
		struct prio_tree_node *node, unsigned long max_heap_index)
{
	static void prio_tree_remove(struct prio_tree_root *,
					struct prio_tree_node *);
	struct prio_tree_node *first = NULL, *prev, *last = NULL;

	if (max_heap_index > prio_tree_maxindex(root->index_bits))
		root->index_bits++;

	while (max_heap_index > prio_tree_maxindex(root->index_bits)) {
		root->index_bits++;

		if (prio_tree_empty(root))
			continue;

		if (first == NULL) {
			first = root->prio_tree_node;
			prio_tree_remove(root, root->prio_tree_node);
			INIT_PRIO_TREE_NODE(first);
			last = first;
		} else {
			prev = last;
			last = root->prio_tree_node;
			prio_tree_remove(root, root->prio_tree_node);
			INIT_PRIO_TREE_NODE(last);
			prev->left = last;
			last->parent = prev;
		}
	}

	INIT_PRIO_TREE_NODE(node);

	if (first) {
		node->left = first;
		first->parent = node;
	} else
		last = node;

	if (!prio_tree_empty(root)) {
		last->left = root->prio_tree_node;
		last->left->parent = last;
	}

	root->prio_tree_node = node;
	return node;
}

/*
 * Replace a prio_tree_node with a new node and return the old node
 */
static struct prio_tree_node *prio_tree_replace(struct prio_tree_root *root,
		struct prio_tree_node *old, struct prio_tree_node *node)
{
	INIT_PRIO_TREE_NODE(node);

	if (prio_tree_root(old)) {
		BUG_ON(root->prio_tree_node != old);
		/*
		 * We can reduce root->index_bits here. However, it is complex
		 * and does not help much to improve performance (IMO).
		 */
		node->parent = node;
		root->prio_tree_node = node;
	} else {
		node->parent = old->parent;
		if (old->parent->left == old)
			old->parent->left = node;
		else
			old->parent->right = node;
	}

	if (!prio_tree_left_empty(old)) {
		node->left = old->left;
		old->left->parent = node;
	}

	if (!prio_tree_right_empty(old)) {
		node->right = old->right;
		old->right->parent = node;
	}

	return old;
}

/*
 * Insert a prio_tree_node @node into a radix priority search tree @root. The
 * algorithm typically takes O(log n) time where 'log n' is the number of bits
 * required to represent the maximum heap_index. In the worst case, the algo
 * can take O((log n)^2) - check prio_tree_expand.
 *
 * If a prior node with same radix_index and heap_index is already found in
 * the tree, then returns the address of the prior node. Otherwise, inserts
 * @node into the tree and returns @node.
 */
static struct prio_tree_node *prio_tree_insert(struct prio_tree_root *root,
		struct prio_tree_node *node)
{
	struct prio_tree_node *cur, *res = node;
	unsigned long radix_index, heap_index;
	unsigned long r_index, h_index, index, mask;
	int size_flag = 0;

	GET_INDEX(node, radix_index, heap_index);

	if (prio_tree_empty(root) ||
			heap_index > prio_tree_maxindex(root->index_bits))
		return prio_tree_expand(root, node, heap_index);

	cur = root->prio_tree_node;
	mask = 1UL << (root->index_bits - 1);

	while (mask) {
		GET_INDEX(cur, r_index, h_index);

		if (r_index == radix_index && h_index == heap_index)
			return cur;

                if (h_index < heap_index ||
		    (h_index == heap_index && r_index > radix_index)) {
			struct prio_tree_node *tmp = node;
			node = prio_tree_replace(root, cur, node);
			cur = tmp;
			/* swap indices */
			index = r_index;
			r_index = radix_index;
			radix_index = index;
			index = h_index;
			h_index = heap_index;
			heap_index = index;
		}

		if (size_flag)
			index = heap_index - radix_index;
		else
			index = radix_index;

		if (index & mask) {
			if (prio_tree_right_empty(cur)) {
				INIT_PRIO_TREE_NODE(node);
				cur->right = node;
				node->parent = cur;
				return res;
			} else
				cur = cur->right;
		} else {
			if (prio_tree_left_empty(cur)) {
				INIT_PRIO_TREE_NODE(node);
				cur->left = node;
				node->parent = cur;
				return res;
			} else
				cur = cur->left;
		}

		mask >>= 1;

		if (!mask) {
			mask = 1UL << (root->index_bits - 1);
			size_flag = 1;
		}
	}
	/* Should not reach here */
	BUG();
	return NULL;
}

/*
 * Remove a prio_tree_node @node from a radix priority search tree @root. The
 * algorithm takes O(log n) time where 'log n' is the number of bits required
 * to represent the maximum heap_index.
 */
static void prio_tree_remove(struct prio_tree_root *root,
		struct prio_tree_node *node)
{
	struct prio_tree_node *cur;
	unsigned long r_index, h_index_right, h_index_left;

	cur = node;

	while (!prio_tree_left_empty(cur) || !prio_tree_right_empty(cur)) {
		if (!prio_tree_left_empty(cur))
			GET_INDEX(cur->left, r_index, h_index_left);
		else {
			cur = cur->right;
			continue;
		}

		if (!prio_tree_right_empty(cur))
			GET_INDEX(cur->right, r_index, h_index_right);
		else {
			cur = cur->left;
			continue;
		}

		/* both h_index_left and h_index_right cannot be 0 */
		if (h_index_left >= h_index_right)
			cur = cur->left;
		else
			cur = cur->right;
	}

	if (prio_tree_root(cur)) {
		BUG_ON(root->prio_tree_node != cur);
		INIT_PRIO_TREE_ROOT(root);
		return;
	}

	if (cur->parent->right == cur)
		cur->parent->right = cur->parent;
	else
		cur->parent->left = cur->parent;

	while (cur != node)
		cur = prio_tree_replace(root, cur->parent, cur);
}

/*
 * Following functions help to enumerate all prio_tree_nodes in the tree that
 * overlap with the input interval X [radix_index, heap_index]. The enumeration
 * takes O(log n + m) time where 'log n' is the height of the tree (which is
 * proportional to # of bits required to represent the maximum heap_index) and
 * 'm' is the number of prio_tree_nodes that overlap the interval X.
 */

static struct prio_tree_node *prio_tree_left(
		struct prio_tree_root *root, struct prio_tree_iter *iter,
		unsigned long radix_index, unsigned long heap_index,
		unsigned long *r_index, unsigned long *h_index)
{
	if (prio_tree_left_empty(iter->cur))
		return NULL;

	GET_INDEX(iter->cur->left, *r_index, *h_index);

	if (radix_index <= *h_index) {
		iter->cur = iter->cur->left;
		iter->mask >>= 1;
		if (iter->mask) {
			if (iter->size_level)
				iter->size_level++;
		} else {
			if (iter->size_level) {
				BUG_ON(!prio_tree_left_empty(iter->cur));
				BUG_ON(!prio_tree_right_empty(iter->cur));
				iter->size_level++;
				iter->mask = ULONG_MAX;
			} else {
				iter->size_level = 1;
				iter->mask = 1UL << (root->index_bits - 1);
			}
		}
		return iter->cur;
	}

	return NULL;
}

static struct prio_tree_node *prio_tree_right(
		struct prio_tree_root *root, struct prio_tree_iter *iter,
		unsigned long radix_index, unsigned long heap_index,
		unsigned long *r_index, unsigned long *h_index)
{
	unsigned long value;

	if (prio_tree_right_empty(iter->cur))
		return NULL;

	if (iter->size_level)
		value = iter->value;
	else
		value = iter->value | iter->mask;

	if (heap_index < value)
		return NULL;

	GET_INDEX(iter->cur->right, *r_index, *h_index);

	if (radix_index <= *h_index) {
		iter->cur = iter->cur->right;
		iter->mask >>= 1;
		iter->value = value;
		if (iter->mask) {
			if (iter->size_level)
				iter->size_level++;
		} else {
			if (iter->size_level) {
				BUG_ON(!prio_tree_left_empty(iter->cur));
				BUG_ON(!prio_tree_right_empty(iter->cur));
				iter->size_level++;
				iter->mask = ULONG_MAX;
			} else {
				iter->size_level = 1;
				iter->mask = 1UL << (root->index_bits - 1);
			}
		}
		return iter->cur;
	}

	return NULL;
}

static struct prio_tree_node *prio_tree_parent(struct prio_tree_iter *iter)
{
	iter->cur = iter->cur->parent;
	if (iter->mask == ULONG_MAX)
		iter->mask = 1UL;
	else if (iter->size_level == 1)
		iter->mask = 1UL;
	else
		iter->mask <<= 1;
	if (iter->size_level)
		iter->size_level--;
	if (!iter->size_level && (iter->value & iter->mask))
		iter->value ^= iter->mask;
	return iter->cur;
}

static inline int overlap(unsigned long radix_index, unsigned long heap_index,
		unsigned long r_index, unsigned long h_index)
{
	return heap_index >= r_index && radix_index <= h_index;
}

/*
 * prio_tree_first:
 *
 * Get the first prio_tree_node that overlaps with the interval [radix_index,
 * heap_index]. Note that always radix_index <= heap_index. We do a pre-order
 * traversal of the tree.
 */
static struct prio_tree_node *prio_tree_first(struct prio_tree_root *root,
		struct prio_tree_iter *iter, unsigned long radix_index,
		unsigned long heap_index)
{
	unsigned long r_index, h_index;

	INIT_PRIO_TREE_ITER(iter);

	if (prio_tree_empty(root))
		return NULL;

	GET_INDEX(root->prio_tree_node, r_index, h_index);

	if (radix_index > h_index)
		return NULL;

	iter->mask = 1UL << (root->index_bits - 1);
	iter->cur = root->prio_tree_node;

	while (1) {
		if (overlap(radix_index, heap_index, r_index, h_index))
			return iter->cur;

		if (prio_tree_left(root, iter, radix_index, heap_index,
					&r_index, &h_index))
			continue;

		if (prio_tree_right(root, iter, radix_index, heap_index,
					&r_index, &h_index))
			continue;

		break;
	}
	return NULL;
}

/*
 * prio_tree_next:
 *
 * Get the next prio_tree_node that overlaps with the input interval in iter
 */
static struct prio_tree_node *prio_tree_next(struct prio_tree_root *root,
		struct prio_tree_iter *iter, unsigned long radix_index,
		unsigned long heap_index)
{
	unsigned long r_index, h_index;

repeat:
	while (prio_tree_left(root, iter, radix_index,
				heap_index, &r_index, &h_index)) {
		if (overlap(radix_index, heap_index, r_index, h_index))
			return iter->cur;
	}

	while (!prio_tree_right(root, iter, radix_index,
				heap_index, &r_index, &h_index)) {
	    	while (!prio_tree_root(iter->cur) &&
				iter->cur->parent->right == iter->cur)
			prio_tree_parent(iter);

		if (prio_tree_root(iter->cur))
			return NULL;

		prio_tree_parent(iter);
	}

	if (overlap(radix_index, heap_index, r_index, h_index))
		return iter->cur;

	goto repeat;
}

/*
 * Radix priority search tree for address_space->i_mmap_{_shared}
 *
 * For each vma that map a unique set of file pages i.e., unique [radix_index,
 * heap_index] value, we have a corresponing priority search tree node. If
 * multiple vmas have identical [radix_index, heap_index] value, then one of
 * them is used as a tree node and others are stored in a vm_set list. The tree
 * node points to the first vma (head) of the list using vm_set.head.
 *
 * prio_tree_root
 *      |
 *      A       vm_set.head
 *     / \      /
 *    L   R -> H-I-J-K-M-N-O-P-Q-S
 *    ^   ^    <-- vm_set.list -->
 *  tree nodes
 *
 * We need some way to identify whether a vma is a tree node, head of a vm_set
 * list, or just a member of a vm_set list. We cannot use vm_flags to store
 * such information. The reason is, in the above figure, it is possible that
 * vm_flags' of R and H are covered by the different mmap_sems. When R is
 * removed under R->mmap_sem, H replaces R as a tree node. Since we do not hold
 * H->mmap_sem, we cannot use H->vm_flags for marking that H is a tree node now.
 * That's why some trick involving shared.vm_set.parent is used for identifying
 * tree nodes and list head nodes.
 *
 * vma radix priority search tree node rules:
 *
 * vma->shared.vm_set.parent != NULL    ==> a tree node
 *      vma->shared.vm_set.head != NULL ==> list of others mapping same range
 *      vma->shared.vm_set.head == NULL ==> no others map the same range
 *
 * vma->shared.vm_set.parent == NULL
 * 	vma->shared.vm_set.head != NULL ==> list head of vmas mapping same range
 * 	vma->shared.vm_set.head == NULL ==> a list node
 */

/*
 * Add a new vma known to map the same set of pages as the old vma:
 * useful for fork's dup_mmap as well as vma_prio_tree_insert below.
 */
void vma_prio_tree_add(struct vm_area_struct *vma, struct vm_area_struct *old)
{
	/* Leave these BUG_ONs till prio_tree patch stabilizes */
	BUG_ON(RADIX_INDEX(vma) != RADIX_INDEX(old));
	BUG_ON(HEAP_INDEX(vma) != HEAP_INDEX(old));

	if (!old->shared.vm_set.parent)
		list_add(&vma->shared.vm_set.list,
				&old->shared.vm_set.list);
	else if (old->shared.vm_set.head)
		list_add_tail(&vma->shared.vm_set.list,
				&old->shared.vm_set.head->shared.vm_set.list);
	else {
		INIT_LIST_HEAD(&vma->shared.vm_set.list);
		vma->shared.vm_set.head = old;
		old->shared.vm_set.head = vma;
	}
}

void vma_prio_tree_insert(struct vm_area_struct *vma,
			  struct prio_tree_root *root)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *old;

	ptr = prio_tree_insert(root, &vma->shared.prio_tree_node);
	if (ptr != &vma->shared.prio_tree_node) {
		old = prio_tree_entry(ptr, struct vm_area_struct,
					shared.prio_tree_node);
		vma_prio_tree_add(vma, old);
	}
}

void vma_prio_tree_remove(struct vm_area_struct *vma,
			  struct prio_tree_root *root)
{
	struct vm_area_struct *node, *head, *new_head;

	if (!vma->shared.vm_set.head) {
		if (!vma->shared.vm_set.parent)
			list_del_init(&vma->shared.vm_set.list);
		else
			prio_tree_remove(root, &vma->shared.prio_tree_node);
	} else {
		/* Leave this BUG_ON till prio_tree patch stabilizes */
		BUG_ON(vma->shared.vm_set.head->shared.vm_set.head != vma);
		if (vma->shared.vm_set.parent) {
			head = vma->shared.vm_set.head;
			if (!list_empty(&head->shared.vm_set.list)) {
				new_head = list_entry(
					head->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&head->shared.vm_set.list);
			} else
				new_head = NULL;

			prio_tree_replace(root, &vma->shared.prio_tree_node,
					&head->shared.prio_tree_node);
			head->shared.vm_set.head = new_head;
			if (new_head)
				new_head->shared.vm_set.head = head;

		} else {
			node = vma->shared.vm_set.head;
			if (!list_empty(&vma->shared.vm_set.list)) {
				new_head = list_entry(
					vma->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&vma->shared.vm_set.list);
				node->shared.vm_set.head = new_head;
				new_head->shared.vm_set.head = node;
			} else
				node->shared.vm_set.head = NULL;
		}
	}
}

/*
 * Helper function to enumerate vmas that map a given file page or a set of
 * contiguous file pages. The function returns vmas that at least map a single
 * page in the given range of contiguous file pages.
 */
struct vm_area_struct *vma_prio_tree_next(struct vm_area_struct *vma,
		struct prio_tree_root *root, struct prio_tree_iter *iter,
		pgoff_t begin, pgoff_t end)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *next;

	if (!vma) {
		/*
		 * First call is with NULL vma
		 */
		ptr = prio_tree_first(root, iter, begin, end);
		if (ptr)
			return prio_tree_entry(ptr, struct vm_area_struct,
						shared.prio_tree_node);
		else
			return NULL;
	}

	if (vma->shared.vm_set.parent) {
		if (vma->shared.vm_set.head)
			return vma->shared.vm_set.head;
	} else {
		next = list_entry(vma->shared.vm_set.list.next,
				struct vm_area_struct, shared.vm_set.list);
		if (!next->shared.vm_set.head)
			return next;
	}

	ptr = prio_tree_next(root, iter, begin, end);
	if (ptr)
		return prio_tree_entry(ptr, struct vm_area_struct,
					shared.prio_tree_node);
	else
		return NULL;
}
EXPORT_SYMBOL(vma_prio_tree_next);
