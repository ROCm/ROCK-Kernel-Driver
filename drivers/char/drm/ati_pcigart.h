/* ati_pcigart.h -- ATI PCI GART support -*- linux-c -*-
 * Created: Wed Dec 13 21:52:19 2000 by gareth@valinux.com
 *
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Gareth Hughes <gareth@valinux.com>
 */

#define __NO_VERSION__
#include "drmP.h"

#if PAGE_SIZE == 8192
# define ATI_PCIGART_TABLE_ORDER 	2
# define ATI_PCIGART_TABLE_PAGES 	(1 << 2)
#elif PAGE_SIZE == 4096
# define ATI_PCIGART_TABLE_ORDER 	3
# define ATI_PCIGART_TABLE_PAGES 	(1 << 3)
#elif
# error - PAGE_SIZE not 8K or 4K
#endif

# define ATI_MAX_PCIGART_PAGES		8192	/* 32 MB aperture, 4K pages */
# define ATI_PCIGART_PAGE_SIZE		4096	/* PCI GART page size */

static unsigned long DRM(ati_alloc_pcigart_table)( void )
{
	unsigned long address;
	struct page *page;
	int i;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	address = __get_free_pages( GFP_KERNEL, ATI_PCIGART_TABLE_ORDER );
	if ( address == 0UL ) {
		return 0;
	}

	page = virt_to_page( address );

	for ( i = 0 ; i <= ATI_PCIGART_TABLE_PAGES ; i++, page++ ) {
		atomic_inc( &page->count );
		SetPageReserved( page );
	}

	DRM_DEBUG( "%s: returning 0x%08lx\n", __FUNCTION__, address );
	return address;
}

static void DRM(ati_free_pcigart_table)( unsigned long address )
{
	struct page *page;
	int i;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !address ) return;

	page = virt_to_page( address );

	for ( i = 0 ; i <= ATI_PCIGART_TABLE_PAGES ; i++, page++ ) {
		atomic_dec( &page->count );
		ClearPageReserved( page );
	}

	free_pages( address, ATI_PCIGART_TABLE_ORDER );
}

unsigned long DRM(ati_pcigart_init)( drm_device_t *dev )
{
	drm_sg_mem_t *entry = dev->sg;
	unsigned long address;
	unsigned long pages;
	u32 *pci_gart, page_base;
	int i, j;

	if ( !entry ) {
		DRM_ERROR( "no scatter/gather memory!\n" );
		return 0;
	}

	address = DRM(ati_alloc_pcigart_table)();
	if ( !address ) {
		DRM_ERROR( "cannot allocate PCI GART page!\n" );
		return 0;
	}

	pci_gart = (u32 *)address;

	pages = ( entry->pages <= ATI_MAX_PCIGART_PAGES )
		? entry->pages : ATI_MAX_PCIGART_PAGES;

	memset( pci_gart, 0, ATI_MAX_PCIGART_PAGES * sizeof(u32) );

	for ( i = 0 ; i < pages ; i++ ) {
		page_base = virt_to_bus( entry->pagelist[i]->virtual );
		for (j = 0; j < (PAGE_SIZE / ATI_PCIGART_PAGE_SIZE); j++) {
			*pci_gart++ = cpu_to_le32( page_base );
			page_base += ATI_PCIGART_PAGE_SIZE;
		}
	}

#if __i386__
	asm volatile ( "wbinvd" ::: "memory" );
#else
	mb();
#endif

	return address;
}

int DRM(ati_pcigart_cleanup)( unsigned long address )
{

	if ( address ) {
		DRM(ati_free_pcigart_table)( address );
	}

	return 0;
}
