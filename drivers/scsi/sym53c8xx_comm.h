/******************************************************************************
**  High Performance device driver for the Symbios 53C896 controller.
**
**  Copyright (C) 1998-2001  Gerard Roudier <groudier@free.fr>
**
**  This driver also supports all the Symbios 53C8XX controller family, 
**  except 53C810 revisions < 16, 53C825 revisions < 16 and all 
**  revisions of 53C815 controllers.
**
**  This driver is based on the Linux port of the FreeBSD ncr driver.
** 
**  Copyright (C) 1994  Wolfgang Stanglmeier
**  
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
**
**  The Linux port of the FreeBSD ncr driver has been achieved in 
**  november 1995 by:
**
**          Gerard Roudier              <groudier@free.fr>
**
**  Being given that this driver originates from the FreeBSD version, and
**  in order to keep synergy on both, any suggested enhancements and corrections
**  received on Linux are automatically a potential candidate for the FreeBSD 
**  version.
**
**  The original driver has been written for 386bsd and FreeBSD by
**          Wolfgang Stanglmeier        <wolf@cologne.de>
**          Stefan Esser                <se@mi.Uni-Koeln.de>
**
**-----------------------------------------------------------------------------
**
**  Major contributions:
**  --------------------
**
**  NVRAM detection and reading.
**    Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
**
*******************************************************************************
*/

/*
**	This file contains definitions and code that the 
**	sym53c8xx and ncr53c8xx drivers should share.
**	The sharing will be achieved in a further version  
**	of the driver bundle. For now, only the ncr53c8xx 
**	driver includes this file.
*/

/*==========================================================
**
**	Hmmm... What complex some PCI-HOST bridges actually 
**	are, despite the fact that the PCI specifications 
**	are looking so smart and simple! ;-)
**
**==========================================================
*/

/*==========================================================
**
**	Miscallaneous defines.
**
**==========================================================
*/

#define u_char		unsigned char
#define u_long		unsigned long

#ifndef bzero
#define bzero(d, n)	memset((d), 0, (n))
#endif
 
/*==========================================================
**
**	assert ()
**
**==========================================================
**
**	modified copy from 386bsd:/usr/include/sys/assert.h
**
**----------------------------------------------------------
*/

#define	assert(expression) { \
	if (!(expression)) { \
		(void)panic( \
			"assertion \"%s\" failed: file \"%s\", line %d\n", \
			#expression, \
			__FILE__, __LINE__); \
	} \
}

/*==========================================================
**
**	Debugging tags
**
**==========================================================
*/

#define DEBUG_ALLOC    (0x0001)
#define DEBUG_PHASE    (0x0002)
#define DEBUG_QUEUE    (0x0008)
#define DEBUG_RESULT   (0x0010)
#define DEBUG_POINTER  (0x0020)
#define DEBUG_SCRIPT   (0x0040)
#define DEBUG_TINY     (0x0080)
#define DEBUG_TIMING   (0x0100)
#define DEBUG_NEGO     (0x0200)
#define DEBUG_TAGS     (0x0400)
#define DEBUG_SCATTER  (0x0800)
#define DEBUG_IC        (0x1000)

/*
**    Enable/Disable debug messages.
**    Can be changed at runtime too.
*/

#ifdef SCSI_NCR_DEBUG_INFO_SUPPORT
static int ncr_debug = SCSI_NCR_DEBUG_FLAGS;
	#define DEBUG_FLAGS ncr_debug
#else
	#define DEBUG_FLAGS	SCSI_NCR_DEBUG_FLAGS
#endif

/*==========================================================
**
**	A la VMS/CAM-3 queue management.
**	Implemented from linux list management.
**
**==========================================================
*/

typedef struct xpt_quehead {
	struct xpt_quehead *flink;	/* Forward  pointer */
	struct xpt_quehead *blink;	/* Backward pointer */
} XPT_QUEHEAD;

#define xpt_que_init(ptr) do { \
	(ptr)->flink = (ptr); (ptr)->blink = (ptr); \
} while (0)

static inline void __xpt_que_add(struct xpt_quehead * new,
	struct xpt_quehead * blink,
	struct xpt_quehead * flink)
{
	flink->blink	= new;
	new->flink	= flink;
	new->blink	= blink;
	blink->flink	= new;
}

static inline void __xpt_que_del(struct xpt_quehead * blink,
	struct xpt_quehead * flink)
{
	flink->blink = blink;
	blink->flink = flink;
}

static inline int xpt_que_empty(struct xpt_quehead *head)
{
	return head->flink == head;
}

static inline void xpt_que_splice(struct xpt_quehead *list,
	struct xpt_quehead *head)
{
	struct xpt_quehead *first = list->flink;

	if (first != list) {
		struct xpt_quehead *last = list->blink;
		struct xpt_quehead *at   = head->flink;

		first->blink = head;
		head->flink  = first;

		last->flink = at;
		at->blink   = last;
	}
}

#define xpt_que_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))


#define xpt_insque(new, pos)		__xpt_que_add(new, pos, (pos)->flink)

#define xpt_remque(el)			__xpt_que_del((el)->blink, (el)->flink)

#define xpt_insque_head(new, head)	__xpt_que_add(new, head, (head)->flink)

static inline struct xpt_quehead *xpt_remque_head(struct xpt_quehead *head)
{
	struct xpt_quehead *elem = head->flink;

	if (elem != head)
		__xpt_que_del(head, elem->flink);
	else
		elem = NULL;
	return elem;
}

#define xpt_insque_tail(new, head)	__xpt_que_add(new, (head)->blink, head)

static inline struct xpt_quehead *xpt_remque_tail(struct xpt_quehead *head)
{
	struct xpt_quehead *elem = head->blink;

	if (elem != head)
		__xpt_que_del(elem->blink, head);
	else
		elem = 0;
	return elem;
}


/*==========================================================
**
**	SMP threading.
**
**	Assuming that SMP systems are generally high end 
**	systems and may use several SCSI adapters, we are 
**	using one lock per controller instead of some global 
**	one. For the moment (linux-2.1.95), driver's entry 
**	points are called with the 'io_request_lock' lock 
**	held, so:
**	- We are uselessly loosing a couple of micro-seconds 
**	  to lock the controller data structure.
**	- But the driver is not broken by design for SMP and 
**	  so can be more resistant to bugs or bad changes in 
**	  the IO sub-system code.
**	- A small advantage could be that the interrupt code 
**	  is grained as wished (e.g.: by controller).
**
**==========================================================
*/

spinlock_t DRIVER_SMP_LOCK = SPIN_LOCK_UNLOCKED;
#define	NCR_LOCK_DRIVER(flags)     spin_lock_irqsave(&DRIVER_SMP_LOCK, flags)
#define	NCR_UNLOCK_DRIVER(flags)   \
		spin_unlock_irqrestore(&DRIVER_SMP_LOCK, flags)

#define NCR_INIT_LOCK_NCB(np)      spin_lock_init(&np->smp_lock)
#define	NCR_LOCK_NCB(np, flags)    spin_lock_irqsave(&np->smp_lock, flags)
#define	NCR_UNLOCK_NCB(np, flags)  spin_unlock_irqrestore(&np->smp_lock, flags)

#define	NCR_LOCK_SCSI_DONE(host, flags) \
		spin_lock_irqsave((host)->host_lock, flags)
#define	NCR_UNLOCK_SCSI_DONE(host, flags) \
		spin_unlock_irqrestore(((host)->host_lock), flags)

/*==========================================================
**
**	Memory mapped IO
**
**	Since linux-2.1, we must use ioremap() to map the io 
**	memory space and iounmap() to unmap it. This allows 
**	portability. Linux 1.3.X and 2.0.X allow to remap 
**	physical pages addresses greater than the highest 
**	physical memory address to kernel virtual pages with 
**	vremap() / vfree(). That was not portable but worked 
**	with i386 architecture.
**
**==========================================================
*/

#ifdef __sparc__
#include <asm/irq.h>
#endif

#define memcpy_to_pci(a, b, c)	memcpy_toio((a), (b), (c))

/*==========================================================
**
**	Insert a delay in micro-seconds and milli-seconds.
**
**	Under Linux, udelay() is restricted to delay < 
**	1 milli-second. In fact, it generally works for up 
**	to 1 second delay. Since 2.1.105, the mdelay() function 
**	is provided for delays in milli-seconds.
**	Under 2.0 kernels, udelay() is an inline function 
**	that is very inaccurate on Pentium processors.
**
**==========================================================
*/

#define UDELAY udelay
#define MDELAY mdelay

/*==========================================================
**
**	Simple power of two buddy-like allocator.
**
**	This simple code is not intended to be fast, but to 
**	provide power of 2 aligned memory allocations.
**	Since the SCRIPTS processor only supplies 8 bit 
**	arithmetic, this allocator allows simple and fast 
**	address calculations  from the SCRIPTS code.
**	In addition, cache line alignment is guaranteed for 
**	power of 2 cache line size.
**	Enhanced in linux-2.3.44 to provide a memory pool 
**	per pcidev to support dynamic dma mapping. (I would 
**	have preferred a real bus astraction, btw).
**
**==========================================================
*/

#define __GetFreePages(flags, order) __get_free_pages(flags, order)

#define MEMO_SHIFT	4	/* 16 bytes minimum memory chunk */
#if PAGE_SIZE >= 8192
#define MEMO_PAGE_ORDER	0	/* 1 PAGE  maximum */
#else
#define MEMO_PAGE_ORDER	1	/* 2 PAGES maximum */
#endif
#define MEMO_FREE_UNUSED	/* Free unused pages immediately */
#define MEMO_WARN	1
#define MEMO_GFP_FLAGS	GFP_ATOMIC
#define MEMO_CLUSTER_SHIFT	(PAGE_SHIFT+MEMO_PAGE_ORDER)
#define MEMO_CLUSTER_SIZE	(1UL << MEMO_CLUSTER_SHIFT)
#define MEMO_CLUSTER_MASK	(MEMO_CLUSTER_SIZE-1)

typedef u_long m_addr_t;	/* Enough bits to bit-hack addresses */
typedef struct device *m_bush_t;	/* Something that addresses DMAable */

typedef struct m_link {		/* Link between free memory chunks */
	struct m_link *next;
} m_link_s;

typedef struct m_vtob {		/* Virtual to Bus address translation */
	struct m_vtob *next;
	m_addr_t vaddr;
	m_addr_t baddr;
} m_vtob_s;
#define VTOB_HASH_SHIFT		5
#define VTOB_HASH_SIZE		(1UL << VTOB_HASH_SHIFT)
#define VTOB_HASH_MASK		(VTOB_HASH_SIZE-1)
#define VTOB_HASH_CODE(m)	\
	((((m_addr_t) (m)) >> MEMO_CLUSTER_SHIFT) & VTOB_HASH_MASK)

typedef struct m_pool {		/* Memory pool of a given kind */
	m_bush_t bush;
	m_addr_t (*getp)(struct m_pool *);
	void (*freep)(struct m_pool *, m_addr_t);
#define M_GETP()		mp->getp(mp)
#define M_FREEP(p)		mp->freep(mp, p)
#define GetPages()		__GetFreePages(MEMO_GFP_FLAGS, MEMO_PAGE_ORDER)
#define FreePages(p)		free_pages(p, MEMO_PAGE_ORDER)
	int nump;
	m_vtob_s *(vtob[VTOB_HASH_SIZE]);
	struct m_pool *next;
	struct m_link h[PAGE_SHIFT-MEMO_SHIFT+MEMO_PAGE_ORDER+1];
} m_pool_s;

static void *___m_alloc(m_pool_s *mp, int size)
{
	int i = 0;
	int s = (1 << MEMO_SHIFT);
	int j;
	m_addr_t a;
	m_link_s *h = mp->h;

	if (size > (PAGE_SIZE << MEMO_PAGE_ORDER))
		return NULL;

	while (size > s) {
		s <<= 1;
		++i;
	}

	j = i;
	while (!h[j].next) {
		if (s == (PAGE_SIZE << MEMO_PAGE_ORDER)) {
			h[j].next = (m_link_s *) M_GETP();
			if (h[j].next)
				h[j].next->next = NULL;
			break;
		}
		++j;
		s <<= 1;
	}
	a = (m_addr_t) h[j].next;
	if (a) {
		h[j].next = h[j].next->next;
		while (j > i) {
			j -= 1;
			s >>= 1;
			h[j].next = (m_link_s *) (a+s);
			h[j].next->next = NULL;
		}
	}
#ifdef DEBUG
	printk("___m_alloc(%d) = %p\n", size, (void *) a);
#endif
	return (void *) a;
}

static void ___m_free(m_pool_s *mp, void *ptr, int size)
{
	int i = 0;
	int s = (1 << MEMO_SHIFT);
	m_link_s *q;
	m_addr_t a, b;
	m_link_s *h = mp->h;

#ifdef DEBUG
	printk("___m_free(%p, %d)\n", ptr, size);
#endif

	if (size > (PAGE_SIZE << MEMO_PAGE_ORDER))
		return;

	while (size > s) {
		s <<= 1;
		++i;
	}

	a = (m_addr_t) ptr;

	while (1) {
#ifdef MEMO_FREE_UNUSED
		if (s == (PAGE_SIZE << MEMO_PAGE_ORDER)) {
			M_FREEP(a);
			break;
		}
#endif
		b = a ^ s;
		q = &h[i];
		while (q->next && q->next != (m_link_s *) b) {
			q = q->next;
		}
		if (!q->next) {
			((m_link_s *) a)->next = h[i].next;
			h[i].next = (m_link_s *) a;
			break;
		}
		q->next = q->next->next;
		a = a & b;
		s <<= 1;
		++i;
	}
}

static void *__m_calloc2(m_pool_s *mp, int size, char *name, int uflags)
{
	void *p;

	p = ___m_alloc(mp, size);

	if (DEBUG_FLAGS & DEBUG_ALLOC)
		printk ("new %-10s[%4d] @%p.\n", name, size, p);

	if (p)
		bzero(p, size);
	else if (uflags & MEMO_WARN)
		printk (NAME53C8XX ": failed to allocate %s[%d]\n", name, size);

	return p;
}

#define __m_calloc(mp, s, n)	__m_calloc2(mp, s, n, MEMO_WARN)

static void __m_free(m_pool_s *mp, void *ptr, int size, char *name)
{
	if (DEBUG_FLAGS & DEBUG_ALLOC)
		printk ("freeing %-10s[%4d] @%p.\n", name, size, ptr);

	___m_free(mp, ptr, size);

}

/*
 * With pci bus iommu support, we use a default pool of unmapped memory 
 * for memory we donnot need to DMA from/to and one pool per pcidev for 
 * memory accessed by the PCI chip. `mp0' is the default not DMAable pool.
 */

static m_addr_t ___mp0_getp(m_pool_s *mp)
{
	m_addr_t m = GetPages();
	if (m)
		++mp->nump;
	return m;
}

static void ___mp0_freep(m_pool_s *mp, m_addr_t m)
{
	FreePages(m);
	--mp->nump;
}

static m_pool_s mp0 = {NULL, ___mp0_getp, ___mp0_freep};

/*
 * DMAable pools.
 */

/*
 * With pci bus iommu support, we maintain one pool per pcidev and a 
 * hashed reverse table for virtual to bus physical address translations.
 */
static m_addr_t ___dma_getp(m_pool_s *mp)
{
	m_addr_t vp;
	m_vtob_s *vbp;

	vbp = __m_calloc(&mp0, sizeof(*vbp), "VTOB");
	if (vbp) {
		dma_addr_t daddr;
		vp = (m_addr_t) dma_alloc_coherent(mp->bush,
						PAGE_SIZE<<MEMO_PAGE_ORDER,
						&daddr, GFP_ATOMIC);
		if (vp) {
			int hc = VTOB_HASH_CODE(vp);
			vbp->vaddr = vp;
			vbp->baddr = daddr;
			vbp->next = mp->vtob[hc];
			mp->vtob[hc] = vbp;
			++mp->nump;
			return vp;
		}
	}
	if (vbp)
		__m_free(&mp0, vbp, sizeof(*vbp), "VTOB");
	return 0;
}

static void ___dma_freep(m_pool_s *mp, m_addr_t m)
{
	m_vtob_s **vbpp, *vbp;
	int hc = VTOB_HASH_CODE(m);

	vbpp = &mp->vtob[hc];
	while (*vbpp && (*vbpp)->vaddr != m)
		vbpp = &(*vbpp)->next;
	if (*vbpp) {
		vbp = *vbpp;
		*vbpp = (*vbpp)->next;
		dma_free_coherent(mp->bush, PAGE_SIZE<<MEMO_PAGE_ORDER,
				  (void *)vbp->vaddr, (dma_addr_t)vbp->baddr);
		__m_free(&mp0, vbp, sizeof(*vbp), "VTOB");
		--mp->nump;
	}
}

static inline m_pool_s *___get_dma_pool(m_bush_t bush)
{
	m_pool_s *mp;
	for (mp = mp0.next; mp && mp->bush != bush; mp = mp->next);
	return mp;
}

static m_pool_s *___cre_dma_pool(m_bush_t bush)
{
	m_pool_s *mp;
	mp = __m_calloc(&mp0, sizeof(*mp), "MPOOL");
	if (mp) {
		bzero(mp, sizeof(*mp));
		mp->bush = bush;
		mp->getp = ___dma_getp;
		mp->freep = ___dma_freep;
		mp->next = mp0.next;
		mp0.next = mp;
	}
	return mp;
}

static void ___del_dma_pool(m_pool_s *p)
{
	struct m_pool **pp = &mp0.next;

	while (*pp && *pp != p)
		pp = &(*pp)->next;
	if (*pp) {
		*pp = (*pp)->next;
		__m_free(&mp0, p, sizeof(*p), "MPOOL");
	}
}

static void *__m_calloc_dma(m_bush_t bush, int size, char *name)
{
	u_long flags;
	struct m_pool *mp;
	void *m = NULL;

	NCR_LOCK_DRIVER(flags);
	mp = ___get_dma_pool(bush);
	if (!mp)
		mp = ___cre_dma_pool(bush);
	if (mp)
		m = __m_calloc(mp, size, name);
	if (mp && !mp->nump)
		___del_dma_pool(mp);
	NCR_UNLOCK_DRIVER(flags);

	return m;
}

static void __m_free_dma(m_bush_t bush, void *m, int size, char *name)
{
	u_long flags;
	struct m_pool *mp;

	NCR_LOCK_DRIVER(flags);
	mp = ___get_dma_pool(bush);
	if (mp)
		__m_free(mp, m, size, name);
	if (mp && !mp->nump)
		___del_dma_pool(mp);
	NCR_UNLOCK_DRIVER(flags);
}

static m_addr_t __vtobus(m_bush_t bush, void *m)
{
	u_long flags;
	m_pool_s *mp;
	int hc = VTOB_HASH_CODE(m);
	m_vtob_s *vp = NULL;
	m_addr_t a = ((m_addr_t) m) & ~MEMO_CLUSTER_MASK;

	NCR_LOCK_DRIVER(flags);
	mp = ___get_dma_pool(bush);
	if (mp) {
		vp = mp->vtob[hc];
		while (vp && (m_addr_t) vp->vaddr != a)
			vp = vp->next;
	}
	NCR_UNLOCK_DRIVER(flags);
	return vp ? vp->baddr + (((m_addr_t) m) - a) : 0;
}

#define _m_calloc_dma(np, s, n)		__m_calloc_dma(np->dev, s, n)
#define _m_free_dma(np, p, s, n)	__m_free_dma(np->dev, p, s, n)
#define m_calloc_dma(s, n)		_m_calloc_dma(np, s, n)
#define m_free_dma(p, s, n)		_m_free_dma(np, p, s, n)
#define _vtobus(np, p)			__vtobus(np->dev, p)
#define vtobus(p)			_vtobus(np, p)

/*
 *  Deal with DMA mapping/unmapping.
 */

/* To keep track of the dma mapping (sg/single) that has been set */
#define __data_mapped	SCp.phase
#define __data_mapping	SCp.have_data_in

static void __unmap_scsi_data(struct device *dev, Scsi_Cmnd *cmd)
{
	enum dma_data_direction dma_dir = 
		(enum dma_data_direction)scsi_to_pci_dma_dir(cmd->sc_data_direction);

	switch(cmd->__data_mapped) {
	case 2:
		dma_unmap_sg(dev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		dma_unmap_single(dev, cmd->__data_mapping,
				 cmd->request_bufflen, dma_dir);
		break;
	}
	cmd->__data_mapped = 0;
}

static u_long __map_scsi_single_data(struct device *dev, Scsi_Cmnd *cmd)
{
	dma_addr_t mapping;
	enum dma_data_direction dma_dir = 
		(enum dma_data_direction)scsi_to_pci_dma_dir(cmd->sc_data_direction);


	if (cmd->request_bufflen == 0)
		return 0;

	mapping = dma_map_single(dev, cmd->request_buffer,
				 cmd->request_bufflen, dma_dir);
	cmd->__data_mapped = 1;
	cmd->__data_mapping = mapping;

	return mapping;
}

static int __map_scsi_sg_data(struct device *dev, Scsi_Cmnd *cmd)
{
	int use_sg;
	enum dma_data_direction dma_dir = 
		(enum dma_data_direction)scsi_to_pci_dma_dir(cmd->sc_data_direction);

	if (cmd->use_sg == 0)
		return 0;

	use_sg = dma_map_sg(dev, cmd->buffer, cmd->use_sg, dma_dir);
	cmd->__data_mapped = 2;
	cmd->__data_mapping = use_sg;

	return use_sg;
}

static void __sync_scsi_data_for_cpu(struct device *dev, Scsi_Cmnd *cmd)
{
	enum dma_data_direction dma_dir = 
		(enum dma_data_direction)scsi_to_pci_dma_dir(cmd->sc_data_direction);

	switch(cmd->__data_mapped) {
	case 2:
		dma_sync_sg_for_cpu(dev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		dma_sync_single_for_cpu(dev, cmd->__data_mapping,
					cmd->request_bufflen, dma_dir);
		break;
	}
}

static void __sync_scsi_data_for_device(struct device *dev, Scsi_Cmnd *cmd)
{
	enum dma_data_direction dma_dir =
		(enum dma_data_direction)scsi_to_pci_dma_dir(cmd->sc_data_direction);

	switch(cmd->__data_mapped) {
	case 2:
		dma_sync_sg_for_device(dev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		dma_sync_single_for_device(dev, cmd->__data_mapping,
					   cmd->request_bufflen, dma_dir);
		break;
	}
}

#define scsi_sg_dma_address(sc)		sg_dma_address(sc)
#define scsi_sg_dma_len(sc)		sg_dma_len(sc)

#define unmap_scsi_data(np, cmd)	__unmap_scsi_data(np->dev, cmd)
#define map_scsi_single_data(np, cmd)	__map_scsi_single_data(np->dev, cmd)
#define map_scsi_sg_data(np, cmd)	__map_scsi_sg_data(np->dev, cmd)
#define sync_scsi_data_for_cpu(np, cmd)	__sync_scsi_data_for_cpu(np->dev, cmd)
#define sync_scsi_data_for_device(np, cmd) __sync_scsi_data_for_device(np->dev, cmd)

#define scsi_data_direction(cmd)	(cmd->sc_data_direction)

/*==========================================================
**
**	Driver setup.
**
**	This structure is initialized from linux config 
**	options. It can be overridden at boot-up by the boot 
**	command line.
**
**==========================================================
*/
static struct ncr_driver_setup
	driver_setup			= SCSI_NCR_DRIVER_SETUP;

#ifdef	SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT
static struct ncr_driver_setup
	driver_safe_setup __initdata	= SCSI_NCR_DRIVER_SAFE_SETUP;
#endif

#define initverbose (driver_setup.verbose)
#define bootverbose (np->verbose)


/*===================================================================
**
**	Utility routines that protperly return data through /proc FS.
**
**===================================================================
*/
#ifdef SCSI_NCR_USER_INFO_SUPPORT

struct info_str
{
	char *buffer;
	int length;
	int offset;
	int pos;
};

static void copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->length)
		len = info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}
	if (info->pos < info->offset) {
		data += (info->offset - info->pos);
		len  -= (info->offset - info->pos);
	}

	if (len > 0) {
		memcpy(info->buffer + info->pos, data, len);
		info->pos += len;
	}
}

static int copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);
	return len;
}

#endif

/*===================================================================
**
**	Driver setup from the boot command line
**
**===================================================================
*/

#ifdef MODULE
#define	ARG_SEP	' '
#else
#define	ARG_SEP	','
#endif

#define OPT_TAGS		1
#define OPT_MASTER_PARITY	2
#define OPT_SCSI_PARITY		3
#define OPT_DISCONNECTION	4
#define OPT_SPECIAL_FEATURES	5
#define OPT_UNUSED_1		6
#define OPT_FORCE_SYNC_NEGO	7
#define OPT_REVERSE_PROBE	8
#define OPT_DEFAULT_SYNC	9
#define OPT_VERBOSE		10
#define OPT_DEBUG		11
#define OPT_BURST_MAX		12
#define OPT_LED_PIN		13
#define OPT_MAX_WIDE		14
#define OPT_SETTLE_DELAY	15
#define OPT_DIFF_SUPPORT	16
#define OPT_IRQM		17
#define OPT_PCI_FIX_UP		18
#define OPT_BUS_CHECK		19
#define OPT_OPTIMIZE		20
#define OPT_RECOVERY		21
#define OPT_SAFE_SETUP		22
#define OPT_USE_NVRAM		23
#define OPT_EXCLUDE		24
#define OPT_HOST_ID		25

#ifdef SCSI_NCR_IARB_SUPPORT
#define OPT_IARB		26
#endif

static char setup_token[] __initdata = 
	"tags:"   "mpar:"
	"spar:"   "disc:"
	"specf:"  "ultra:"
	"fsn:"    "revprob:"
	"sync:"   "verb:"
	"debug:"  "burst:"
	"led:"    "wide:"
	"settle:" "diff:"
	"irqm:"   "pcifix:"
	"buschk:" "optim:"
	"recovery:"
	"safe:"   "nvram:"
	"excl:"   "hostid:"
#ifdef SCSI_NCR_IARB_SUPPORT
	"iarb:"
#endif
	;	/* DONNOT REMOVE THIS ';' */

#ifdef MODULE
#define	ARG_SEP	' '
#else
#define	ARG_SEP	','
#endif

static int __init get_setup_token(char *p)
{
	char *cur = setup_token;
	char *pc;
	int i = 0;

	while (cur != NULL && (pc = strchr(cur, ':')) != NULL) {
		++pc;
		++i;
		if (!strncmp(p, cur, pc - cur))
			return i;
		cur = pc;
	}
	return 0;
}


static int __init sym53c8xx__setup(char *str)
{
#ifdef SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT
	char *cur = str;
	char *pc, *pv;
	int i, val, c;
	int xi = 0;

	while (cur != NULL && (pc = strchr(cur, ':')) != NULL) {
		char *pe;

		val = 0;
		pv = pc;
		c = *++pv;

		if	(c == 'n')
			val = 0;
		else if	(c == 'y')
			val = 1;
		else
			val = (int) simple_strtoul(pv, &pe, 0);

		switch (get_setup_token(cur)) {
		case OPT_TAGS:
			driver_setup.default_tags = val;
			if (pe && *pe == '/') {
				i = 0;
				while (*pe && *pe != ARG_SEP && 
					i < sizeof(driver_setup.tag_ctrl)-1) {
					driver_setup.tag_ctrl[i++] = *pe++;
				}
				driver_setup.tag_ctrl[i] = '\0';
			}
			break;
		case OPT_MASTER_PARITY:
			driver_setup.master_parity = val;
			break;
		case OPT_SCSI_PARITY:
			driver_setup.scsi_parity = val;
			break;
		case OPT_DISCONNECTION:
			driver_setup.disconnection = val;
			break;
		case OPT_SPECIAL_FEATURES:
			driver_setup.special_features = val;
			break;
		case OPT_FORCE_SYNC_NEGO:
			driver_setup.force_sync_nego = val;
			break;
		case OPT_REVERSE_PROBE:
			driver_setup.reverse_probe = val;
			break;
		case OPT_DEFAULT_SYNC:
			driver_setup.default_sync = val;
			break;
		case OPT_VERBOSE:
			driver_setup.verbose = val;
			break;
		case OPT_DEBUG:
			driver_setup.debug = val;
			break;
		case OPT_BURST_MAX:
			driver_setup.burst_max = val;
			break;
		case OPT_LED_PIN:
			driver_setup.led_pin = val;
			break;
		case OPT_MAX_WIDE:
			driver_setup.max_wide = val? 1:0;
			break;
		case OPT_SETTLE_DELAY:
			driver_setup.settle_delay = val;
			break;
		case OPT_DIFF_SUPPORT:
			driver_setup.diff_support = val;
			break;
		case OPT_IRQM:
			driver_setup.irqm = val;
			break;
		case OPT_PCI_FIX_UP:
			driver_setup.pci_fix_up	= val;
			break;
		case OPT_BUS_CHECK:
			driver_setup.bus_check = val;
			break;
		case OPT_OPTIMIZE:
			driver_setup.optimize = val;
			break;
		case OPT_RECOVERY:
			driver_setup.recovery = val;
			break;
		case OPT_USE_NVRAM:
			driver_setup.use_nvram = val;
			break;
		case OPT_SAFE_SETUP:
			memcpy(&driver_setup, &driver_safe_setup,
				sizeof(driver_setup));
			break;
		case OPT_EXCLUDE:
			if (xi < SCSI_NCR_MAX_EXCLUDES)
				driver_setup.excludes[xi++] = val;
			break;
		case OPT_HOST_ID:
			driver_setup.host_id = val;
			break;
#ifdef SCSI_NCR_IARB_SUPPORT
		case OPT_IARB:
			driver_setup.iarb = val;
			break;
#endif
		default:
			printk("sym53c8xx_setup: unexpected boot option '%.*s' ignored\n", (int)(pc-cur+1), cur);
			break;
		}

		if ((cur = strchr(cur, ARG_SEP)) != NULL)
			++cur;
	}
#endif /* SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT */
	return 1;
}

/*===================================================================
**
**	Get device queue depth from boot command line.
**
**===================================================================
*/
#define DEF_DEPTH	(driver_setup.default_tags)
#define ALL_TARGETS	-2
#define NO_TARGET	-1
#define ALL_LUNS	-2
#define NO_LUN		-1

static int device_queue_depth(int unit, int target, int lun)
{
	int c, h, t, u, v;
	char *p = driver_setup.tag_ctrl;
	char *ep;

	h = -1;
	t = NO_TARGET;
	u = NO_LUN;
	while ((c = *p++) != 0) {
		v = simple_strtoul(p, &ep, 0);
		switch(c) {
		case '/':
			++h;
			t = ALL_TARGETS;
			u = ALL_LUNS;
			break;
		case 't':
			if (t != target)
				t = (target == v) ? v : NO_TARGET;
			u = ALL_LUNS;
			break;
		case 'u':
			if (u != lun)
				u = (lun == v) ? v : NO_LUN;
			break;
		case 'q':
			if (h == unit &&
				(t == ALL_TARGETS || t == target) &&
				(u == ALL_LUNS    || u == lun))
				return v;
			break;
		case '-':
			t = ALL_TARGETS;
			u = ALL_LUNS;
			break;
		default:
			break;
		}
		p = ep;
	}
	return DEF_DEPTH;
}
