/*
 * kref.c - library routines for handling generic reference counted objects
 *
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2004 IBM Corp.
 *
 * based on lib/kobject.c which was:
 * Copyright (C) 2002-2003 Patrick Mochel <mochel@osdl.org>
 *
 * This file is released under the GPLv2.
 *
 */

/* #define DEBUG */

#include <linux/kref.h>
#include <linux/module.h>

/**
 * kref_init - initialize object.
 * @kref: object in question.
 * @release: pointer to a function that will clean up the object
 *	     when the last reference to the object is released.
 *	     This pointer is required.
 */
void kref_init(struct kref *kref, void (*release)(struct kref *kref))
{
	WARN_ON(release == NULL);
	atomic_set(&kref->refcount,1);
	kref->release = release;
}

/**
 * kref_get - increment refcount for object.
 * @kref: object.
 */
struct kref *kref_get(struct kref *kref)
{
	WARN_ON(!atomic_read(&kref->refcount));
	atomic_inc(&kref->refcount);
	return kref;
}

/**
 * kref_put - decrement refcount for object.
 * @kref: object.
 *
 * Decrement the refcount, and if 0, call kref->release().
 */
void kref_put(struct kref *kref)
{
	if (atomic_dec_and_test(&kref->refcount)) {
		pr_debug("kref cleaning up\n");
		kref->release(kref);
	}
}

EXPORT_SYMBOL(kref_init);
EXPORT_SYMBOL(kref_get);
EXPORT_SYMBOL(kref_put);
