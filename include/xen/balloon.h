/******************************************************************************
 * balloon.h
 *
 * Xen balloon driver - enables returning/claiming memory to/from Xen.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __XEN_BALLOON_H__
#define __XEN_BALLOON_H__

#include <linux/spinlock.h>

#if !defined(CONFIG_PARAVIRT_XEN) || defined(HAVE_XEN_PLATFORM_COMPAT_H)
/*
 * Inform the balloon driver that it should allow some slop for device-driver
 * memory activities.
 */
void balloon_update_driver_allowance(long delta);

/* Allocate/free a set of empty pages in low memory (i.e., no RAM mapped). */
struct page **alloc_empty_pages_and_pagevec(int nr_pages);
void free_empty_pages_and_pagevec(struct page **pagevec, int nr_pages);

/* Free an empty page range (not allocated through
   alloc_empty_pages_and_pagevec), adding to the balloon. */
void free_empty_pages(struct page **pagevec, int nr_pages);

void balloon_release_driver_page(struct page *page);

/*
 * Prevent the balloon driver from changing the memory reservation during
 * a driver critical region.
 */
extern spinlock_t balloon_lock;
#define balloon_lock(__flags)   spin_lock_irqsave(&balloon_lock, __flags)
#define balloon_unlock(__flags) spin_unlock_irqrestore(&balloon_lock, __flags)

#else /* CONFIG_PARAVIRT_XEN */

#define RETRY_UNLIMITED	0

struct balloon_stats {
	/* We aim for 'current allocation' == 'target allocation'. */
	unsigned long current_pages;
	unsigned long target_pages;
	/* Number of pages in high- and low-memory balloons. */
	unsigned long balloon_low;
	unsigned long balloon_high;
	unsigned long schedule_delay;
	unsigned long max_schedule_delay;
	unsigned long retry_count;
	unsigned long max_retry_count;
#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG
	unsigned long hotplug_pages;
	unsigned long balloon_hotplug;
#endif
};

extern struct balloon_stats balloon_stats;

void balloon_set_new_target(unsigned long target);

int alloc_xenballooned_pages(int nr_pages, struct page **pages,
		bool highmem);
void free_xenballooned_pages(int nr_pages, struct page **pages);

struct page *get_balloon_scratch_page(void);
void put_balloon_scratch_page(void);

#endif /* CONFIG_PARAVIRT_XEN */

struct device;
#ifdef CONFIG_XEN_SELFBALLOONING
extern int register_xen_selfballooning(struct device *dev);
#else
static inline int register_xen_selfballooning(struct device *dev)
{
	return -ENOSYS;
}
#endif

#endif /* __XEN_BALLOON_H__ */
