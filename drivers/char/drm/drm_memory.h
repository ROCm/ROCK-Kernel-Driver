/* drm_memory.h -- Memory management wrappers for DRM -*- linux-c -*-
 * Created: Thu Feb  4 14:00:34 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "drmP.h"

/* Cut down version of drm_memory_debug.h, which used to be called
 * drm_memory.h.  If you want the debug functionality, change 0 to 1
 * below.
 */
#define DEBUG_MEMORY 0


#if DEBUG_MEMORY
#include "drm_memory_debug.h"
#else
void DRM(mem_init)(void)
{
}

/* drm_mem_info is called whenever a process reads /dev/drm/mem. */
int DRM(mem_info)(char *buf, char **start, off_t offset,
		  int len, int *eof, void *data)
{
	return 0;
}

void *DRM(alloc)(size_t size, int area)
{
	return kmalloc(size, GFP_KERNEL);
}

void *DRM(realloc)(void *oldpt, size_t oldsize, size_t size, int area)
{
	void *pt;

	if (!(pt = kmalloc(size, GFP_KERNEL))) return NULL;
	if (oldpt && oldsize) {
		memcpy(pt, oldpt, oldsize);
		kfree(oldpt);
	}
	return pt;
}

void DRM(free)(void *pt, size_t size, int area)
{
	kfree(pt);
}

unsigned long DRM(alloc_pages)(int order, int area)
{
	unsigned long address;
	unsigned long bytes	  = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int  sz;

	address = __get_free_pages(GFP_KERNEL, order);
	if (!address) 
		return 0;

				/* Zero */
	memset((void *)address, 0, bytes);

				/* Reserve */
	for (addr = address, sz = bytes;
	     sz > 0;
	     addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		SetPageReserved(virt_to_page(addr));
	}

	return address;
}

void DRM(free_pages)(unsigned long address, int order, int area)
{
	unsigned long bytes = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int  sz;

	if (!address) 
		return;

	/* Unreserve */
	for (addr = address, sz = bytes;
	     sz > 0;
	     addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
	}

	free_pages(address, order);
}

void *DRM(ioremap)(unsigned long offset, unsigned long size)
{
	return ioremap(offset, size);
}

void *DRM(ioremap_nocache)(unsigned long offset, unsigned long size)
{
	return ioremap_nocache(offset, size);
}

void DRM(ioremapfree)(void *pt, unsigned long size)
{
	iounmap(pt);
}

#if __REALLY_HAVE_AGP
struct agp_memory *DRM(alloc_agp)(int pages, u32 type)
{
	return DRM(agp_allocate_memory)(pages, type);
}

int DRM(free_agp)(struct agp_memory *handle, int pages)
{
	return DRM(agp_free_memory)(handle) ? 0 : -EINVAL;
}

int DRM(bind_agp)(struct agp_memory *handle, unsigned int start)
{
	return DRM(agp_bind_memory)(handle, start);
}

int DRM(unbind_agp)(struct agp_memory *handle)
{
	return DRM(agp_unbind_memory)(handle);
}
#endif /* agp */
#endif /* debug_memory */
