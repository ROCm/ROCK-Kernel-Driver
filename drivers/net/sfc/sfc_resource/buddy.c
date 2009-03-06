
/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains implementation of a buddy allocator.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 * Certain parts of the driver were implemented by
 *          Alexandra Kossovsky <Alexandra.Kossovsky@oktetlabs.ru>
 *          OKTET Labs Ltd, Russia,
 *          http://oktetlabs.ru, <info@oktetlabs.ru>
 *          by request of Solarflare Communications
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <ci/efhw/common.h> /* get uintXX types on win32 */
#include <ci/efrm/sysdep.h>
#include <ci/efrm/buddy.h>
#include <ci/efrm/debug.h>

#if 1
#define DEBUG_ALLOC(x)
#else
#define DEBUG_ALLOC(x) x

static inline void efrm_buddy_dump(struct efrm_buddy_allocator *b)
{
	unsigned o;

	EFRM_NOTICE("%s: dump allocator with order %u",
		    __func__, b->order);
	for (o = 0; o <= b->order; o++) {
		struct list_head *l = &b->free_lists[o];
		while (l->next != &b->free_lists[o]) {
			l = l->next;
			EFRM_NOTICE("%s: order %x: %zx", __func__, o,
				    l - b->links);
		}
	}
}
#endif

/*
 * The purpose of the following inline functions is to give the
 * understandable names to the simple actions.
 */
static inline void
efrm_buddy_free_list_add(struct efrm_buddy_allocator *b,
			 unsigned order, unsigned addr)
{
	list_add(&b->links[addr], &b->free_lists[order]);
	b->orders[addr] = (uint8_t) order;
}
static inline void
efrm_buddy_free_list_del(struct efrm_buddy_allocator *b, unsigned addr)
{
	list_del(&b->links[addr]);
	b->links[addr].next = NULL;
}
static inline int
efrm_buddy_free_list_empty(struct efrm_buddy_allocator *b, unsigned order)
{
	return list_empty(&b->free_lists[order]);
}
static inline unsigned
efrm_buddy_free_list_pop(struct efrm_buddy_allocator *b, unsigned order)
{
	struct list_head *l = list_pop(&b->free_lists[order]);
	l->next = NULL;
	return (unsigned)(l - b->links);
}
static inline int
efrm_buddy_addr_in_free_list(struct efrm_buddy_allocator *b, unsigned addr)
{
	return b->links[addr].next != NULL;
}
static inline unsigned
efrm_buddy_free_list_first(struct efrm_buddy_allocator *b, unsigned order)
{
	return (unsigned)(b->free_lists[order].next - b->links);
}

int efrm_buddy_ctor(struct efrm_buddy_allocator *b, unsigned order)
{
	unsigned o;
	unsigned size = 1 << order;

	DEBUG_ALLOC(EFRM_NOTICE("%s(%u)", __func__, order));
	EFRM_ASSERT(b);
	EFRM_ASSERT(order <= sizeof(unsigned) * 8 - 1);

	b->order = order;
	b->free_lists = vmalloc((order + 1) * sizeof(struct list_head));
	if (b->free_lists == NULL)
		goto fail1;

	b->links = vmalloc(size * sizeof(struct list_head));
	if (b->links == NULL)
		goto fail2;

	b->orders = vmalloc(size);
	if (b->orders == NULL)
		goto fail3;

	memset(b->links, 0, size * sizeof(struct list_head));

	for (o = 0; o <= b->order; ++o)
		INIT_LIST_HEAD(b->free_lists + o);

	efrm_buddy_free_list_add(b, b->order, 0);

	return 0;

fail3:
	vfree(b->links);
fail2:
	vfree(b->free_lists);
fail1:
	return -ENOMEM;
}

void efrm_buddy_dtor(struct efrm_buddy_allocator *b)
{
	EFRM_ASSERT(b);

	vfree(b->free_lists);
	vfree(b->links);
	vfree(b->orders);
}

int efrm_buddy_alloc(struct efrm_buddy_allocator *b, unsigned order)
{
	unsigned smallest;
	unsigned addr;

	DEBUG_ALLOC(EFRM_NOTICE("%s(%u)", __func__, order));
	EFRM_ASSERT(b);

	/* Find smallest chunk that is big enough.  ?? Can optimise this by
	 ** keeping array of pointers to smallest chunk for each order.
	 */
	smallest = order;
	while (smallest <= b->order &&
	       efrm_buddy_free_list_empty(b, smallest))
		++smallest;

	if (smallest > b->order) {
		DEBUG_ALLOC(EFRM_NOTICE
			    ("buddy - alloc order %d failed - max order %d",
			     order, b->order););
		return -ENOMEM;
	}

	/* Split blocks until we get one of the correct size. */
	addr = efrm_buddy_free_list_pop(b, smallest);

	DEBUG_ALLOC(EFRM_NOTICE("buddy - alloc %x order %d cut from order %d",
				addr, order, smallest););
	while (smallest-- > order)
		efrm_buddy_free_list_add(b, smallest, addr + (1 << smallest));

	EFRM_DO_DEBUG(b->orders[addr] = (uint8_t) order);

	EFRM_ASSERT(addr < 1u << b->order);
	return addr;
}

void
efrm_buddy_free(struct efrm_buddy_allocator *b, unsigned addr,
		unsigned order)
{
	unsigned buddy_addr;

	DEBUG_ALLOC(EFRM_NOTICE("%s(%u, %u)", __func__, addr, order));
	EFRM_ASSERT(b);
	EFRM_ASSERT(order <= b->order);
	EFRM_ASSERT((unsigned long)addr + ((unsigned long)1 << order) <=
		    (unsigned long)1 << b->order);
	EFRM_ASSERT(!efrm_buddy_addr_in_free_list(b, addr));
	EFRM_ASSERT(b->orders[addr] == order);

	/* merge free blocks */
	while (order < b->order) {
		buddy_addr = addr ^ (1 << order);
		if (!efrm_buddy_addr_in_free_list(b, buddy_addr) ||
		    b->orders[buddy_addr] != order)
			break;
		efrm_buddy_free_list_del(b, buddy_addr);
		if (buddy_addr < addr)
			addr = buddy_addr;
		++order;
	}

	DEBUG_ALLOC(EFRM_NOTICE
		    ("buddy - free %x merged into order %d", addr, order););
	efrm_buddy_free_list_add(b, order, addr);
}
