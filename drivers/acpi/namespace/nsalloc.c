/*******************************************************************************
 *
 * Module Name: nsalloc - Namespace allocation and deletion utilities
 *              $Revision: 60 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "acnamesp.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsalloc")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_create_node
 *
 * PARAMETERS:  Acpi_name       - Name of the new node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a namespace node
 *
 ******************************************************************************/

acpi_namespace_node *
acpi_ns_create_node (
	u32                     name)
{
	acpi_namespace_node     *node;


	FUNCTION_TRACE ("Ns_create_node");


	node = ACPI_MEM_CALLOCATE (sizeof (acpi_namespace_node));
	if (!node) {
		return_PTR (NULL);
	}

	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_NSNODE].total_allocated++);

	node->data_type      = ACPI_DESC_TYPE_NAMED;
	node->name           = name;
	node->reference_count = 1;

	return_PTR (node);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_node
 *
 * PARAMETERS:  Node            - Node to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a namespace node
 *
 ******************************************************************************/

void
acpi_ns_delete_node (
	acpi_namespace_node     *node)
{
	acpi_namespace_node     *parent_node;
	acpi_namespace_node     *prev_node;
	acpi_namespace_node     *next_node;


	FUNCTION_TRACE_PTR ("Ns_delete_node", node);


	parent_node = acpi_ns_get_parent_object (node);

	prev_node = NULL;
	next_node = parent_node->child;

	while (next_node != node) {
		prev_node = next_node;
		next_node = prev_node->peer;
	}

	if (prev_node) {
		prev_node->peer = next_node->peer;
		if (next_node->flags & ANOBJ_END_OF_PEER_LIST) {
			prev_node->flags |= ANOBJ_END_OF_PEER_LIST;
		}
	}
	else {
		parent_node->child = next_node->peer;
	}


	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_NSNODE].total_freed++);

	/*
	 * Detach an object if there is one
	 */
	if (node->object) {
		acpi_ns_detach_object (node);
	}

	ACPI_MEM_FREE (node);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_install_node
 *
 * PARAMETERS:  Walk_state      - Current state of the walk
 *              Parent_node     - The parent of the new Node
 *              Node            - The new Node to install
 *              Type            - ACPI object type of the new Node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a new entry within a namespace table.
 *
 ******************************************************************************/

void
acpi_ns_install_node (
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *parent_node,   /* Parent */
	acpi_namespace_node     *node,          /* New Child*/
	acpi_object_type8       type)
{
	u16                     owner_id = TABLE_ID_DSDT;
	acpi_namespace_node     *child_node;


	FUNCTION_TRACE ("Ns_install_node");


	/*
	 * Get the owner ID from the Walk state
	 * The owner ID is used to track table deletion and
	 * deletion of objects created by methods
	 */
	if (walk_state) {
		owner_id = walk_state->owner_id;
	}


	/* link the new entry into the parent and existing children */

	/* TBD: Could be first, last, or alphabetic */

	child_node = parent_node->child;
	if (!child_node) {
		parent_node->child = node;
	}

	else {
		while (!(child_node->flags & ANOBJ_END_OF_PEER_LIST)) {
			child_node = child_node->peer;
		}

		child_node->peer = node;

		/* Clear end-of-list flag */

		child_node->flags &= ~ANOBJ_END_OF_PEER_LIST;
	}

	/* Init the new entry */

	node->owner_id  = owner_id;
	node->flags     |= ANOBJ_END_OF_PEER_LIST;
	node->peer      = parent_node;


	/*
	 * If adding a name with unknown type, or having to
	 * add the region in order to define fields in it, we
	 * have a forward reference.
	 */
	if ((ACPI_TYPE_ANY == type) ||
		(INTERNAL_TYPE_FIELD_DEFN == type) ||
		(INTERNAL_TYPE_BANK_FIELD_DEFN == type)) {
		/*
		 * We don't want to abort here, however!
		 * We will fill in the actual type when the
		 * real definition is found later.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "[%4.4s] is a forward reference\n",
			(char*)&node->name));
	}

	/*
	 * The Def_field_defn and Bank_field_defn cases are actually
	 * looking up the Region in which the field will be defined
	 */
	if ((INTERNAL_TYPE_FIELD_DEFN == type) ||
		(INTERNAL_TYPE_BANK_FIELD_DEFN == type)) {
		type = ACPI_TYPE_REGION;
	}

	/*
	 * Scope, Def_any, and Index_field_defn are bogus "types" which do
	 * not actually have anything to do with the type of the name
	 * being looked up.  Save any other value of Type as the type of
	 * the entry.
	 */
	if ((type != INTERNAL_TYPE_SCOPE) &&
		(type != INTERNAL_TYPE_DEF_ANY) &&
		(type != INTERNAL_TYPE_INDEX_FIELD_DEFN)) {
		node->type = (u8) type;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "%4.4s added to %p at %p\n",
		(char*)&node->name, parent_node, node));

	/*
	 * Increment the reference count(s) of all parents up to
	 * the root!
	 */
	while ((node = acpi_ns_get_parent_object (node)) != NULL) {
		node->reference_count++;
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_children
 *
 * PARAMETERS:  Parent_node     - Delete this objects children
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all children of the parent object. Deletes a
 *              "scope".
 *
 ******************************************************************************/

void
acpi_ns_delete_children (
	acpi_namespace_node     *parent_node)
{
	acpi_namespace_node     *child_node;
	acpi_namespace_node     *next_node;
	u8                      flags;


	FUNCTION_TRACE_PTR ("Ns_delete_children", parent_node);


	if (!parent_node) {
		return_VOID;
	}

	/* If no children, all done! */

	child_node = parent_node->child;
	if (!child_node) {
		return_VOID;
	}

	/*
	 * Deallocate all children at this level
	 */
	do {
		/* Get the things we need */

		next_node   = child_node->peer;
		flags       = child_node->flags;

		/* Grandchildren should have all been deleted already */

		if (child_node->child) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Found a grandchild! P=%p C=%p\n",
				parent_node, child_node));
		}

		/* Now we can free this child object */

		ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_NSNODE].total_freed++);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object %p, Remaining %X\n",
			child_node, acpi_gbl_current_node_count));

		/*
		 * Detach an object if there is one, then free the child node
		 */
		acpi_ns_detach_object (child_node);
		ACPI_MEM_FREE (child_node);

		/* And move on to the next child in the list */

		child_node = next_node;

	} while (!(flags & ANOBJ_END_OF_PEER_LIST));


	/* Clear the parent's child pointer */

	parent_node->child = NULL;

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_namespace_subtree
 *
 * PARAMETERS:  Parent_node     - Root of the subtree to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a subtree of the namespace.  This includes all objects
 *              stored within the subtree.  Scope tables are deleted also
 *
 ******************************************************************************/

acpi_status
acpi_ns_delete_namespace_subtree (
	acpi_namespace_node     *parent_node)
{
	acpi_namespace_node     *child_node = NULL;
	u32                     level = 1;


	FUNCTION_TRACE ("Ns_delete_namespace_subtree");


	if (!parent_node) {
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Traverse the tree of objects until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {
		/* Get the next node in this scope (NULL if none) */

		child_node = acpi_ns_get_next_node (ACPI_TYPE_ANY, parent_node,
				 child_node);
		if (child_node) {
			/* Found a child node - detach any attached object */

			acpi_ns_detach_object (child_node);

			/* Check if this node has any children */

			if (acpi_ns_get_next_node (ACPI_TYPE_ANY, child_node, 0)) {
				/*
				 * There is at least one child of this node,
				 * visit the node
				 */
				level++;
				parent_node   = child_node;
				child_node    = 0;
			}
		}

		else {
			/*
			 * No more children of this parent node.
			 * Move up to the grandparent.
			 */
			level--;

			/*
			 * Now delete all of the children of this parent
			 * all at the same time.
			 */
			acpi_ns_delete_children (parent_node);

			/* New "last child" is this parent node */

			child_node = parent_node;

			/* Move up the tree to the grandparent */

			parent_node = acpi_ns_get_parent_object (parent_node);
		}
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_remove_reference
 *
 * PARAMETERS:  Node           - Named node whose reference count is to be
 *                               decremented
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Remove a Node reference.  Decrements the reference count
 *              of all parent Nodes up to the root.  Any node along
 *              the way that reaches zero references is freed.
 *
 ******************************************************************************/

static void
acpi_ns_remove_reference (
	acpi_namespace_node     *node)
{
	acpi_namespace_node     *next_node;


	FUNCTION_ENTRY ();


	/*
	 * Decrement the reference count(s) of this node and all
	 * nodes up to the root,  Delete anything with zero remaining references.
	 */
	next_node = node;
	while (next_node) {
		/* Decrement the reference count on this node*/

		next_node->reference_count--;

		/* Delete the node if no more references */

		if (!next_node->reference_count) {
			/* Delete all children and delete the node */

			acpi_ns_delete_children (next_node);
			acpi_ns_delete_node (next_node);
		}

		/* Move up to parent */

		next_node = acpi_ns_get_parent_object (next_node);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_namespace_by_owner
 *
 * PARAMETERS:  Owner_id    - All nodes with this owner will be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete entries within the namespace that are owned by a
 *              specific ID.  Used to delete entire ACPI tables.  All
 *              reference counts are updated.
 *
 ******************************************************************************/

acpi_status
acpi_ns_delete_namespace_by_owner (
	u16                     owner_id)
{
	acpi_namespace_node     *child_node;
	u32                     level;
	acpi_namespace_node     *parent_node;


	FUNCTION_TRACE ("Ns_delete_namespace_by_owner");


	parent_node = acpi_gbl_root_node;
	child_node  = 0;
	level       = 1;

	/*
	 * Traverse the tree of nodes until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {
		/* Get the next node in this scope (NULL if none) */

		child_node = acpi_ns_get_next_node (ACPI_TYPE_ANY, parent_node,
				 child_node);
		if (child_node) {
			if (child_node->owner_id == owner_id) {
				/* Found a child node - detach any attached object */

				acpi_ns_detach_object (child_node);
			}

			/* Check if this node has any children */

			if (acpi_ns_get_next_node (ACPI_TYPE_ANY, child_node, 0)) {
				/*
				 * There is at least one child of this node,
				 * visit the node
				 */
				level++;
				parent_node   = child_node;
				child_node    = 0;
			}

			else if (child_node->owner_id == owner_id) {
				acpi_ns_remove_reference (child_node);
			}
		}

		else {
			/*
			 * No more children of this parent node.
			 * Move up to the grandparent.
			 */
			level--;

			if (level != 0) {
				if (parent_node->owner_id == owner_id) {
					acpi_ns_remove_reference (parent_node);
				}
			}

			/* New "last child" is this parent node */

			child_node = parent_node;

			/* Move up the tree to the grandparent */

			parent_node = acpi_ns_get_parent_object (parent_node);
		}
	}

	return_ACPI_STATUS (AE_OK);
}


