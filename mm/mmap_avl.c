/*
 * Searching a VMA in the linear list task->mm->mmap is horribly slow.
 * Use an AVL (Adelson-Velskii and Landis) tree to speed up this search
 * from O(n) to O(log n), where n is the number of VMAs of the task
 * n is typically around 6, but may reach 3000 in some cases: object-oriented
 * databases, persistent store, generational garbage collection (Java, Lisp),
 * ElectricFence.
 * Written by Bruno Haible <haible@ma2s2.mathematik.uni-karlsruhe.de>.
 */

/* We keep the list and tree sorted by address. */
#define vm_avl_key	vm_end
#define vm_avl_key_t	unsigned long	/* typeof(vma->avl_key) */

/*
 * task->mm->mmap_avl is the AVL tree corresponding to task->mm->mmap
 * or, more exactly, its root.
 * A vm_area_struct has the following fields:
 *   vm_avl_left     left son of a tree node
 *   vm_avl_right    right son of a tree node
 *   vm_avl_height   1+max(heightof(left),heightof(right))
 * The empty tree is represented as NULL.
 */

/* Since the trees are balanced, their height will never be large. */
#define avl_maxheight	41	/* why this? a small exercise */
#define heightof(tree)	((tree) == vm_avl_empty ? 0 : (tree)->vm_avl_height)
/*
 * Consistency and balancing rules:
 * 1. tree->vm_avl_height == 1+max(heightof(tree->vm_avl_left),heightof(tree->vm_avl_right))
 * 2. abs( heightof(tree->vm_avl_left) - heightof(tree->vm_avl_right) ) <= 1
 * 3. foreach node in tree->vm_avl_left: node->vm_avl_key <= tree->vm_avl_key,
 *    foreach node in tree->vm_avl_right: node->vm_avl_key >= tree->vm_avl_key.
 */

#ifdef DEBUG_AVL

/* Look up the nodes at the left and at the right of a given node. */
static void avl_neighbours (struct vm_area_struct * node, struct vm_area_struct * tree, struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = node->vm_avl_key;

	*to_the_left = *to_the_right = NULL;
	for (;;) {
		if (tree == vm_avl_empty) {
			printk("avl_neighbours: node not found in the tree\n");
			return;
		}
		if (key == tree->vm_avl_key)
			break;
		if (key < tree->vm_avl_key) {
			*to_the_right = tree;
			tree = tree->vm_avl_left;
		} else {
			*to_the_left = tree;
			tree = tree->vm_avl_right;
		}
	}
	if (tree != node) {
		printk("avl_neighbours: node not exactly found in the tree\n");
		return;
	}
	if (tree->vm_avl_left != vm_avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_left; node->vm_avl_right != vm_avl_empty; node = node->vm_avl_right)
			continue;
		*to_the_left = node;
	}
	if (tree->vm_avl_right != vm_avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_right; node->vm_avl_left != vm_avl_empty; node = node->vm_avl_left)
			continue;
		*to_the_right = node;
	}
	if ((*to_the_left && ((*to_the_left)->vm_next != node)) || (node->vm_next != *to_the_right))
		printk("avl_neighbours: tree inconsistent with list\n");
}

#endif

/*
 * Rebalance a tree.
 * After inserting or deleting a node of a tree we have a sequence of subtrees
 * nodes[0]..nodes[k-1] such that
 * nodes[0] is the root and nodes[i+1] = nodes[i]->{vm_avl_left|vm_avl_right}.
 */
static void avl_rebalance (struct vm_area_struct *** nodeplaces_ptr, int count)
{
	for ( ; count > 0 ; count--) {
		struct vm_area_struct ** nodeplace = *--nodeplaces_ptr;
		struct vm_area_struct * node = *nodeplace;
		struct vm_area_struct * nodeleft = node->vm_avl_left;
		struct vm_area_struct * noderight = node->vm_avl_right;
		int heightleft = heightof(nodeleft);
		int heightright = heightof(noderight);
		if (heightright + 1 < heightleft) {
			/*                                                      */
			/*                            *                         */
			/*                          /   \                       */
			/*                       n+2      n                     */
			/*                                                      */
			struct vm_area_struct * nodeleftleft = nodeleft->vm_avl_left;
			struct vm_area_struct * nodeleftright = nodeleft->vm_avl_right;
			int heightleftright = heightof(nodeleftright);
			if (heightof(nodeleftleft) >= heightleftright) {
				/*                                                        */
				/*                *                    n+2|n+3            */
				/*              /   \                  /    \             */
				/*           n+2      n      -->      /   n+1|n+2         */
				/*           / \                      |    /    \         */
				/*         n+1 n|n+1                 n+1  n|n+1  n        */
				/*                                                        */
				node->vm_avl_left = nodeleftright; nodeleft->vm_avl_right = node;
				nodeleft->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightleftright);
				*nodeplace = nodeleft;
			} else {
				/*                                                        */
				/*                *                     n+2               */
				/*              /   \                 /     \             */
				/*           n+2      n      -->    n+1     n+1           */
				/*           / \                    / \     / \           */
				/*          n  n+1                 n   L   R   n          */
				/*             / \                                        */
				/*            L   R                                       */
				/*                                                        */
				nodeleft->vm_avl_right = nodeleftright->vm_avl_left;
				node->vm_avl_left = nodeleftright->vm_avl_right;
				nodeleftright->vm_avl_left = nodeleft;
				nodeleftright->vm_avl_right = node;
				nodeleft->vm_avl_height = node->vm_avl_height = heightleftright;
				nodeleftright->vm_avl_height = heightleft;
				*nodeplace = nodeleftright;
			}
		}
		else if (heightleft + 1 < heightright) {
			/* similar to the above, just interchange 'left' <--> 'right' */
			struct vm_area_struct * noderightright = noderight->vm_avl_right;
			struct vm_area_struct * noderightleft = noderight->vm_avl_left;
			int heightrightleft = heightof(noderightleft);
			if (heightof(noderightright) >= heightrightleft) {
				node->vm_avl_right = noderightleft; noderight->vm_avl_left = node;
				noderight->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightrightleft);
				*nodeplace = noderight;
			} else {
				noderight->vm_avl_left = noderightleft->vm_avl_right;
				node->vm_avl_right = noderightleft->vm_avl_left;
				noderightleft->vm_avl_right = noderight;
				noderightleft->vm_avl_left = node;
				noderight->vm_avl_height = node->vm_avl_height = heightrightleft;
				noderightleft->vm_avl_height = heightright;
				*nodeplace = noderightleft;
			}
		}
		else {
			int height = (heightleft<heightright ? heightright : heightleft) + 1;
			if (height == node->vm_avl_height)
				break;
			node->vm_avl_height = height;
		}
	}
}

/* Insert a node into a tree. */
static inline void avl_insert (struct vm_area_struct * new_node, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == vm_avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	new_node->vm_avl_left = vm_avl_empty;
	new_node->vm_avl_right = vm_avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Insert a node into a tree, and
 * return the node to the left of it and the node to the right of it.
 */
static inline void avl_insert_neighbours (struct vm_area_struct * new_node, struct vm_area_struct ** ptree,
	struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	*to_the_left = *to_the_right = NULL;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == vm_avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key) {
			*to_the_right = node;
			nodeplace = &node->vm_avl_left;
		} else {
			*to_the_left = node;
			nodeplace = &node->vm_avl_right;
		}
	}
	new_node->vm_avl_left = vm_avl_empty;
	new_node->vm_avl_right = vm_avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Removes a node out of a tree. */
static void avl_remove (struct vm_area_struct * node_to_delete, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = node_to_delete->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	struct vm_area_struct ** nodeplace_to_delete;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
#ifdef DEBUG_AVL
		if (node == vm_avl_empty) {
			/* what? node_to_delete not found in tree? */
			printk("avl_remove: node to delete not found in tree\n");
			return;
		}
#endif
		*stack_ptr++ = nodeplace; stack_count++;
		if (key == node->vm_avl_key)
			break;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	nodeplace_to_delete = nodeplace;
	/* Have to remove node_to_delete = *nodeplace_to_delete. */
	if (node_to_delete->vm_avl_left == vm_avl_empty) {
		*nodeplace_to_delete = node_to_delete->vm_avl_right;
		stack_ptr--; stack_count--;
	} else {
		struct vm_area_struct *** stack_ptr_to_delete = stack_ptr;
		struct vm_area_struct ** nodeplace = &node_to_delete->vm_avl_left;
		struct vm_area_struct * node;
		for (;;) {
			node = *nodeplace;
			if (node->vm_avl_right == vm_avl_empty)
				break;
			*stack_ptr++ = nodeplace; stack_count++;
			nodeplace = &node->vm_avl_right;
		}
		*nodeplace = node->vm_avl_left;
		/* node replaces node_to_delete */
		node->vm_avl_left = node_to_delete->vm_avl_left;
		node->vm_avl_right = node_to_delete->vm_avl_right;
		node->vm_avl_height = node_to_delete->vm_avl_height;
		*nodeplace_to_delete = node; /* replace node_to_delete */
		*stack_ptr_to_delete = &node->vm_avl_left; /* replace &node_to_delete->vm_avl_left */
	}
	avl_rebalance(stack_ptr,stack_count);
}

#ifdef DEBUG_AVL

/* print a list */
static void printk_list (struct vm_area_struct * vma)
{
	printk("[");
	while (vma) {
		printk("%08lX-%08lX", vma->vm_start, vma->vm_end);
		vma = vma->vm_next;
		if (!vma)
			break;
		printk(" ");
	}
	printk("]");
}

/* print a tree */
static void printk_avl (struct vm_area_struct * tree)
{
	if (tree != vm_avl_empty) {
		printk("(");
		if (tree->vm_avl_left != vm_avl_empty) {
			printk_avl(tree->vm_avl_left);
			printk("<");
		}
		printk("%08lX-%08lX", tree->vm_start, tree->vm_end);
		if (tree->vm_avl_right != vm_avl_empty) {
			printk(">");
			printk_avl(tree->vm_avl_right);
		}
		printk(")");
	}
}

static char *avl_check_point = "somewhere";

/* check a tree's consistency and balancing */
static void avl_checkheights (struct vm_area_struct * tree)
{
	int h, hl, hr;

	if (tree == vm_avl_empty)
		return;
	avl_checkheights(tree->vm_avl_left);
	avl_checkheights(tree->vm_avl_right);
	h = tree->vm_avl_height;
	hl = heightof(tree->vm_avl_left);
	hr = heightof(tree->vm_avl_right);
	if ((h == hl+1) && (hr <= hl) && (hl <= hr+1))
		return;
	if ((h == hr+1) && (hl <= hr) && (hr <= hl+1))
		return;
	printk("%s: avl_checkheights: heights inconsistent\n",avl_check_point);
}

/* check that all values stored in a tree are < key */
static void avl_checkleft (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == vm_avl_empty)
		return;
	avl_checkleft(tree->vm_avl_left,key);
	avl_checkleft(tree->vm_avl_right,key);
	if (tree->vm_avl_key < key)
		return;
	printk("%s: avl_checkleft: left key %lu >= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values stored in a tree are > key */
static void avl_checkright (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == vm_avl_empty)
		return;
	avl_checkright(tree->vm_avl_left,key);
	avl_checkright(tree->vm_avl_right,key);
	if (tree->vm_avl_key > key)
		return;
	printk("%s: avl_checkright: right key %lu <= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values are properly increasing */
static void avl_checkorder (struct vm_area_struct * tree)
{
	if (tree == vm_avl_empty)
		return;
	avl_checkorder(tree->vm_avl_left);
	avl_checkorder(tree->vm_avl_right);
	avl_checkleft(tree->vm_avl_left,tree->vm_avl_key);
	avl_checkright(tree->vm_avl_right,tree->vm_avl_key);
}

/* all checks */
static void avl_check (struct task_struct * task, char *caller)
{
	avl_check_point = caller;
/*	printk("task \"%s\", %s\n",task->comm,caller); */
/*	printk("task \"%s\" list: ",task->comm); printk_list(task->mm->mmap); printk("\n"); */
/*	printk("task \"%s\" tree: ",task->comm); printk_avl(task->mm->mmap_avl); printk("\n"); */
	avl_checkheights(task->mm->mmap_avl);
	avl_checkorder(task->mm->mmap_avl);
}

#endif
