/******************************************************************************
 *
 * Module Name: utalloc - local cache and memory allocation routines
 *              $Revision: 100 $
 *
 *****************************************************************************/

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
#include "acparser.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acglobal.h"

#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("utalloc")


/******************************************************************************
 *
 * FUNCTION:    Acpi_ut_release_to_cache
 *
 * PARAMETERS:  List_id             - Memory list/cache ID
 *              Object              - The object to be released
 *
 * RETURN:      None
 *
 * DESCRIPTION: Release an object to the specified cache.  If cache is full,
 *              the object is deleted.
 *
 ******************************************************************************/

void
acpi_ut_release_to_cache (
	u32                     list_id,
	void                    *object)
{
	ACPI_MEMORY_LIST        *cache_info;


	FUNCTION_ENTRY ();


	/* If walk cache is full, just free this wallkstate object */

	cache_info = &acpi_gbl_memory_lists[list_id];
	if (cache_info->cache_depth >= cache_info->max_cache_depth) {
		ACPI_MEM_FREE (object);
		ACPI_MEM_TRACKING (cache_info->total_freed++);
	}

	/* Otherwise put this object back into the cache */

	else {
		acpi_ut_acquire_mutex (ACPI_MTX_CACHES);

		/* Mark the object as cached */

		MEMSET (object, 0xCA, cache_info->object_size);

		/* Put the object at the head of the cache list */

		* (char **) (((char *) object) + cache_info->link_offset) = cache_info->list_head;
		cache_info->list_head = object;
		cache_info->cache_depth++;

		acpi_ut_release_mutex (ACPI_MTX_CACHES);
	}
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ut_acquire_from_cache
 *
 * PARAMETERS:  List_id             - Memory list ID
 *
 * RETURN:      A requested object.  NULL if the object could not be
 *              allocated.
 *
 * DESCRIPTION: Get an object from the specified cache.  If cache is empty,
 *              the object is allocated.
 *
 ******************************************************************************/

void *
acpi_ut_acquire_from_cache (
	u32                     list_id)
{
	ACPI_MEMORY_LIST        *cache_info;
	void                    *object;


	PROC_NAME ("Ut_acquire_from_cache");


	cache_info = &acpi_gbl_memory_lists[list_id];
	acpi_ut_acquire_mutex (ACPI_MTX_CACHES);
	ACPI_MEM_TRACKING (cache_info->cache_requests++);

	/* Check the cache first */

	if (cache_info->list_head) {
		/* There is an object available, use it */

		object = cache_info->list_head;
		cache_info->list_head = * (char **) (((char *) object) + cache_info->link_offset);

		ACPI_MEM_TRACKING (cache_info->cache_hits++);
		cache_info->cache_depth--;

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Object %p from %s\n",
			object, acpi_gbl_memory_lists[list_id].list_name));
#endif

		acpi_ut_release_mutex (ACPI_MTX_CACHES);

		/* Clear (zero) the previously used Object */

		MEMSET (object, 0, cache_info->object_size);
	}

	else {
		/* The cache is empty, create a new object */

		/* Avoid deadlock with ACPI_MEM_CALLOCATE */

		acpi_ut_release_mutex (ACPI_MTX_CACHES);

		object = ACPI_MEM_CALLOCATE (cache_info->object_size);
		ACPI_MEM_TRACKING (cache_info->total_allocated++);
	}

	return (object);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_generic_cache
 *
 * PARAMETERS:  List_id         - Memory list ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free all objects within the requested cache.
 *
 ******************************************************************************/

void
acpi_ut_delete_generic_cache (
	u32                     list_id)
{
	ACPI_MEMORY_LIST        *cache_info;
	char                    *next;


	FUNCTION_ENTRY ();


	cache_info = &acpi_gbl_memory_lists[list_id];
	while (cache_info->list_head) {
		/* Delete one cached state object */

		next = * (char **) (((char *) cache_info->list_head) + cache_info->link_offset);
		ACPI_MEM_FREE (cache_info->list_head);

		cache_info->list_head = next;
		cache_info->cache_depth--;
	}
}


#ifdef ACPI_DBG_TRACK_ALLOCATIONS


/*
 * These procedures are used for tracking memory leaks in the subsystem, and
 * they get compiled out when the ACPI_DBG_TRACK_ALLOCATIONS is not set.
 *
 * Each memory allocation is tracked via a doubly linked list.  Each
 * element contains the caller's component, module name, function name, and
 * line number.  Acpi_ut_allocate and Acpi_ut_callocate call
 * Acpi_ut_add_element_to_alloc_list to add an element to the list; deletion
 * occurs in the body of Acpi_ut_free.
 */


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_search_alloc_list
 *
 * PARAMETERS:  Address             - Address of allocated memory
 *
 * RETURN:      A list element if found; NULL otherwise.
 *
 * DESCRIPTION: Searches for an element in the global allocation tracking list.
 *
 ******************************************************************************/

ACPI_DEBUG_MEM_BLOCK *
acpi_ut_search_alloc_list (
	u32                     list_id,
	void                    *address)
{
	ACPI_DEBUG_MEM_BLOCK    *element;


	FUNCTION_ENTRY ();


	if (list_id > ACPI_MEM_LIST_MAX) {
		return (NULL);
	}

	element = acpi_gbl_memory_lists[list_id].list_head;

	/* Search for the address. */

	while (element) {
		if (element == address) {
			return (element);
		}

		element = element->next;
	}

	return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_add_element_to_alloc_list
 *
 * PARAMETERS:  Address             - Address of allocated memory
 *              Size                - Size of the allocation
 *              Alloc_type          - MEM_MALLOC or MEM_CALLOC
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Inserts an element into the global allocation tracking list.
 *
 ******************************************************************************/

acpi_status
acpi_ut_add_element_to_alloc_list (
	u32                     list_id,
	ACPI_DEBUG_MEM_BLOCK    *address,
	u32                     size,
	u8                      alloc_type,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_MEMORY_LIST        *mem_list;
	ACPI_DEBUG_MEM_BLOCK    *element;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_PTR ("Ut_add_element_to_alloc_list", address);


	if (list_id > ACPI_MEM_LIST_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	mem_list = &acpi_gbl_memory_lists[list_id];
	acpi_ut_acquire_mutex (ACPI_MTX_MEMORY);

	/*
	 * Search list for this address to make sure it is not already on the list.
	 * This will catch several kinds of problems.
	 */

	element = acpi_ut_search_alloc_list (list_id, address);
	if (element) {
		REPORT_ERROR (("Ut_add_element_to_alloc_list: Address already present in list! (%p)\n",
			address));

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Element %p Address %p\n", element, address));

		goto unlock_and_exit;
	}

	/* Fill in the instance data. */

	address->size      = size;
	address->alloc_type = alloc_type;
	address->component = component;
	address->line      = line;

	STRNCPY (address->module, module, MAX_MODULE_NAME);

	/* Insert at list head */

	if (mem_list->list_head) {
		((ACPI_DEBUG_MEM_BLOCK *)(mem_list->list_head))->previous = address;
	}

	address->next = mem_list->list_head;
	address->previous = NULL;

	mem_list->list_head = address;


unlock_and_exit:
	acpi_ut_release_mutex (ACPI_MTX_MEMORY);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_element_from_alloc_list
 *
 * PARAMETERS:  Address             - Address of allocated memory
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:
 *
 * DESCRIPTION: Deletes an element from the global allocation tracking list.
 *
 ******************************************************************************/

acpi_status
acpi_ut_delete_element_from_alloc_list (
	u32                     list_id,
	ACPI_DEBUG_MEM_BLOCK    *address,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_MEMORY_LIST        *mem_list;


	FUNCTION_TRACE ("Ut_delete_element_from_alloc_list");


	if (list_id > ACPI_MEM_LIST_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	mem_list = &acpi_gbl_memory_lists[list_id];
	if (NULL == mem_list->list_head) {
		/* No allocations! */

		_REPORT_ERROR (module, line, component,
				("Ut_delete_element_from_alloc_list: Empty allocation list, nothing to free!\n"));

		return_ACPI_STATUS (AE_OK);
	}


	acpi_ut_acquire_mutex (ACPI_MTX_MEMORY);

	/* Unlink */

	if (address->previous) {
		(address->previous)->next = address->next;
	}
	else {
		mem_list->list_head = address->next;
	}

	if (address->next) {
		(address->next)->previous = address->previous;
	}


	/* Mark the segment as deleted */

	MEMSET (&address->user_space, 0xEA, address->size);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "Freeing size %X\n", address->size));

	acpi_ut_release_mutex (ACPI_MTX_MEMORY);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_dump_allocation_info
 *
 * PARAMETERS:
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print some info about the outstanding allocations.
 *
 ******************************************************************************/

void
acpi_ut_dump_allocation_info (
	void)
{
/*
	ACPI_MEMORY_LIST        *Mem_list;
*/

	FUNCTION_TRACE ("Ut_dump_allocation_info");

/*
	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Current allocations",
			  Mem_list->Current_count,
			  ROUND_UP_TO_1_k (Mem_list->Current_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Max concurrent allocations",
			  Mem_list->Max_concurrent_count,
			  ROUND_UP_TO_1_k (Mem_list->Max_concurrent_size)));


	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Total (all) internal objects",
			  Running_object_count,
			  ROUND_UP_TO_1_k (Running_object_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Total (all) allocations",
			  Running_alloc_count,
			  ROUND_UP_TO_1_k (Running_alloc_size)));


	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Current Nodes",
			  Acpi_gbl_Current_node_count,
			  ROUND_UP_TO_1_k (Acpi_gbl_Current_node_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Max Nodes",
			  Acpi_gbl_Max_concurrent_node_count,
			  ROUND_UP_TO_1_k ((Acpi_gbl_Max_concurrent_node_count * sizeof (acpi_namespace_node)))));
*/
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_dump_current_allocations
 *
 * PARAMETERS:  Component           - Component(s) to dump info for.
 *              Module              - Module to dump info for.  NULL means all.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a list of all outstanding allocations.
 *
 ******************************************************************************/

void
acpi_ut_dump_current_allocations (
	u32                     component,
	NATIVE_CHAR             *module)
{
	ACPI_DEBUG_MEM_BLOCK    *element;
	u32                     i;


	FUNCTION_TRACE ("Ut_dump_current_allocations");


	element = acpi_gbl_memory_lists[0].list_head;
	if (element == NULL) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
				"No outstanding allocations.\n"));
		return_VOID;
	}


	/*
	 * Walk the allocation list.
	 */
	acpi_ut_acquire_mutex (ACPI_MTX_MEMORY);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
		"Outstanding allocations:\n"));

	for (i = 1; ; i++)  /* Just a counter */ {
		if ((element->component & component) &&
			((module == NULL) || (0 == STRCMP (module, element->module)))) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
					 "%p Len %04lX %9.9s-%ld",
					 &element->user_space, element->size, element->module,
					 element->line));

			/* Most of the elements will be internal objects. */

			switch (((acpi_operand_object  *)
				(&element->user_space))->common.data_type) {
			case ACPI_DESC_TYPE_INTERNAL:
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
						" Obj_type %s",
						acpi_ut_get_type_name (((acpi_operand_object *)(&element->user_space))->common.type)));
				break;

			case ACPI_DESC_TYPE_PARSER:
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
						" Parse_obj Opcode %04X",
						((acpi_parse_object *)(&element->user_space))->opcode));
				break;

			case ACPI_DESC_TYPE_NAMED:
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
						" Node %4.4s",
						&((acpi_namespace_node *)(&element->user_space))->name));
				break;

			case ACPI_DESC_TYPE_STATE:
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
						" State_obj"));
				break;
			}

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS, "\n"));
		}

		if (element->next == NULL) {
			break;
		}

		element = element->next;
	}

	acpi_ut_release_mutex (ACPI_MTX_MEMORY);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
		"Total number of unfreed allocations = %d(%X)\n", i,i));


	return_VOID;

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_allocate
 *
 * PARAMETERS:  Size                - Size of the allocation
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:      Address of the allocated memory on success, NULL on failure.
 *
 * DESCRIPTION: The subsystem's equivalent of malloc.
 *
 ******************************************************************************/

void *
acpi_ut_allocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_DEBUG_MEM_BLOCK    *address;
	acpi_status             status;


	FUNCTION_TRACE_U32 ("Ut_allocate", size);


	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		_REPORT_ERROR (module, line, component,
				("Ut_allocate: Attempt to allocate zero bytes\n"));
		size = 1;
	}

	address = acpi_os_allocate (size + sizeof (ACPI_DEBUG_MEM_BLOCK));
	if (!address) {
		/* Report allocation error */

		_REPORT_ERROR (module, line, component,
				("Ut_allocate: Could not allocate size %X\n", size));

		return_PTR (NULL);
	}

	status = acpi_ut_add_element_to_alloc_list (ACPI_MEM_LIST_GLOBAL, address, size,
			  MEM_MALLOC, component, module, line);
	if (ACPI_FAILURE (status)) {
		acpi_os_free (address);
		return_PTR (NULL);
	}

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_allocated++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size += size;

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p Size %X\n", address, size));

	return_PTR ((void *) &address->user_space);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_callocate
 *
 * PARAMETERS:  Size                - Size of the allocation
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:      Address of the allocated memory on success, NULL on failure.
 *
 * DESCRIPTION: Subsystem equivalent of calloc.
 *
 ******************************************************************************/

void *
acpi_ut_callocate (
	u32                     size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_DEBUG_MEM_BLOCK    *address;
	acpi_status             status;


	FUNCTION_TRACE_U32 ("Ut_callocate", size);


	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		_REPORT_ERROR (module, line, component,
				("Ut_callocate: Attempt to allocate zero bytes\n"));
		return_PTR (NULL);
	}


	address = acpi_os_callocate (size + sizeof (ACPI_DEBUG_MEM_BLOCK));
	if (!address) {
		/* Report allocation error */

		_REPORT_ERROR (module, line, component,
				("Ut_callocate: Could not allocate size %X\n", size));
		return_PTR (NULL);
	}

	status = acpi_ut_add_element_to_alloc_list (ACPI_MEM_LIST_GLOBAL, address, size,
			   MEM_CALLOC, component, module, line);
	if (ACPI_FAILURE (status)) {
		acpi_os_free (address);
		return_PTR (NULL);
	}

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_allocated++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size += size;

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p Size %X\n", address, size));
	return_PTR ((void *) &address->user_space);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_free
 *
 * PARAMETERS:  Address             - Address of the memory to deallocate
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:      None
 *
 * DESCRIPTION: Frees the memory at Address
 *
 ******************************************************************************/

void
acpi_ut_free (
	void                    *address,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_DEBUG_MEM_BLOCK    *debug_block;


	FUNCTION_TRACE_PTR ("Ut_free", address);


	if (NULL == address) {
		_REPORT_ERROR (module, line, component,
			("Acpi_ut_free: Trying to delete a NULL address\n"));

		return_VOID;
	}

	debug_block = (ACPI_DEBUG_MEM_BLOCK *)
			  (((char *) address) - sizeof (ACPI_DEBUG_MEM_HEADER));

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_freed++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size -= debug_block->size;

	acpi_ut_delete_element_from_alloc_list (ACPI_MEM_LIST_GLOBAL, debug_block,
			component, module, line);
	acpi_os_free (debug_block);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p freed\n", address));

	return_VOID;
}

#endif  /* #ifdef ACPI_DBG_TRACK_ALLOCATIONS */

