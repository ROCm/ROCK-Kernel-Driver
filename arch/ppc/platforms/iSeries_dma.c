/*
 * iSeries_dma.c
 * Copyright (C) 2001 Mike Corrigan  IBM Corporation
 *
 * Dynamic DMA mapping support.
 * 
 * Manages the TCE space assigned to this partition
 * 
 * modeled from pci-dma.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/iSeries/iSeries_pci.h>

struct pci_dev  * iSeries_veth_dev = NULL;
struct pci_dev  * iSeries_vio_dev  = NULL;

struct TceTable   virtBusTceTable;	/* Tce table for virtual bus */

struct TceTable * tceTables[256];	// Tce tables for 256 busses
					// Bus 255 is the virtual bus
					// zero indicates no bus defined
		  // allocates a contiguous range of tces (power-of-2 size)
static long       alloc_tce_range( struct TceTable *, 
				   unsigned order );
		  // allocates a contiguous range of tces (power-of-2 size)
		  // assumes lock already held
static long       alloc_tce_range_nolock( struct TceTable *, 
					  unsigned order );
		  // frees a contiguous range of tces (power-of-2 size)
static void       free_tce_range( struct TceTable *, 
				  long tcenum, 
				  unsigned order );
		  // frees a contiguous rnage of tces (power-of-2 size)
		  // assumes lock already held
static void       free_tce_range_nolock( struct TceTable *, 
				  	 long tcenum, 
					 unsigned order );
		  // allocates a range of tces and sets them to the 
		  // pages 
static 		  dma_addr_t get_tces( struct TceTable *, 
				       unsigned order, 
				       void *page, 
			    	       unsigned numPages,
			    	       int tceType,
			    	       int direction );
static void       free_tces( struct TceTable *, 
			     dma_addr_t tce, 
			     unsigned order,
			     unsigned numPages );
static long       test_tce_range( struct TceTable *, 
				  long tcenum, 
				  unsigned order );

static unsigned   fill_scatterlist_sg( struct scatterlist *sg, int nents, 
		        	       dma_addr_t dma_addr, unsigned long numTces );

static unsigned long num_tces_sg( struct scatterlist *sg, 
				  int nents );
	
static dma_addr_t create_tces_sg( struct TceTable *tbl, 
				  struct scatterlist *sg, 
			 	  int nents, 
				  unsigned numTces,
				  int tceType, 
				  int direction );

static unsigned __inline__ count_leading_zeros32( unsigned long x )
{
	unsigned lz;
	asm("cntlzw %0,%1" : "=r"(lz) : "r"(x));
	return lz;
}

static void __inline__ build_tce( struct TceTable * tbl, long tcenum, 
			     unsigned long uaddr, int tceType, int direction )
{
	union Tce tce;
	
	tce.wholeTce = 0;
	tce.tceBits.rpn = (virt_to_absolute(uaddr)) >> PAGE_SHIFT;
	// If for virtual bus
	if ( tceType == TCE_VB ) {
		tce.tceBits.valid = 1;
		tce.tceBits.allIo = 1;
		if ( direction != PCI_DMA_TODEVICE )
			tce.tceBits.readWrite = 1;
	}
	// If for PCI bus
	else {
		tce.tceBits.readWrite = 1; // Read allowed 
		if ( direction != PCI_DMA_TODEVICE )
			tce.tceBits.pciWrite = 1;
	}
	HvCallXm_setTce( (u64)tbl->index, (u64)tcenum, tce.wholeTce );

}



// Build a TceTable structure.  This contains a multi-level bit map which
// is used to manage allocation of the tce space.

struct TceTable * build_tce_table( struct HvTceTableManagerCB * tceTableParms,
				   struct TceTable * tbl )
{
	unsigned long bits, bytes, totalBytes;
	unsigned long numBits[NUM_TCE_LEVELS], numBytes[NUM_TCE_LEVELS];
	unsigned i, k, m;
	unsigned char * pos, * p, b;

	tbl->size = tceTableParms->size;
	tbl->busNumber = tceTableParms->busNumber;
	tbl->startOffset = tceTableParms->startOffset;
	tbl->index = tceTableParms->index;
	spin_lock_init( &(tbl->lock) );
	
	tbl->mlbm.maxLevel = 0;

	// Compute number of bits and bytes for each level of the
	// multi-level bit map
	// 
	totalBytes = 0;
	bits = tbl->size * (PAGE_SIZE / sizeof( union Tce ));
	
	for ( i=0; i<NUM_TCE_LEVELS; ++i ) {
		bytes = (bits+7)/8;
#ifdef DEBUG_TCE
		printk("build_tce_table: level %d bits=%ld, bytes=%ld\n", i, bits, bytes );
#endif
		numBits[i] = bits;
		numBytes[i] = bytes;
		bits /= 2;
		/* we need extra space at the end that's a multiple of 8 bytes */
		/* for the cntlzw algorithm to work correctly. */
		/* but we don't want the bits turned on or used, so we don't muck with numBytes[i] */
		totalBytes += (bytes + 7) & ~7;
	}
#ifdef DEBUG_TCE
	printk("build_tce_table: totalBytes=%ld\n", totalBytes );
#endif
	
	pos = (char *)__get_free_pages( GFP_ATOMIC, get_order( totalBytes )); 
	if ( !pos )
		return NULL;

	memset( pos, 0, totalBytes );
	
	// For each level, fill in the pointer to the bit map,
	// and turn on the last bit in the bit map (if the
	// number of bits in the map is odd).  The highest
	// level will get all of its bits turned on.
	
	for (i=0; i<NUM_TCE_LEVELS; ++i) {
		if ( numBytes[i] ) {
			tbl->mlbm.level[i].map = pos;
			tbl->mlbm.maxLevel = i;

			if ( numBits[i] & 1 ) {
				p = pos + numBytes[i] - 1;
				m = (( numBits[i] % 8) - 1) & 7;
				*p = 0x80 >> m;
#ifdef DEBUG_TCE
				printk("build_tce_table: level %d last bit %x\n", i, 0x80>>m );
#endif				
			}
		}
		else
			tbl->mlbm.level[i].map = 0;
		/* see the comment up above on the totalBytes calculation for why we do this. */
		pos += (numBytes[i] + 7) & ~7;
		tbl->mlbm.level[i].numBits = numBits[i];
		tbl->mlbm.level[i].numBytes = numBytes[i];
		
	}

	// For the highest level, turn on all the bits
	
	i = tbl->mlbm.maxLevel;
	p = tbl->mlbm.level[i].map;
	m = numBits[i];
#ifdef DEBUG_TCE
	printk("build_tce_table: highest level (%d) has all bits set\n", i);
#endif	
	for (k=0; k<numBytes[i]; ++k) {
		if ( m >= 8 ) {
			// handle full bytes
			*p++ = 0xff;
			m -= 8;
		}
		else {
			// handle the last partial byte
			b = 0x80;
			*p = 0;
			while (m) {
				*p |= b;
				b >>= 1;
				--m;
			}
		}
	}

	return tbl;
	
}

static long alloc_tce_range( struct TceTable *tbl, unsigned order )
{
	long retval;
	unsigned long flags;
	
	// Lock the tce allocation bitmap
	spin_lock_irqsave( &(tbl->lock), flags );

	// Do the actual work
	retval = alloc_tce_range_nolock( tbl, order );
	
	// Unlock the tce allocation bitmap
	spin_unlock_irqrestore( &(tbl->lock), flags );

	return retval;
}

static long alloc_tce_range_nolock( struct TceTable *tbl, unsigned order )
{
	unsigned long numBits, numBytes;
	unsigned long i, bit, block, mask;
	long tcenum;
	u32 * map;

	// If the order (power of 2 size) requested is larger than our
	// biggest, indicate failure
	if ( order > tbl->mlbm.maxLevel ) {
#ifdef DEBUG_TCE
		printk("alloc_tce_range_nolock: invalid order requested, order = %d\n", order );
#endif
		return -1;
	}
	
	numBits =  tbl->mlbm.level[order].numBits;
	numBytes = tbl->mlbm.level[order].numBytes;
	map =      (u32 *)(tbl->mlbm.level[order].map);

	// Initialize return value to -1 (failure)
	tcenum = -1;

	// Loop through the bytes of the bitmap
	for (i=0; i<numBytes/4; ++i) {
		if ( *map ) {
			// A free block is found, compute the block
			// number (of this size)
			bit = count_leading_zeros32( *map );
			block = (i * 32) + bit;
			// turn off the bit in the map to indicate
			// that the block is now in use
			mask = 0xffffffff ^ (0x80000000 >> bit);
			*map &= mask;
			// compute the index into our tce table for
			// the first tce in the block
#ifdef DEBUG_TCE
			printk("alloc_tce_range_nolock: allocating block %ld, (byte=%ld, bit=%ld) order %d\n", block, i, bit, order );
#endif
			tcenum = block << order;
			break;
		}
		++map;
	}

#ifdef DEBUG_TCE
	if ( tcenum == -1 ) {
		printk("alloc_tce_range_nolock: no available blocks of order = %d\n", order );
		if ( order < tbl->mlbm.maxLevel )
			printk("alloc_tce_range_nolock: trying next bigger size\n" );
		else
			printk("alloc_tce_range_nolock: maximum size reached...failing\n");
	}
#endif	
	
	// If no block of the requested size was found, try the next
	// size bigger.  If one of those is found, return the second
	// half of the block to freespace and keep the first half
	if ( ( tcenum == -1 ) && ( order < tbl->mlbm.maxLevel  ) ) {
		tcenum = alloc_tce_range_nolock( tbl, order+1 );
		if ( tcenum != -1 ) {
			free_tce_range_nolock( tbl, tcenum+(1<<order), order );
		}
	}
	
	// Return the index of the first tce in the block
	// (or -1 if we failed)
	return tcenum;
	
}

static void	free_tce_range( struct TceTable *tbl, long tcenum, unsigned order )
{
	unsigned long flags;

	// Lock the tce allocation bitmap
	spin_lock_irqsave( &(tbl->lock), flags );

	// Do the actual work
	free_tce_range_nolock( tbl, tcenum, order );
	
	// Unlock the tce allocation bitmap
	spin_unlock_irqrestore( &(tbl->lock), flags );

}

static void	free_tce_range_nolock( struct TceTable *tbl, long tcenum, unsigned order )
{
	unsigned long block;
	unsigned byte, bit, mask, b;
	unsigned char  * map, * bytep;
	
	if ( order > tbl->mlbm.maxLevel ) {
		printk("free_tce_range: order too large, order = %d\n", order );
		return;
	}
	
	block = tcenum >> order;
	if ( tcenum != (block << order ) ) {
		printk("free_tce_range: tcenum %lx is not on appropriate boundary for order %x\n", tcenum, order );
		return;
	}
	if ( block >= tbl->mlbm.level[order].numBits ) {
		printk("free_tce_range: tcenum %lx is outside the range of this map (order %x, numBits %lx\n", tcenum, order, tbl->mlbm.level[order].numBits );
		return;
	}
#ifdef DEBUG_TCE	
	if ( test_tce_range( tbl, tcenum, order ) ) {
		printk("free_tce_range: freeing range not completely allocated.\n");
		printk("free_tce_range:   TceTable %p, tcenum %lx, order %x\n", tbl, tcenum, order );
	}
#endif
	map = tbl->mlbm.level[order].map;
	byte  = block / 8;
	bit   = block % 8;
	mask  = 0x80 >> bit;
	bytep = map + byte;
#ifdef DEBUG_TCE
	printk("free_tce_range_nolock: freeing block %ld (byte=%d, bit=%d) of order %d\n",block, byte, bit, order);
	if ( *bytep & mask )
		printk("free_tce_range: already free: TceTable %p, tcenum %lx, order %x\n", tbl, tcenum, order );
#endif	
	*bytep |= mask;

	// If there is a higher level in the bit map than this we may be
	// able to buddy up this block with its partner.
	//   If this is the highest level we can't buddy up
	//   If this level has an odd number of bits and
	//      we are freeing the last block we can't buddy up
	// don't buddy up if it's in the first 1/4 of the bits
	  if ( ( order < tbl->mlbm.maxLevel ) &&
	       ( ( 0 == ( tbl->mlbm.level[order].numBits & 1 ) ) ||
		 ( block < tbl->mlbm.level[order].numBits-1 ) )  &&
	       ( block > (tbl->mlbm.level[order].numBits/4) ) ) {
	
		// See if we can buddy up the block we just freed
		bit  &= 6;		// get to the first of the buddy bits
		mask  = 0xc0 >> bit;	// build two bit mask
		b     = *bytep & mask;	// Get the two bits
		if ( 0 == (b ^ mask) ) { // If both bits are on
			// both of the buddy blocks are free we can combine them
			*bytep ^= mask;	// turn off the two bits
			block = ( byte * 8 ) + bit; // block of first of buddies
			tcenum = block << order;
			// free the buddied block
#ifdef DEBUG_TCE
			printk("free_tce_range: buddying up block %ld and block %ld\n", block, block+1);
#endif			
			free_tce_range_nolock( tbl, tcenum, order+1 ); 
		}	
	}
}

static long	test_tce_range( struct TceTable *tbl, long tcenum, unsigned order )
{
	unsigned long block;
	unsigned byte, bit, mask, b;
	long	retval, retLeft, retRight;
	unsigned char  * map;
	
	map = tbl->mlbm.level[order].map;
	block = tcenum >> order;
	byte = block / 8;		// Byte within bitmap
	bit  = block % 8;		// Bit within byte
	mask = 0x80 >> bit;		
	b    = (*(map+byte) & mask );	// 0 if block is allocated, else free
	if ( b ) 
		retval = 1;		// 1 == block is free
	else
		retval = 0;		// 0 == block is allocated
	// Test bits at all levels below this to ensure that all agree

	if (order) {
		retLeft  = test_tce_range( tbl, tcenum, order-1 );
		retRight = test_tce_range( tbl, tcenum+(1<<(order-1)), order-1 );
		if ( retLeft || retRight ) {
			retval = 2;		
		}
	}

	// Test bits at all levels above this to ensure that all agree
	
	return retval;
}

static dma_addr_t get_tces( struct TceTable *tbl, unsigned order, void *page, unsigned numPages, int tceType, int direction )
{
	long tcenum;
	unsigned long uaddr;
	unsigned i;
	dma_addr_t retTce = NO_TCE;

	uaddr = (unsigned long)page & PAGE_MASK;
	
	// Allocate a range of tces
	tcenum = alloc_tce_range( tbl, order );
	if ( tcenum != -1 ) {
		// We got the tces we wanted
		tcenum += tbl->startOffset;	// Offset into real TCE table
		retTce = tcenum << PAGE_SHIFT;	// Set the return dma address
		// Setup a tce for each page
		for (i=0; i<numPages; ++i) {
			build_tce( tbl, tcenum, uaddr, tceType, direction );
			++tcenum;
			uaddr += PAGE_SIZE;
		}
	}
#ifdef DEBUG_TCE
	else
		printk("alloc_tce_range failed\n");
#endif	
	return retTce; 
}

static void free_tces( struct TceTable *tbl, dma_addr_t dma_addr, unsigned order, unsigned numPages )
{
	long tcenum, freeTce, maxTcenum;
	unsigned i;
	union Tce tce;

	maxTcenum = (tbl->size * (PAGE_SIZE / sizeof(union Tce))) - 1;
	
	tcenum = dma_addr >> PAGE_SHIFT;
	tcenum -= tbl->startOffset;

	if ( tcenum > maxTcenum ) {
		printk("free_tces: tcenum > maxTcenum, tcenum = %ld, maxTcenum = %ld\n",
				tcenum, maxTcenum );
		printk("free_tces:    TCE Table at %16lx\n", (unsigned long)tbl );
		printk("free_tces:      bus#     %lu\n", (unsigned long)tbl->busNumber );
		printk("free_tces:      size     %lu\n", (unsigned long)tbl->size );
		printk("free_tces:      startOff %lu\n", (unsigned long)tbl->startOffset );
		printk("free_tces:      index    %lu\n", (unsigned long)tbl->index );
		return;
	}
	
	freeTce = tcenum;

	for (i=0; i<numPages; ++i) {
		tce.wholeTce = 0;
		HvCallXm_setTce( (u64)tbl->index, (u64)tcenum, tce.wholeTce );
		++tcenum;
	}

	free_tce_range( tbl, freeTce, order );

}

void __init create_virtual_bus_tce_table(void)
{
	struct TceTable * t;
	struct HvTceTableManagerCB virtBusTceTableParms;
	u64 absParmsPtr;

	virtBusTceTableParms.busNumber = 255;	/* Bus 255 is the virtual bus */
	virtBusTceTableParms.virtualBusFlag = 0xff; /* Ask for virtual bus */
	
	absParmsPtr = virt_to_absolute( (u32)&virtBusTceTableParms );
	HvCallXm_getTceTableParms( absParmsPtr );
	
	t = build_tce_table( &virtBusTceTableParms, &virtBusTceTable );
	if ( t ) {
		tceTables[255] = t;
		printk("Virtual Bus TCE table built successfully.\n");
		printk("  TCE table size = %ld entries\n", 
				(unsigned long)t->size*(PAGE_SIZE/sizeof(union Tce)) );
		printk("  TCE table token = %d\n",
				(unsigned)t->index );
		printk("  TCE table start entry = 0x%lx\n",
				(unsigned long)t->startOffset );
	}
	else 
		printk("Virtual Bus TCE table failed.\n");
}

void __init create_pci_bus_tce_table( unsigned busNumber )
{
	struct TceTable * t;
	struct TceTable * newTceTable;
	struct HvTceTableManagerCB pciBusTceTableParms;
	u64 absParmsPtr;
	unsigned i;

	if ( busNumber > 254 ) {
		printk("PCI Bus TCE table failed.\n");
		printk("  Invalid bus number %u\n", busNumber );
		return;
	}

	newTceTable = kmalloc( sizeof(struct TceTable), GFP_KERNEL );
	
	pciBusTceTableParms.busNumber = busNumber;
	pciBusTceTableParms.virtualBusFlag = 0; 
	
	absParmsPtr = virt_to_absolute( (u32)&pciBusTceTableParms );
	HvCallXm_getTceTableParms( absParmsPtr );

	// Determine if the table identified by the index and startOffset
	// returned by the hypervisor for this bus has already been created.

	for ( i=0; i<255; ++i) {
		t = tceTables[i];
		if ( t ) {
			if ( ( t->index == pciBusTceTableParms.index ) && 
		     	    ( t->startOffset == pciBusTceTableParms.startOffset ) ) {
				if ( t->size != pciBusTceTableParms.size ) 
					printk("PCI Bus %d Shares a TCE table with Bus %d, but sizes differ\n", busNumber, i );
				else
					printk("PCI Bus %d Shares a TCE table with bus %d\n", busNumber, i );
				tceTables[busNumber] = t;
				break;
			}
		}
	}

	if ( ! tceTables[busNumber] ) {
	
		t = build_tce_table( &pciBusTceTableParms, newTceTable );
		if ( t ) {
			tceTables[busNumber] = t;
			printk("PCI Bus %d TCE table built successfully.\n", busNumber);
			printk("  TCE table size = %ld entries\n", 
					(unsigned long)t->size*(PAGE_SIZE/sizeof(union Tce)) );
			printk("  TCE table token = %d\n",
					(unsigned)t->index );
			printk("  TCE table start entry = 0x%lx\n",
					(unsigned long)t->startOffset );
		}
		else {
			kfree( newTceTable );
			printk("PCI Bus %d TCE table failed.\n", busNumber );
		}
	}
}


// Allocates a contiguous real buffer and creates TCEs over it.
// Returns the virtual address of the buffer and sets dma_handle
// to the dma address (tce) of the first page.
void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	struct TceTable * tbl;
	void *ret = NULL;
	unsigned order, nPages, bus;
	dma_addr_t tce;
	int tceType;
	
	size = PAGE_ALIGN(size);
	order = get_order(size);
	nPages = 1 << order;

	// If no pci_dev then use virtual bus
	if (hwdev == NULL ) {
		bus = 255;
		tceType = TCE_VB;
	}
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
		tceType = TCE_PCI;
#else
                BUG();
                return NULL;
#endif /* CONFIG_PCI */
	}
	
	tbl = tceTables[bus];
	if ( tbl ) {
		// Alloc enough pages (and possibly more)
		ret = (void *)__get_free_pages( GFP_ATOMIC, order );
		if ( ret ) {
			// Page allocation succeeded
			memset(ret, 0, nPages << PAGE_SHIFT);
			// Set up tces to cover the allocated range
			tce = get_tces( tbl, order, ret, nPages, tceType,
					PCI_DMA_BIDIRECTIONAL );
			if ( tce == NO_TCE ) {
#ifdef DEBUG_TCE				
				printk("pci_alloc_consistent: get_tces failed\n" );
#endif
				free_pages( (unsigned long)ret, order );
				ret = NULL;
			}
			else
			{
				*dma_handle = tce;
			}
		}
#ifdef DEBUG_TCE
		else
			printk("pci_alloc_consistent: __get_free_pages failed for order = %d\n", order);
#endif		
	}

	return ret;
}

void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	struct TceTable * tbl;
	unsigned order, nPages, bus;
	
	size = PAGE_ALIGN(size);
	order = get_order(size);
	nPages = 1 << order;
	
	// If no pci_dev then use virtual bus
	if (hwdev == NULL )
		bus = 255;
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
#else
                BUG();
                return;
#endif /* CONFIG_PCI */
	}

	if ( bus > 255 ) {
		printk("pci_free_consistent: invalid bus # %d\n", bus );
		printk("pci_free_consistent:   hwdev = %08lx\n", (unsigned long)hwdev );
	}
	
	tbl = tceTables[bus];

	if ( tbl ) {
		free_tces( tbl, dma_handle, order, nPages );
		free_pages( (unsigned long)vaddr, order );
	}
}

// Creates TCEs for a user provided buffer.  The user buffer must be 
// contiguous real kernel storage (not vmalloc).  The address of the buffer
// passed here is the kernel (virtual) address of the buffer.  The buffer
// need not be page aligned, the dma_addr_t returned will point to the same
// byte within the page as vaddr.
dma_addr_t pci_map_single( struct pci_dev *hwdev, void *vaddr, size_t size, int direction )
{
	struct TceTable * tbl;
	dma_addr_t dma_handle;
	unsigned long uaddr;
	unsigned order, nPages, bus;
	int tceType;

	if ( direction == PCI_DMA_NONE )
		BUG();
	
	dma_handle = NO_TCE;
	
	uaddr = (unsigned long)vaddr;
	nPages = PAGE_ALIGN( uaddr + size ) - ( uaddr & PAGE_MASK );
	order = get_order( nPages & PAGE_MASK );
	nPages >>= PAGE_SHIFT;
	
	// If no pci_dev then use virtual bus
	if (hwdev == NULL ) {
		bus = 255;
		tceType = TCE_VB;
	}
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
		tceType = TCE_PCI;
#else
                BUG();
                return NO_TCE;
#endif /* CONFIG_PCI */

	}
	
	tbl = tceTables[bus];

	if ( tbl ) {
		dma_handle = get_tces( tbl, order, vaddr, nPages, tceType, 
					direction );
		dma_handle |= ( uaddr & ~PAGE_MASK );
	}

	return dma_handle;
}

void pci_unmap_single( struct pci_dev *hwdev, dma_addr_t dma_handle, size_t size, int direction )
{
	struct TceTable * tbl;
	unsigned order, nPages, bus;
	
	if ( direction == PCI_DMA_NONE )
		BUG();

	nPages = PAGE_ALIGN( dma_handle + size ) - ( dma_handle & PAGE_MASK );
	order = get_order( nPages & PAGE_MASK );
	nPages >>= PAGE_SHIFT;
	
	// If no pci_dev then use virtual bus
	if (hwdev == NULL )
		bus = 255;
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
#else
                BUG();
                return;
#endif /* CONFIG_PCI */
	}
	
	if ( bus > 255 ) {
		printk("pci_unmap_single: invalid bus # %d\n", bus );
		printk("pci_unmap_single:   hwdev = %08lx\n", (unsigned long)hwdev );
	}

	tbl = tceTables[bus];

	if ( tbl ) 
		free_tces( tbl, dma_handle, order, nPages );

}

// Figure out how many TCEs are actually going to be required
// to map this scatterlist.  This code is not optimal.  It 
// takes into account the case where entry n ends in the same
// page in which entry n+1 starts.  It does not handle the 
// general case of entry n ending in the same page in which 
// entry m starts.   

static unsigned long num_tces_sg( struct scatterlist *sg, int nents )
{
	unsigned long nTces, numPages, startPage, endPage, prevEndPage;
	unsigned i;

	prevEndPage = 0;
	nTces = 0;

	for (i=0; i<nents; ++i) {
		// Compute the starting page number and
		// the ending page number for this entry
		startPage = (unsigned long)sg->address >> PAGE_SHIFT;
		endPage = ((unsigned long)sg->address + sg->length - 1) >> PAGE_SHIFT;
		numPages = endPage - startPage + 1;
		// Simple optimization: if the previous entry ended
		// in the same page in which this entry starts
		// then we can reduce the required pages by one.
		// This matches assumptions in fill_scatterlist_sg and
		// create_tces_sg
		if ( startPage == prevEndPage )
			--numPages;
		nTces += numPages;
		prevEndPage = endPage;
		sg++;
	}
	return nTces;
}

// Fill in the dma data in the scatterlist
// return the number of dma sg entries created
static unsigned fill_scatterlist_sg( struct scatterlist *sg, int nents, 
				 dma_addr_t dma_addr , unsigned long numTces)
{
	struct scatterlist *dma_sg;
	u32 cur_start_dma;
	unsigned long cur_len_dma, cur_end_virt, uaddr;
	unsigned num_dma_ents;

	dma_sg = sg;
	num_dma_ents = 1;

	// Process the first sg entry
	cur_start_dma = dma_addr + ((unsigned long)sg->address & (~PAGE_MASK));
	cur_len_dma = sg->length;
	// cur_end_virt holds the address of the byte immediately after the
	// end of the current buffer.
	cur_end_virt = (unsigned long)sg->address + cur_len_dma;
	// Later code assumes that unused sg->dma_address and sg->dma_length
	// fields will be zero.  Other archs seem to assume that the user
	// (device driver) guarantees that...I don't want to depend on that
	sg->dma_address = sg->dma_length = 0;
	
	// Process the rest of the sg entries
	while (--nents) {
		++sg;
		// Clear possibly unused fields. Note: sg >= dma_sg so
		// this can't be clearing a field we've already set
		sg->dma_address = sg->dma_length = 0;

		// Check if it is possible to make this next entry
		// contiguous (in dma space) with the previous entry.
		
		// The entries can be contiguous in dma space if
		// the previous entry ends immediately before the
		// start of the current entry (in virtual space)
		// or if the previous entry ends at a page boundary
		// and the current entry starts at a page boundary.
		uaddr = (unsigned long)sg->address;
		if ( ( uaddr != cur_end_virt ) &&
		     ( ( ( uaddr | cur_end_virt ) & (~PAGE_MASK) ) ||
		       ( ( uaddr & PAGE_MASK ) == ( ( cur_end_virt-1 ) & PAGE_MASK ) ) ) ) {
			// This entry can not be contiguous in dma space.
			// save the previous dma entry and start a new one
			dma_sg->dma_address = cur_start_dma;
			dma_sg->dma_length  = cur_len_dma;

			++dma_sg;
			++num_dma_ents;
			
			cur_start_dma += cur_len_dma-1;
			// If the previous entry ends and this entry starts
			// in the same page then they share a tce.  In that
			// case don't bump cur_start_dma to the next page 
			// in dma space.  This matches assumptions made in
			// num_tces_sg and create_tces_sg.
			if ((uaddr & PAGE_MASK) == ((cur_end_virt-1) & PAGE_MASK))
				cur_start_dma &= PAGE_MASK;
			else
				cur_start_dma = PAGE_ALIGN(cur_start_dma+1);
			cur_start_dma += ( uaddr & (~PAGE_MASK) );
			cur_len_dma = 0;
		}
		// Accumulate the length of this entry for the next 
		// dma entry
		cur_len_dma += sg->length;
		cur_end_virt = uaddr + sg->length;
	}
	// Fill in the last dma entry
	dma_sg->dma_address = cur_start_dma;
	dma_sg->dma_length  = cur_len_dma;

	if ((((cur_start_dma +cur_len_dma - 1)>> PAGE_SHIFT) - (dma_addr >> PAGE_SHIFT) + 1) != numTces)
	  {
	    printk("fill_scatterlist_sg: numTces %ld, used tces %d\n",
		   numTces,
		   (unsigned)(((cur_start_dma + cur_len_dma - 1) >> PAGE_SHIFT) - (dma_addr >> PAGE_SHIFT) + 1));
	  }
	

	return num_dma_ents;
}

// Call the hypervisor to create the TCE entries.
// return the number of TCEs created
static dma_addr_t create_tces_sg( struct TceTable *tbl, struct scatterlist *sg, 
		   int nents, unsigned numTces, int tceType, int direction )
{
	unsigned order, i, j;
	unsigned long startPage, endPage, prevEndPage, numPages, uaddr;
	long tcenum, starttcenum;
	dma_addr_t dmaAddr;

	dmaAddr = NO_TCE;

	order = get_order( numTces << PAGE_SHIFT );
	// allocate a block of tces
	tcenum = alloc_tce_range( tbl, order );
	if ( tcenum != -1 ) {
		tcenum += tbl->startOffset;
		starttcenum = tcenum;
		dmaAddr = tcenum << PAGE_SHIFT;
		prevEndPage = 0;
		for (j=0; j<nents; ++j) {
			startPage = (unsigned long)sg->address >> PAGE_SHIFT;
			endPage = ((unsigned long)sg->address + sg->length - 1) >> PAGE_SHIFT;
			numPages = endPage - startPage + 1;
			
			uaddr = (unsigned long)sg->address;
			
			// If the previous entry ended in the same page that
			// the current page starts then they share that
			// tce and we reduce the number of tces we need
			// by one.  This matches assumptions made in
			// num_tces_sg and fill_scatterlist_sg
			if ( startPage == prevEndPage ) { 
				--numPages;
				uaddr += PAGE_SIZE;
			}
			
			for (i=0; i<numPages; ++i) {
			  build_tce( tbl, tcenum, uaddr, tceType,
				     direction );
			  ++tcenum;
			  uaddr += PAGE_SIZE;
			}
			
			prevEndPage = endPage;
			sg++;
		}
		if ((tcenum - starttcenum) != numTces)
	    		printk("create_tces_sg: numTces %d, tces used %d\n",
		   		numTces, (unsigned)(tcenum - starttcenum));

	}

	return dmaAddr;
}

int pci_map_sg( struct pci_dev *hwdev, struct scatterlist *sg, int nents, int direction )
{
	struct TceTable * tbl;
	unsigned bus, numTces;
	int tceType, num_dma;
	dma_addr_t dma_handle;

	// Fast path for a single entry scatterlist
	if ( nents == 1 ) {
		sg->dma_address = pci_map_single( hwdev, sg->address, 
					sg->length, direction );
		sg->dma_length = sg->length;
		return 1;
	}
	
	if ( direction == PCI_DMA_NONE )
		BUG();
	
	// If no pci_dev then use virtual bus
	if (hwdev == NULL ) {
		bus = 255;
		tceType = TCE_VB;
	}
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
		tceType = TCE_PCI;
#else
                BUG();
                return 0;
#endif /* CONFIG_PCI */
	}
	
	tbl = tceTables[bus];

	if ( tbl ) {
		// Compute the number of tces required
		numTces = num_tces_sg( sg, nents );
		// Create the tces and get the dma address
		dma_handle = create_tces_sg( tbl, sg, nents, numTces,
				             tceType, direction );

		// Fill in the dma scatterlist
		num_dma = fill_scatterlist_sg( sg, nents, dma_handle, numTces );
	}

	return num_dma;
}

void pci_unmap_sg( struct pci_dev *hwdev, struct scatterlist *sg, int nelms, int direction )
{
	struct TceTable * tbl;
	unsigned order, numTces, bus, i;
	dma_addr_t dma_end_page, dma_start_page;
	
	if ( direction == PCI_DMA_NONE )
		BUG();

	dma_start_page = sg->dma_address & PAGE_MASK;
	for ( i=nelms; i>0; --i ) {
		unsigned k = i - 1;
		if ( sg[k].dma_length ) {
			dma_end_page = ( sg[k].dma_address +
					 sg[k].dma_length - 1 ) & PAGE_MASK;
			break;
		}
	}

	numTces = ((dma_end_page - dma_start_page ) >> PAGE_SHIFT) + 1;
	order = get_order( numTces << PAGE_SHIFT );
	
	// If no pci_dev then use virtual bus
	if (hwdev == NULL )
		bus = 255;
	else {
#ifdef CONFIG_PCI        
		// Get the iSeries bus # to use as an index
		// into the TCE table array
		bus = ISERIES_GET_BUS( hwdev );
#else
                BUG();
                return;
#endif /* CONFIG_PCI */
	}
		
	if ( bus > 255 ) {
		printk("pci_unmap_sg: invalid bus # %d\n", bus );
		printk("pci_unmap_sg:   hwdev = %08lx\n", (unsigned long)hwdev );
	}


	tbl = tceTables[bus];

	if ( tbl ) 
		free_tces( tbl, dma_start_page, order, numTces );

}

