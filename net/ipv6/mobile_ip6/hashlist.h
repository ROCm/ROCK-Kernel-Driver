/*
 *	MIPL Mobile IPv6 Hashlist header file
 *
 *	$Id: s.hashlist.h 1.13 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _HASHLIST_H
#define _HASHLIST_H

#define ITERATOR_ERR -1
#define ITERATOR_CONT 0
#define ITERATOR_STOP 1
#define ITERATOR_DELETE_ENTRY 2

struct hashlist_entry {
	unsigned long sortkey;
	struct list_head sorted;
	struct list_head hashlist;
};

struct hashlist * hashlist_create(
	int bucketnum, int max_entries, size_t size, char *name,
	void (*ctor)(void *, kmem_cache_t *, unsigned long),
	void (*dtor)(void *, kmem_cache_t *, unsigned long),
	int (*compare)(void *data, void *hashkey),
	__u32 (*hash_function)(void *hashkey));

void hashlist_destroy(struct hashlist *hashlist);

void *hashlist_alloc(struct hashlist *hashlist, int type);

void hashlist_free(struct hashlist *hashlist, struct hashlist_entry *he);

struct hashlist_entry *hashlist_get(struct hashlist *hashlist, void *hashkey);

struct hashlist_entry *hashlist_get_ex(
	struct hashlist *hashlist, void *hashkey,
	int (*compare)(void *data, void *hashkey));

int hashlist_add(struct hashlist *hashlist, void *hashkey,
		 unsigned long sortkey, void *data);

void hashlist_delete(struct hashlist *hashlist, struct hashlist_entry *he);

/* iterator function */
typedef int (*hashlist_iterator_t)(void *, void *, unsigned long *);

int hashlist_iterate(struct hashlist *hashlist, void *args,
		     hashlist_iterator_t func);

void * hashlist_get_first(struct hashlist *hashlist);

int hashlist_reposition(struct hashlist *hashlist, struct hashlist_entry *he,
			unsigned long sortkey);

#endif
