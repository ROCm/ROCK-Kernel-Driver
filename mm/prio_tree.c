/*
 * mm/prio_tree.c - priority search tree for mapping->i_mmap{,_shared}
 *
 * Copyright (C) 2004, Rajesh Venkatasubramanian <vrajesh@umich.edu>
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

static unsigned long index_bits_to_maxindex[BITS_PER_LONG];

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
		}
		else {
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
	}
	else
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
static inline struct prio_tree_node *prio_tree_replace(
	struct prio_tree_root *root, struct prio_tree_node *old,
	struct prio_tree_node *node)
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
	}
	else {
		node->parent = old->parent;
		if (old->parent->left == old)
			old->parent->left = node;
		else {
			BUG_ON(old->parent->right != old);
			old->parent->right = node;
		}
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

#undef	swap
#define	swap(x,y,z)	do {z = x; x = y; y = z; } while (0)

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

struct prio_tree_node *prio_tree_insert(struct prio_tree_root *root,
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

                if (h_index < heap_index || (h_index == heap_index &&
						r_index > radix_index))
		{
			struct prio_tree_node *tmp = node;
			node = prio_tree_replace(root, cur, node);
			cur = tmp;
			swap(r_index, radix_index, index);
			swap(h_index, heap_index, index);
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
			}
			else
				cur = cur->right;
		}
		else {
			if (prio_tree_left_empty(cur)) {
				INIT_PRIO_TREE_NODE(node);
				cur->left = node;
				node->parent = cur;
				return res;
			}
			else
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

void prio_tree_remove(struct prio_tree_root *root, struct prio_tree_node *node)
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
		*root = PRIO_TREE_ROOT;
		return;
	}

	if (cur->parent->right == cur)
		cur->parent->right = cur->parent;
	else {
		BUG_ON(cur->parent->left != cur);
		cur->parent->left = cur->parent;
	}

	while (cur != node)
		cur = prio_tree_replace(root, cur->parent, cur);

	return;
}

/*
 * Following functions help to enumerate all prio_tree_nodes in the tree that
 * overlap with the input interval X [radix_index, heap_index]. The enumeration
 * takes O(log n + m) time where 'log n' is the height of the tree (which is
 * proportional to # of bits required to represent the maximum heap_index) and
 * 'm' is the number of prio_tree_nodes that overlap the interval X.
 */

static inline struct prio_tree_node *__prio_tree_left(
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
		}
		else {
			iter->size_level = 1;
			iter->mask = 1UL << (root->index_bits - 1);
		}
		return iter->cur;
	}

	return NULL;
}


static inline struct prio_tree_node *__prio_tree_right(
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
		}
		else {
			iter->size_level = 1;
			iter->mask = 1UL << (root->index_bits - 1);
		}
		return iter->cur;
	}

	return NULL;
}

static inline struct prio_tree_node *__prio_tree_parent(
	struct prio_tree_iter *iter)
{
	iter->cur = iter->cur->parent;
	iter->mask <<= 1;
	if (iter->size_level) {
		if (iter->size_level == 1)
			iter->mask = 1UL;
		iter->size_level--;
	}
	else if (iter->value & iter->mask)
		iter->value ^= iter->mask;
	return iter->cur;
}

static inline int overlap(unsigned long radix_index, unsigned long heap_index,
	unsigned long r_index, unsigned long h_index)
{
	if (heap_index < r_index || radix_index > h_index)
		return 0;

	return 1;
}

/*
 * prio_tree_first:
 *
 * Get the first prio_tree_node that overlaps with the interval [radix_index,
 * heap_index]. Note that always radix_index <= heap_index. We do a pre-order
 * traversal of the tree.
 */
struct prio_tree_node *prio_tree_first(struct prio_tree_root *root,
	struct prio_tree_iter *iter, unsigned long radix_index,
	unsigned long heap_index)
{
	unsigned long r_index, h_index;

	*iter = PRIO_TREE_ITER;

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

		if (__prio_tree_left(root, iter, radix_index, heap_index,
					&r_index, &h_index))
			continue;

		if (__prio_tree_right(root, iter, radix_index, heap_index,
					&r_index, &h_index))
			continue;

		break;
	}
	return NULL;
}
EXPORT_SYMBOL(prio_tree_first);

/* Get the next prio_tree_node that overlaps with the input interval in iter */
struct prio_tree_node *prio_tree_next(struct prio_tree_root *root,
	struct prio_tree_iter *iter, unsigned long radix_index,
	unsigned long heap_index)
{
	unsigned long r_index, h_index;

repeat:
	while (__prio_tree_left(root, iter, radix_index, heap_index,
				&r_index, &h_index))
		if (overlap(radix_index, heap_index, r_index, h_index))
			return iter->cur;

	while (!__prio_tree_right(root, iter, radix_index, heap_index,
				&r_index, &h_index)) {
	    	while (!prio_tree_root(iter->cur) &&
				iter->cur->parent->right == iter->cur)
			__prio_tree_parent(iter);

		if (prio_tree_root(iter->cur))
			return NULL;

		__prio_tree_parent(iter);
	}

	if (overlap(radix_index, heap_index, r_index, h_index))
			return iter->cur;

	goto repeat;
}
EXPORT_SYMBOL(prio_tree_next);

/*
 * Radix priority search tree for address_space->i_mmap_{_shared}
 *
 * For each vma that map a unique set of file pages i.e., unique [radix_index,
 * heap_index] value, we have a corresponing priority search tree node. If
 * multiple vmas have identical [radix_index, heap_index] value, then one of
 * them is used as a tree node and others are stored in a vm_set list. The tree
 * node points to the first vma (head) of the list using vm_set_head.
 *
 * prio_tree_root
 *      |
 *      A       vm_set_head
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
 * That's why some trick involving shared.both.parent is used for identifying
 * tree nodes and list head nodes. We can possibly use the least significant
 * bit of the vm_set_head field to mark tree and list head nodes. I was worried
 * about the alignment of vm_area_struct in various architectures.
 *
 * vma radix priority search tree node rules:
 *
 * vma->shared.both.parent != NULL	==>	a tree node
 *
 * vma->shared.both.parent == NULL
 * 	vma->vm_set_head != NULL  ==>  list head of vmas that map same pages
 * 	vma->vm_set_head == NULL  ==>  a list node
 */

void __vma_prio_tree_insert(struct prio_tree_root *root,
	struct vm_area_struct *vma)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *old;

	ptr = prio_tree_insert(root, &vma->shared.prio_tree_node);

	if (ptr == &vma->shared.prio_tree_node) {
		vma->vm_set_head = NULL;
		return;
	}

	old = prio_tree_entry(ptr, struct vm_area_struct,
			shared.prio_tree_node);

	__vma_prio_tree_add(vma, old);
}

void __vma_prio_tree_remove(struct prio_tree_root *root,
	struct vm_area_struct *vma)
{
	struct vm_area_struct *node, *head, *new_head;

	if (vma->shared.both.parent == NULL && vma->vm_set_head == NULL) {
		list_del_init(&vma->shared.vm_set.list);
		INIT_VMA_SHARED(vma);
		return;
	}

	if (vma->vm_set_head) {
		/* Leave this BUG_ON till prio_tree patch stabilizes */
		BUG_ON(vma->vm_set_head->vm_set_head != vma);
		if (vma->shared.both.parent) {
			head = vma->vm_set_head;
			if (!list_empty(&head->shared.vm_set.list)) {
				new_head = list_entry(
					head->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&head->shared.vm_set.list);
			}
			else
				new_head = NULL;

			prio_tree_replace(root, &vma->shared.prio_tree_node,
					&head->shared.prio_tree_node);
			head->vm_set_head = new_head;
			if (new_head)
				new_head->vm_set_head = head;

		}
		else {
			node = vma->vm_set_head;
			if (!list_empty(&vma->shared.vm_set.list)) {
				new_head = list_entry(
					vma->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&vma->shared.vm_set.list);
				node->vm_set_head = new_head;
				new_head->vm_set_head = node;
			}
			else
				node->vm_set_head = NULL;
		}
		INIT_VMA_SHARED(vma);
		return;
	}

	prio_tree_remove(root, &vma->shared.prio_tree_node);
	INIT_VMA_SHARED(vma);
}

void __init prio_tree_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(index_bits_to_maxindex) - 1; i++)
		index_bits_to_maxindex[i] = (1UL << (i + 1)) - 1;
	index_bits_to_maxindex[ARRAY_SIZE(index_bits_to_maxindex) - 1] = ~0UL;
}
