/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/pid.h>
#include <linux/err.h>
#include "amd_rdma.h"
#include "kfd_priv.h"


/**
 * This function makes the pages underlying a range of GPU virtual memory
 * accessible for DMA operations from another PCIe device
 *
 * \param   address       - The start address in the Unified Virtual Address
 *			    space in the specified process
 * \param   length        - The length of requested mapping
 * \param   pid           - Pointer to structure pid to which address belongs.
 *			    Could be NULL for current process address space.
 * \param   dma_device    - Device structure of another device
 * \param   page_table    - On return: Pointer to structure describing
 *			    underlying pages/locations
 * \param   free_callback - Pointer to callback which will be called when access
 *			    to such memory must be stopped immediately: Memory
 *			    was freed, GECC events, etc.
 *			    Client should  immediately stop any transfer
 *			    operations and returned as soon as possible.
 *			    After return all resources associated with address
 *			    will be release and no access will be allowed.
 * \param   client_priv   - Pointer to be passed as parameter on
 *			    'free_callback;
 *
 * \return  0 if operation was successful
 */
static int get_pages(uint64_t address, uint64_t length, struct pid *pid,
		struct device *dma_device,
		struct amd_p2p_page_table **page_table,
		void  (*free_callback)(struct amd_p2p_page_table *page_table,
					void *client_priv),
		void  *client_priv)
{
	return -ENOTSUPP;
}
/**
 *
 * This function release resources previously allocated by get_pages() call.
 *
 * \param   page_table - A pointer to page table entries allocated by
 *			 get_pages() call.
 *
 * \return  0 if operation was successful
 */
static int put_pages(struct amd_p2p_page_table *page_table)
{
	return -ENOTSUPP;
}

/**
 * Check if given address belongs to GPU address space.
 *
 * \param   address - Address to check
 * \param   pid     - Process to which given address belongs.
 *		      Could be NULL if current one.
 *
 * \return  0  - This is not GPU address managed by AMD driver
 *	    1  - This is GPU address managed by AMD driver
 */
static int is_gpu_address(uint64_t address, struct pid *pid)
{
	return -ENOTSUPP;
}

/**
 * Return the single page size to be used when building scatter/gather table
 * for given range.
 *
 * \param   address   - Address
 * \param   length    - Range length
 * \param   pid       - Process id structure. Could be NULL if current one.
 * \param   page_size - On return: Page size
 *
 * \return  0 if operation was successful
 */
static int get_page_size(uint64_t address, uint64_t length, struct pid *pid,
			unsigned long *page_size)
{
	return -ENOTSUPP;
}


/**
 * Singleton object: rdma interface function pointers
 */
static const struct amd_rdma_interface  rdma_ops = {
	.get_pages = get_pages,
	.put_pages = put_pages,
	.is_gpu_address = is_gpu_address,
	.get_page_size = get_page_size,
};

/**
 * amdkfd_query_rdma_interface - Return interface (function pointers table) for
 *				 rdma interface
 *
 *
 * \param interace     - OUT: Pointer to interface
 *
 * \return 0 if operation was successful.
 */
int amdkfd_query_rdma_interface(const struct amd_rdma_interface **ops)
{
	*ops  = &rdma_ops;

	return 0;
}
EXPORT_SYMBOL(amdkfd_query_rdma_interface);



