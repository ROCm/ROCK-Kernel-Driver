/*
 * kref.c - library routines for handling generic reference counted objects
 *
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2004 IBM Corp.
 *
 * based on kobject.h which was:
 * Copyright (C) 2002-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (C) 2002-2003 Open Source Development Labs
 *
 * This file is released under the GPLv2.
 *
 */

#if defined(__KERNEL__) && !defined(_KREF_H_)
#define _KREF_H_

#include <linux/types.h>
#include <asm/atomic.h>


struct kref {
	atomic_t refcount;
	void (*release)(struct kref *kref);
};

void kref_init(struct kref *kref, void (*release)(struct kref *));
struct kref *kref_get(struct kref *kref);
void kref_put(struct kref *kref);


#endif /* _KREF_H_ */
