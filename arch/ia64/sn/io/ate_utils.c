/* $Id: ate_utils.c,v 1.1 2002/02/28 17:31:25 marcelo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/io.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/prio.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/sn_private.h>

#include <asm/sn/ate_utils.h>

/*
 * Allocate the map needed to allocate the ATE entries.
 */
struct map *
atemapalloc(ulong_t mapsiz)
{
	struct map *mp;
	ulong_t size;
	struct a {
		spinlock_t lock;
		sv_t 	sema;
	} *sync;

	if (mapsiz == 0)
		return(NULL);
	size = sizeof(struct map) * (mapsiz + 2);
	if ((mp = (struct map *) kmalloc(size, GFP_KERNEL)) == NULL)
		return(NULL);
	memset(mp, 0x0, size);

	sync = kmalloc(sizeof(struct a), GFP_KERNEL);
	if (sync == NULL) {
		kfree(mp);
		return(NULL);
	}
	memset(sync, 0x0, sizeof(struct a));

	mutex_spinlock_init(&sync->lock);
	sv_init( &(sync->sema), &(sync->lock), SV_MON_SPIN | SV_ORDER_FIFO /*| SV_INTS*/);
	mp[1].m_size = (unsigned long) &sync->lock;
	mp[1].m_addr = (unsigned long) &sync->sema;
	mapsize(mp) = mapsiz - 1;
	return(mp);
}

/*
 * free a map structure previously allocated via rmallocmap().
 */
void
atemapfree(struct map *mp)
{
	struct a {
		spinlock_t lock;
		sv_t 	sema;
	};
	/* ASSERT(sv_waitq(mapout(mp)) == 0); */
	/* sv_destroy(mapout(mp)); */
	spin_lock_destroy(maplock(mp));
	kfree((void *)mp[1].m_size);
	kfree(mp);
}

/*
 * Allocate 'size' units from the given map.
 * Return the base of the allocated space.
 * In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 * Algorithm is first-fit.
 */

ulong_t
atealloc(
	struct map *mp,
	size_t size)
{
	register unsigned int a;
	register struct map *bp;
	register unsigned long s;

	ASSERT(size >= 0);

	if (size == 0)
		return((ulong_t) NULL);

	s = mutex_spinlock(maplock(mp));

	for (bp = mapstart(mp); bp->m_size; bp++) {
		if (bp->m_size >= size) {
			a = bp->m_addr;
			bp->m_addr += size;
			if ((bp->m_size -= size) == 0) {
				do {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
				} while ((((bp-1)->m_size) = (bp->m_size)));
				mapsize(mp)++;
			}

			ASSERT(bp->m_size < 0x80000000);
			mutex_spinunlock(maplock(mp), s);
			return(a);
		}
	}

	/*
	 * We did not get what we need .. we cannot sleep .. 
	 */
	mutex_spinunlock(maplock(mp), s);
	return(0);
}

/*
 * Free the previously allocated space a of size units into the specified map.
 * Sort ``a'' into map and combine on one or both ends if possible.
 * Returns 0 on success, 1 on failure.
 */
void
atefree(struct map *mp, size_t size, ulong_t a)
{
	register struct map *bp;
	register unsigned int t;
	register unsigned long s;

	ASSERT(size >= 0);

	if (size == 0)
		return;

	bp = mapstart(mp);
	s = mutex_spinlock(maplock(mp));

	for ( ; bp->m_addr<=a && bp->m_size!=0; bp++)
		;
	if (bp>mapstart(mp) && (bp-1)->m_addr+(bp-1)->m_size == a) {
		(bp-1)->m_size += size;
		if (bp->m_addr) {	
			/* m_addr==0 end of map table */
			ASSERT(a+size <= bp->m_addr);
			if (a+size == bp->m_addr) { 

				/* compress adjacent map addr entries */
				(bp-1)->m_size += bp->m_size;
				while (bp->m_size) {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
					(bp-1)->m_size = bp->m_size;
				}
				mapsize(mp)++;
			}
		}
	} else {
		if (a+size == bp->m_addr && bp->m_size) {
			bp->m_addr -= size;
			bp->m_size += size;
		} else {
			ASSERT(size);
			if (mapsize(mp) == 0) {
				mutex_spinunlock(maplock(mp), s);
				printk("atefree : map overflow 0x%p Lost 0x%lx items at 0x%lx",
						(void *)mp, size, a) ;
				return ;
			}
			do {
				t = bp->m_addr;
				bp->m_addr = a;
				a = t;
				t = bp->m_size;
				bp->m_size = size;
				bp++;
			} while ((size = t));
			mapsize(mp)--;
		}
	}
	mutex_spinunlock(maplock(mp), s);
	/*
	 * wake up everyone waiting for space
	 */
	if (mapout(mp))
		;
		/* sv_broadcast(mapout(mp)); */
}
