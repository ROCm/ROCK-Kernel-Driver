/******************************************************************************
 *
 * Module Name: utalloc - local cache and memory allocation routines
 *              $Revision: 128 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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

#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("utalloc")


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


	ACPI_FUNCTION_ENTRY ();


	/* If walk cache is full, just free this wallkstate object */

	cache_info = &acpi_gbl_memory_lists[list_id];
	if (cache_info->cache_depth >= cache_info->max_cache_depth) {
		ACPI_MEM_FREE (object);
		ACPI_MEM_TRACKING (cache_info->total_freed++);
	}

	/* Otherwise put this object back into the cache */

	else {
		if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_CACHES))) {
			return;
		}

		/* Mark the object as cached */

		ACPI_MEMSET (object, 0xCA, cache_info->object_size);
		ACPI_SET_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_CACHED);

		/* Put the object at the head of the cache list */

		* (ACPI_CAST_INDIRECT_PTR (char, &(((char *) object)[cache_info->link_offset]))) = cache_info->list_head;
		cache_info->list_head = object;
		cache_info->cache_depth++;

		(void) acpi_ut_release_mutex (ACPI_MTX_CACHES);
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


	ACPI_FUNCTION_NAME ("Ut_acquire_from_cache");


	cache_info = &acpi_gbl_memory_lists[list_id];
	if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_CACHES))) {
		return (NULL);
	}

	ACPI_MEM_TRACKING (cache_info->cache_requests++);

	/* Check the cache first */

	if (cache_info->list_head) {
		/* There is an object available, use it */

		object = cache_info->list_head;
		cache_info->list_head = *(ACPI_CAST_INDIRECT_PTR (char, &(((char *) object)[cache_info->link_offset])));

		ACPI_MEM_TRACKING (cache_info->cache_hits++);
		cache_info->cache_depth--;

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Object %p from %s\n",
			object, acpi_gbl_memory_lists[list_id].list_name));
#endif

		if (ACPI_FAILURE (acpi_ut_release_mutex (ACPI_MTX_CACHES))) {
			return (NULL);
		}

		/* Clear (zero) the previously used Object */

		ACPI_MEMSET (object, 0, cache_info->object_size);
	}

	else {
		/* The cache is empty, create a new object */

		/* Avoid deadlock with ACPI_MEM_CALLOCATE */

		if (ACPI_FAILURE (acpi_ut_release_mutex (ACPI_MTX_CACHES))) {
			return (NULL);
		}

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


	ACPI_FUNCTION_ENTRY ();


	cache_info = &acpi_gbl_memory_lists[list_id];
	while (cache_info->list_head) {
		/* Delete one cached state object */

		next = *(ACPI_CAST_INDIRECT_PTR (char, &(((char *) cache_info->list_head)[cache_info->link_offset])));
		ACPI_MEM_FREE (cache_info->list_head);

		cache_info->list_head = next;
		cache_info->cache_depth--;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_validate_buffer
 *
 * PARAMETERS:  Buffer              - Buffer descriptor to be validated
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform parameter validation checks on an acpi_buffer
 *
 ******************************************************************************/

acpi_status
acpi_ut_validate_buffer (
	acpi_buffer             *buffer)
{

	/* Obviously, the structure pointer must be valid */

	if (!buffer) {
		return (AE_BAD_PARAMETER);
	}

	/* Special semantics for the length */

	if ((buffer->length == ACPI_NO_BUFFER)              ||
		(buffer->length == ACPI_ALLOCATE_BUFFER)        ||
		(buffer->length == ACPI_ALLOCATE_LOCAL_BUFFER)) {
		return (AE_OK);
	}

	/* Length is valid, the buffer pointer must be also */

	if (!buffer->pointer) {
		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_initialize_buffer
 *
 * PARAMETERS:  Required_length     - Length needed
 *              Buffer              - Buffer to be validated
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate that the buffer is of the required length or
 *              allocate a new buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ut_initialize_buffer (
	acpi_buffer             *buffer,
	ACPI_SIZE               required_length)
{
	acpi_status             status = AE_OK;


	switch (buffer->length) {
	case ACPI_NO_BUFFER:

		/* Set the exception and returned the required length */

		status = AE_BUFFER_OVERFLOW;
		break;


	case ACPI_ALLOCATE_BUFFER:

		/* Allocate a new buffer */

		buffer->pointer = acpi_os_allocate (required_length);
		if (!buffer->pointer) {
			return (AE_NO_MEMORY);
		}

		/* Clear the buffer */

		ACPI_MEMSET (buffer->pointer, 0, required_length);
		break;


	case ACPI_ALLOCATE_LOCAL_BUFFER:

		/* Allocate a new buffer with local interface to allow tracking */

		buffer->pointer = ACPI_MEM_ALLOCATE (required_length);
		if (!buffer->pointer) {
			return (AE_NO_MEMORY);
		}

		/* Clear the buffer */

		ACPI_MEMSET (buffer->pointer, 0, required_length);
		break;


	default:

		/* Validate the size of the buffer */

		if (buffer->length < required_length) {
			status = AE_BUFFER_OVERFLOW;
		}

		/* Clear the buffer */

		ACPI_MEMSET (buffer->pointer, 0, required_length);
		break;
	}

	buffer->length = required_length;
	return (status);
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
	ACPI_SIZE               size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	void                    *allocation;


	ACPI_FUNCTION_TRACE_U32 ("Ut_allocate", size);


	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_allocate: Attempt to allocate zero bytes\n"));
		size = 1;
	}

	allocation = acpi_os_allocate (size);
	if (!allocation) {
		/* Report allocation error */

		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_allocate: Could not allocate size %X\n", (u32) size));

		return_PTR (NULL);
	}

	return_PTR (allocation);
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
	ACPI_SIZE               size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	void                    *allocation;


	ACPI_FUNCTION_TRACE_U32 ("Ut_callocate", size);


	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_callocate: Attempt to allocate zero bytes\n"));
		return_PTR (NULL);
	}

	allocation = acpi_os_allocate (size);
	if (!allocation) {
		/* Report allocation error */

		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_callocate: Could not allocate size %X\n", (u32) size));
		return_PTR (NULL);
	}

	/* Clear the memory block */

	ACPI_MEMSET (allocation, 0, size);
	return_PTR (allocation);
}


#ifdef ACPI_DBG_TRACK_ALLOCATIONS
/*
 * These procedures are used for tracking memory leaks in the subsystem, and
 * they get compiled out when the ACPI_DBG_TRACK_ALLOCATIONS is not set.
 *
 * Each memory allocation is tracked via a doubly linked list.  Each
 * element contains the caller's component, module name, function name, and
 * line number.  Acpi_ut_allocate and Acpi_ut_callocate call
 * Acpi_ut_track_allocation to add an element to the list; deletion
 * occurs in the body of Acpi_ut_free.
 */


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_allocate_and_track
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
acpi_ut_allocate_and_track (
	ACPI_SIZE               size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	acpi_debug_mem_block    *allocation;
	acpi_status             status;


	allocation = acpi_ut_allocate (size + sizeof (acpi_debug_mem_block), component,
			  module, line);
	if (!allocation) {
		return (NULL);
	}

	status = acpi_ut_track_allocation (ACPI_MEM_LIST_GLOBAL, allocation, size,
			  ACPI_MEM_MALLOC, component, module, line);
	if (ACPI_FAILURE (status)) {
		acpi_os_free (allocation);
		return (NULL);
	}

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_allocated++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size += (u32) size;

	return ((void *) &allocation->user_space);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_callocate_and_track
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
acpi_ut_callocate_and_track (
	ACPI_SIZE               size,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	acpi_debug_mem_block    *allocation;
	acpi_status             status;


	allocation = acpi_ut_callocate (size + sizeof (acpi_debug_mem_block), component,
			  module, line);
	if (!allocation) {
		/* Report allocation error */

		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_callocate: Could not allocate size %X\n", (u32) size));
		return (NULL);
	}

	status = acpi_ut_track_allocation (ACPI_MEM_LIST_GLOBAL, allocation, size,
			   ACPI_MEM_CALLOC, component, module, line);
	if (ACPI_FAILURE (status)) {
		acpi_os_free (allocation);
		return (NULL);
	}

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_allocated++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size += (u32) size;

	return ((void *) &allocation->user_space);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_free_and_track
 *
 * PARAMETERS:  Allocation          - Address of the memory to deallocate
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *
 * RETURN:      None
 *
 * DESCRIPTION: Frees the memory at Allocation
 *
 ******************************************************************************/

void
acpi_ut_free_and_track (
	void                    *allocation,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	acpi_debug_mem_block    *debug_block;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ut_free", allocation);


	if (NULL == allocation) {
		_ACPI_REPORT_ERROR (module, line, component,
			("Acpi_ut_free: Attempt to delete a NULL address\n"));

		return_VOID;
	}

	debug_block = ACPI_CAST_PTR (acpi_debug_mem_block,
			  (((char *) allocation) - sizeof (acpi_debug_mem_header)));

	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].total_freed++;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].current_total_size -= debug_block->size;

	status = acpi_ut_remove_allocation (ACPI_MEM_LIST_GLOBAL, debug_block,
			  component, module, line);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not free memory, %s\n",
			acpi_format_exception (status)));
	}

	acpi_os_free (debug_block);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p freed\n", allocation));

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_find_allocation
 *
 * PARAMETERS:  Allocation             - Address of allocated memory
 *
 * RETURN:      A list element if found; NULL otherwise.
 *
 * DESCRIPTION: Searches for an element in the global allocation tracking list.
 *
 ******************************************************************************/

acpi_debug_mem_block *
acpi_ut_find_allocation (
	u32                     list_id,
	void                    *allocation)
{
	acpi_debug_mem_block    *element;


	ACPI_FUNCTION_ENTRY ();


	if (list_id > ACPI_MEM_LIST_MAX) {
		return (NULL);
	}

	element = acpi_gbl_memory_lists[list_id].list_head;

	/* Search for the address. */

	while (element) {
		if (element == allocation) {
			return (element);
		}

		element = element->next;
	}

	return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_track_allocation
 *
 * PARAMETERS:  Allocation          - Address of allocated memory
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
acpi_ut_track_allocation (
	u32                     list_id,
	acpi_debug_mem_block    *allocation,
	ACPI_SIZE               size,
	u8                      alloc_type,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_MEMORY_LIST        *mem_list;
	acpi_debug_mem_block    *element;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("Ut_track_allocation", allocation);


	if (list_id > ACPI_MEM_LIST_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	mem_list = &acpi_gbl_memory_lists[list_id];
	status = acpi_ut_acquire_mutex (ACPI_MTX_MEMORY);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Search list for this address to make sure it is not already on the list.
	 * This will catch several kinds of problems.
	 */

	element = acpi_ut_find_allocation (list_id, allocation);
	if (element) {
		ACPI_REPORT_ERROR (("Ut_track_allocation: Allocation already present in list! (%p)\n",
			allocation));

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Element %p Address %p\n", element, allocation));

		goto unlock_and_exit;
	}

	/* Fill in the instance data. */

	allocation->size      = (u32) size;
	allocation->alloc_type = alloc_type;
	allocation->component = component;
	allocation->line      = line;

	ACPI_STRNCPY (allocation->module, module, ACPI_MAX_MODULE_NAME);

	/* Insert at list head */

	if (mem_list->list_head) {
		((acpi_debug_mem_block *)(mem_list->list_head))->previous = allocation;
	}

	allocation->next = mem_list->list_head;
	allocation->previous = NULL;

	mem_list->list_head = allocation;


unlock_and_exit:
	status = acpi_ut_release_mutex (ACPI_MTX_MEMORY);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_remove_allocation
 *
 * PARAMETERS:  Allocation          - Address of allocated memory
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
acpi_ut_remove_allocation (
	u32                     list_id,
	acpi_debug_mem_block    *allocation,
	u32                     component,
	NATIVE_CHAR             *module,
	u32                     line)
{
	ACPI_MEMORY_LIST        *mem_list;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ut_remove_allocation");


	if (list_id > ACPI_MEM_LIST_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	mem_list = &acpi_gbl_memory_lists[list_id];
	if (NULL == mem_list->list_head) {
		/* No allocations! */

		_ACPI_REPORT_ERROR (module, line, component,
				("Ut_remove_allocation: Empty allocation list, nothing to free!\n"));

		return_ACPI_STATUS (AE_OK);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_MEMORY);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Unlink */

	if (allocation->previous) {
		(allocation->previous)->next = allocation->next;
	}
	else {
		mem_list->list_head = allocation->next;
	}

	if (allocation->next) {
		(allocation->next)->previous = allocation->previous;
	}

	/* Mark the segment as deleted */

	ACPI_MEMSET (&allocation->user_space, 0xEA, allocation->size);

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "Freeing size %X\n", allocation->size));

	status = acpi_ut_release_mutex (ACPI_MTX_MEMORY);
	return_ACPI_STATUS (status);
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

	ACPI_FUNCTION_TRACE ("Ut_dump_allocation_info");

/*
	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Current allocations",
			  Mem_list->Current_count,
			  ROUND_UP_TO_1K (Mem_list->Current_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Max concurrent allocations",
			  Mem_list->Max_concurrent_count,
			  ROUND_UP_TO_1K (Mem_list->Max_concurrent_size)));


	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Total (all) internal objects",
			  Running_object_count,
			  ROUND_UP_TO_1K (Running_object_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Total (all) allocations",
			  Running_alloc_count,
			  ROUND_UP_TO_1K (Running_alloc_size)));


	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Current Nodes",
			  Acpi_gbl_Current_node_count,
			  ROUND_UP_TO_1K (Acpi_gbl_Current_node_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
			  ("%30s: %4d (%3d Kb)\n", "Max Nodes",
			  Acpi_gbl_Max_concurrent_node_count,
			  ROUND_UP_TO_1K ((Acpi_gbl_Max_concurrent_node_count * sizeof (acpi_namespace_node)))));
*/
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_dump_allocations
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
acpi_ut_dump_allocations (
	u32                     component,
	NATIVE_CHAR             *module)
{
	acpi_debug_mem_block    *element;
	ACPI_DESCRIPTOR         *descriptor;
	u32                     num_outstanding = 0;


	ACPI_FUNCTION_TRACE ("Ut_dump_allocations");


	/*
	 * Walk the allocation list.
	 */
	if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_MEMORY))) {
		return;
	}

	element = acpi_gbl_memory_lists[0].list_head;
	while (element) {
		if ((element->component & component) &&
			((module == NULL) || (0 == ACPI_STRCMP (module, element->module)))) {
			/* Ignore allocated objects that are in a cache */

			descriptor = ACPI_CAST_PTR (ACPI_DESCRIPTOR, &element->user_space);
			if (descriptor->descriptor_id != ACPI_DESC_TYPE_CACHED) {
				acpi_os_printf ("%p Len %04X %9.9s-%d ",
						 descriptor, element->size, element->module,
						 element->line);

				/* Most of the elements will be internal objects. */

				switch (ACPI_GET_DESCRIPTOR_TYPE (descriptor)) {
				case ACPI_DESC_TYPE_OPERAND:
					acpi_os_printf ("Obj_type %12.12s R%hd",
							acpi_ut_get_type_name (descriptor->object.common.type),
							descriptor->object.common.reference_count);
					break;

				case ACPI_DESC_TYPE_PARSER:
					acpi_os_printf ("Parse_obj Aml_opcode %04hX",
							descriptor->op.asl.aml_opcode);
					break;

				case ACPI_DESC_TYPE_NAMED:
					acpi_os_printf ("Node %4.4s",
							descriptor->node.name.ascii);
					break;

				case ACPI_DESC_TYPE_STATE:
					acpi_os_printf ("Untyped State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_UPDATE:
					acpi_os_printf ("UPDATE State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_PACKAGE:
					acpi_os_printf ("PACKAGE State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_CONTROL:
					acpi_os_printf ("CONTROL State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_RPSCOPE:
					acpi_os_printf ("ROOT-PARSE-SCOPE State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_PSCOPE:
					acpi_os_printf ("PARSE-SCOPE State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_WSCOPE:
					acpi_os_printf ("WALK-SCOPE State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_RESULT:
					acpi_os_printf ("RESULT State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_NOTIFY:
					acpi_os_printf ("NOTIFY State_obj");
					break;

				case ACPI_DESC_TYPE_STATE_THREAD:
					acpi_os_printf ("THREAD State_obj");
					break;

				default:
					/* All types should appear above */
					break;
				}

				acpi_os_printf ( "\n");
				num_outstanding++;
			}
		}
		element = element->next;
	}

	(void) acpi_ut_release_mutex (ACPI_MTX_MEMORY);

	/* Print summary */

	if (!num_outstanding) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"No outstanding allocations.\n"));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"%d(%X) Outstanding allocations\n",
			num_outstanding, num_outstanding));
	}

	return_VOID;
}


#endif  /* #ifdef ACPI_DBG_TRACK_ALLOCATIONS */

