/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Node plugin interface.

   Description: The tree provides the abstraction of flows, which it
   internally fragments into items which it stores in nodes.

   A key_atom is a piece of data bound to a single key.

   For reasonable space efficiency to be achieved it is often
   necessary to store key_atoms in the nodes in the form of items, where
   an item is a sequence of key_atoms of the same or similar type. It is
   more space-efficient, because the item can implement (very)
   efficient compression of key_atom's bodies using internal knowledge
   about their semantics, and it can often avoid having a key for each
   key_atom. Each type of item has specific operations implemented by its
   item handler (see balance.c).

   Rationale: the rest of the code (specifically balancing routines)
   accesses leaf level nodes through this interface. This way we can
   implement various block layouts and even combine various layouts
   within the same tree. Balancing/allocating algorithms should not
   care about peculiarities of splitting/merging specific item types,
   but rather should leave that to the item's item handler.

   Items, including those that provide the abstraction of flows, have
   the property that if you move them in part or in whole to another
   node, the balancing code invokes their is_left_mergeable()
   item_operation to determine if they are mergeable with their new
   neighbor in the node you have moved them to.  For some items the
   is_left_mergeable() function always returns null.

   When moving the bodies of items from one node to another:

     if a partial item is shifted to another node the balancing code invokes
     an item handler method to handle the item splitting.

     if the balancing code needs to merge with an item in the node it
     is shifting to, it will invoke an item handler method to handle
     the item merging.

     if it needs to move whole item bodies unchanged, the balancing code uses xmemcpy()
     adjusting the item headers after the move is done using the node handler.
*/

#include "../../forward.h"
#include "../../debug.h"
#include "../../key.h"
#include "../../coord.h"
#include "../plugin_header.h"
#include "../item/item.h"
#include "node.h"
#include "../plugin.h"
#include "../../znode.h"
#include "../../tree.h"
#include "../../super.h"
#include "../../reiser4.h"

/* return starting key of the leftmost item in the @node */
reiser4_internal reiser4_key *
leftmost_key_in_node(const znode * node /* node to query */ ,
		     reiser4_key * key /* resulting key */ )
{
	assert("nikita-1634", node != NULL);
	assert("nikita-1635", key != NULL);

	if (!node_is_empty(node)) {
		coord_t first_item;

		coord_init_first_unit(&first_item, (znode *) node);
		item_key_by_coord(&first_item, key);
	} else
		*key = *max_key();
	return key;
}

#if REISER4_DEBUG_OUTPUT
/* helper function: convert 4 bit integer to its hex representation */
/* Audited by: green(2002.06.12) */
static char
hex_to_ascii(const int hex /* hex digit */ )
{
	assert("nikita-1081", (0 <= hex) && (hex < 0x10));

	if (hex < 10)
		return '0' + hex;
	else
		return 'a' + hex - 10;
}

/* helper function used to indent output during recursive tree printing */
/* Audited by: green(2002.06.12) */
reiser4_internal void
indent(unsigned indentation)
{
	unsigned i;

	for (i = 0; i < indentation; ++i)
		printk("%.1i........", indentation - i);
}

/* helper function used to indent output for @node during recursive tree
   printing */
reiser4_internal void
indent_znode(const znode * node /* current node */ )
{
	if (znode_get_tree(node)->height < znode_get_level(node))
		indent(0);
	else
		indent(znode_get_tree(node)->height - znode_get_level(node));
}

/* debugging aid: output human readable information about @node */
reiser4_internal void
print_node_content(const char *prefix /* output prefix */ ,
		   const znode * node /* node to print */ ,
		   __u32 flags /* print flags */ )
{
	unsigned short i;
	coord_t coord;
	item_plugin *iplug;
	reiser4_key key;

	if (!znode_is_loaded(node)) {
		print_znode("znode is not loaded\n", node);
		return;
	}
	if (node_plugin_by_node(node)->print != NULL) {
		indent_znode(node);
		node_plugin_by_node(node)->print(prefix, node, flags);

		indent_znode(node);
		print_key("LDKEY", &node->ld_key);

		indent_znode(node);
		print_key("RDKEY", &node->rd_key);
	}

	/*if( flags & REISER4_NODE_SILENT ) {return;} */

	coord.node = (znode *) node;
	coord.unit_pos = 0;
	coord.between = AT_UNIT;
	/*indent_znode (node); */
	for (i = 0; i < node_num_items(node); i++) {
		int j;
		int length;
		char *data;

		indent_znode(node);
		printk("%d: ", i);

		coord_set_item_pos(&coord, i);

		iplug = item_plugin_by_coord(&coord);
		print_plugin("\titem plugin", item_plugin_to_plugin(iplug));
		indent_znode(node);
		item_key_by_coord(&coord, &key);
		print_key("\titem key", &key);

		indent_znode(node);
		printk("\tlength %d\n", item_length_by_coord(&coord));
		indent_znode(node);
		iplug->b.print("\titem", &coord);

		data = item_body_by_coord(&coord);
		length = item_length_by_coord(&coord);
		indent_znode(node);
		printk("\titem length: %i, offset: %i\n", length, data - zdata(node));
		for (j = 0; j < length; ++j) {
			char datum;

			if ((j % 16) == 0) {
				/* next 16 bytes */
				if (j == 0) {
					indent_znode(node);
					printk("\tdata % .2i: ", j);
				} else {
					printk("\n");
					indent_znode(node);
					printk("\t     % .2i: ", j);
				}
			}
			datum = data[j];
			printk("%c", hex_to_ascii((datum & 0xf0) >> 4));
			printk("%c ", hex_to_ascii(datum & 0xf));
		}
		printk("\n");
		indent_znode(node);
		printk("======================\n");
	}
	printk("\n");
}

/* debugging aid: output human readable information about @node
   the same as the above, but items to be printed must be specified */
reiser4_internal void
print_node_items(const char *prefix /* output prefix */ ,
		 const znode * node /* node to print */ ,
		 __u32 flags /* print flags */ ,
		 unsigned from, unsigned count)
{
	unsigned i;
	coord_t coord;
	item_plugin *iplug;
	reiser4_key key;

	if (!znode_is_loaded(node)) {
		print_znode("znode is not loaded\n", node);
		return;
	}
	if (node_plugin_by_node(node)->print != NULL) {
		indent_znode(node);
		node_plugin_by_node(node)->print(prefix, node, flags);

		indent_znode(node);
		print_key("LDKEY", &node->ld_key);

		indent_znode(node);
		print_key("RDKEY", &node->rd_key);
	}

	/*if( flags & REISER4_NODE_SILENT ) {return;} */

	coord.node = (znode *) node;
	coord.unit_pos = 0;
	coord.between = AT_UNIT;
	/*indent_znode (node); */
	if (from >= node_num_items(node) || from + count > node_num_items(node)) {
		printk("there are no those items (%u-%u) in the node (%u)\n",
		       from, from + count - 1, node_num_items(node));
		return;
	}

	for (i = from; i < from + count; i++) {
		int j;
		int length;
		char *data;

		indent_znode(node);
		printk("%d: ", i);

		coord_set_item_pos(&coord, i);

		iplug = item_plugin_by_coord(&coord);
		print_plugin("\titem plugin", item_plugin_to_plugin(iplug));
		indent_znode(node);
		item_key_by_coord(&coord, &key);
		print_key("\titem key", &key);

		if (iplug->b.print) {
			indent_znode(node);
			printk("\tlength %d\n", item_length_by_coord(&coord));
			indent_znode(node);
			iplug->b.print("\titem", &coord);
		}
		data = item_body_by_coord(&coord);
		length = item_length_by_coord(&coord);
		indent_znode(node);
		printk("\titem length: %i, offset: %i\n", length, data - zdata(node));
		for (j = 0; j < length; ++j) {
			char datum;

			if ((j % 16) == 0) {
				/* next 16 bytes */
				if (j == 0) {
					indent_znode(node);
					printk("\tdata % .2i: ", j);
				} else {
					printk("\n");
					indent_znode(node);
					printk("\t     % .2i: ", j);
				}
			}
			datum = data[j];
			printk("%c", hex_to_ascii((datum & 0xf0) >> 4));
			printk("%c ", hex_to_ascii(datum & 0xf));
		}
		printk("\n");
		indent_znode(node);
		printk("======================\n");
	}
	printk("\n");
}
#endif

#if REISER4_DEBUG_NODE
/* debugging aid: check consistency of @node content */
void
node_check(znode * node /* node to check */ ,
	   __u32 flags /* check flags */ )
{
	const char *mes;
	int result;
	reiser4_tree *tree;

	assert("nikita-3534", schedulable());

	if (!reiser4_is_debugged(reiser4_get_current_sb(), REISER4_CHECK_NODE))
		return;

	if (get_current_context()->disable_node_check)
		return;
	tree = znode_get_tree(node);

	if (znode_above_root(node))
		return;
	if (znode_just_created(node))
		return;

	zload(node);
	result = node_plugin_by_node(node)->check(node, flags, &mes);
	if (result != 0) {
		printk("%s\n", mes);
		print_node_content("check", node, ~0u);
		reiser4_panic("vs-273", "node corrupted");
	}
	zrelse(node);
}
#endif

node_plugin node_plugins[LAST_NODE_ID] = {
	[NODE40_ID] = {
		.h = {
			.type_id = REISER4_NODE_PLUGIN_TYPE,
			.id = NODE40_ID,
			.pops = NULL,
			.label = "unified",
			.desc = "unified node layout",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO,
		},
		.item_overhead = item_overhead_node40,
		.free_space = free_space_node40,
		.lookup = lookup_node40,
		.num_of_items = num_of_items_node40,
		.item_by_coord = item_by_coord_node40,
		.length_by_coord = length_by_coord_node40,
		.plugin_by_coord = plugin_by_coord_node40,
		.key_at = key_at_node40,
		.estimate = estimate_node40,
		.check = check_node40,
		.parse = parse_node40,
		.init = init_node40,
#ifdef GUESS_EXISTS
		.guess = guess_node40,
#endif
#if REISER4_DEBUG_OUTPUT
		.print = print_node40,
#endif
		.change_item_size = change_item_size_node40,
		.create_item = create_item_node40,
		.update_item_key = update_item_key_node40,
		.cut_and_kill = kill_node40,
		.cut = cut_node40,
		.shift = shift_node40,
		.shrink_item = shrink_item_node40,
		.fast_insert = fast_insert_node40,
		.fast_paste = fast_paste_node40,
		.fast_cut = fast_cut_node40,
		.max_item_size = max_item_size_node40,
		.prepare_removal = prepare_removal_node40,
		.set_item_plugin = set_item_plugin_node40
	}
};

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
