/*
 *	Generic hashtable with chaining.  Supports secodary sort order
 *	with doubly linked-list.
 *
 *	Authors:
 *	Sami Kivisaari		<skivisaa@cc.hut.fi>
 *	Antti Tuominen		<ajtuomin@tml.hut.fi>
 *
 *	$Id: s.hashlist.c 1.21 02/10/07 19:31:52+03:00 antti@traci.mipl.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include "hashlist.h"
#include "debug.h"

struct hashlist {
	int count;		/* entry count	*/
	int maxcount;		/* max entries	*/
	__u32 bucketnum;	/* hash buckets	*/

	kmem_cache_t *kmem;

	struct list_head *hashtable;
	struct list_head sortedlist;

	int (*compare)(void *data, void *hashkey);
	__u32 (*hash_function)(void *hashkey);
};

/**
 * hashlist_create - Create new hashlist
 * @bucketnum: number of hash buckets
 * @maxentries: maximum number of entries (0 = no limit)
 * @size: entry size in bytes
 * @name: name for kmem_cache_t
 * @ctor: kmem_cache_t constructor
 * @dtor: kmem_cache_t destructor
 * @compare: compare function for key
 * @hash_function: hash function
 *
 * Creates a hashlist structure with @max_entries entries of @size
 * bytes.  User must supply @hash_function and @compare function for
 * the hashlist.  User can also supply @ctor and @dtor for kmem_cache.
 **/
struct hashlist *hashlist_create(int bucketnum, int max_entries, size_t size,
				 char *name,
				 void (*ctor)(void *, kmem_cache_t *, unsigned long),
				 void (*dtor)(void *, kmem_cache_t *, unsigned long),
				 int (*compare)(void *data, void *hashkey),
				 __u32 (*hash_function)(void *hashkey))
{
	int i;
	struct hashlist *hl;

	if (!compare || !hash_function)
		goto hlfailed;

	hl = kmalloc(sizeof(struct hashlist), GFP_ATOMIC);
	if (!hl) goto hlfailed;

	hl->kmem = kmem_cache_create(name, size, 0, 0, ctor, dtor);
	if (!hl->kmem) goto poolfailed;

	hl->hashtable = kmalloc(
		sizeof(struct list_head) * bucketnum, GFP_ATOMIC);
	if (!hl->hashtable) goto hashfailed;

	for (i = 0; i < bucketnum; i++)
		INIT_LIST_HEAD(&hl->hashtable[i]);

	INIT_LIST_HEAD(&hl->sortedlist);

	hl->maxcount = max_entries;
	hl->count = 0;
	hl->bucketnum = bucketnum;
	hl->compare = compare;
	hl->hash_function = hash_function;

	return hl;

hashfailed:
	kmem_cache_destroy(hl->kmem);
	hl->kmem = NULL;

poolfailed:
	kfree(hl);

hlfailed:
	DEBUG(DBG_ERROR, "could not create hashlist");

	return NULL;	
}

/**
 * hashlist_destroy - Destroy hashlist
 * @hashlist: hashlist to destroy
 *
 * Frees all memory allocated for a hashlist.
 **/
void hashlist_destroy(struct hashlist *hashlist)
{
	DEBUG_FUNC();

	if (hashlist == NULL) return;

	if (hashlist->hashtable) {
		kfree(hashlist->hashtable);
		hashlist->hashtable = NULL;
	}

	if (hashlist->kmem) {
		kmem_cache_destroy(hashlist->kmem);
		hashlist->kmem = NULL;
	}

	kfree(hashlist);

	return;
}

/*
 * Insert a chain of entries to hashlist into correct order.  The
 * entries are assumed to have valid hashkeys.  We use time_after_eq
 * for comparing, since it handles wrap-around correctly, and the
 * sortkey is usually jiffies.
 */
static void sorted_insert(struct list_head *lh, struct hashlist_entry *he)
{
	struct list_head *p;
	struct hashlist_entry *hlp = NULL;
	unsigned long sortkey = he->sortkey;

	if (list_empty(lh)) {
		list_add(&he->sorted, lh);
		return;
	}
	
	list_for_each(p, lh) {
		hlp = list_entry(p, typeof(*hlp), sorted);
		if (time_after_eq(hlp->sortkey, sortkey)) {
			list_add(&he->sorted, hlp->sorted.prev);
			return;
		}
	}
	list_add(&he->sorted, &hlp->sorted);
}

/**
 * hashlist_iterate - Apply function for all elements in a hash list
 * @hashlist: pointer to hashlist
 * @args: data to pass to the function
 * @func: pointer to a function
 *
 * Apply arbitrary function @func to all elements in a hash list.
 * @func must be a pointer to a function with the following prototype:
 * int func(void *entry, void *arg, struct in6_addr *hashkey, unsigned
 * long *sortkey).  Function must return %ITERATOR_STOP,
 * %ITERATOR_CONT or %ITERATOR_DELETE_ENTRY.  %ITERATOR_STOP stops
 * iterator and returns last return value from the function.
 * %ITERATOR_CONT continues with iteration.  %ITERATOR_DELETE_ENTRY
 * deletes current entry from the hashlist.  If function changes
 * hashlist element's sortkey, iterator automatically schedules
 * element to be reinserted after all elements have been processed.
 */
int hashlist_iterate(
	struct hashlist *hashlist, void *args,
	hashlist_iterator_t func)
{
	int res = ITERATOR_CONT;
	unsigned long skey;
	struct list_head *p, *n, repos;
	struct hashlist_entry *he;

	DEBUG_FUNC();
	INIT_LIST_HEAD(&repos);

	list_for_each_safe(p, n, &hashlist->sortedlist) {
		he = list_entry(p, typeof(*he), sorted);
		if (res == ITERATOR_STOP)
			break;
		skey = he->sortkey;
		res = func(he, args, &he->sortkey);
		if (res == ITERATOR_DELETE_ENTRY) {
			hashlist_delete(hashlist, he);
			hashlist_free(hashlist, he);
		} else if (skey != he->sortkey) {
			/* iterator changed the sortkey, schedule for
			 * repositioning */
			list_move(&he->sorted, &repos);
		}
	}
	list_for_each_safe(p, n, &repos) {	
		he = list_entry(p, typeof(*he), sorted);
		sorted_insert(&hashlist->sortedlist, he);
	}
	return res;
}

/**
 * hashlist_alloc - Allocate memory for a hashlist entry
 * @hashlist: hashlist for allocated entry
 * @size: size of entry in bytes
 *
 * Allocates @size bytes memory from @hashlist->kmem.
 **/
void *hashlist_alloc(struct hashlist *hashlist, int type)
{
	if (hashlist == NULL) return NULL;
	return kmem_cache_alloc(hashlist->kmem, type);
}

/**
 * hashlist_free - Free hashlist entry
 * @hashlist: hashlist where @he is
 * @he: entry to free
 *
 * Frees an allocated hashlist entry.
 **/
void hashlist_free(struct hashlist *hashlist, struct hashlist_entry *he)
{
	kmem_cache_free(hashlist->kmem, he);
}

/**
 * hashlist_add - Add element to hashlist
 * @hashlist: pointer to hashlist
 * @hashkey: hashkey for the element
 * @sortkey: key for sorting
 * @data: element data
 *
 * Add element to hashlist.  Hashlist is also sorted in a linked list
 * by @sortkey.
 */
int hashlist_add(struct hashlist *hashlist, void *hashkey,
		 unsigned long sortkey, void *entry)
{
	struct hashlist_entry *he = (struct hashlist_entry *)entry;
	unsigned int hash;

	if (hashlist->count >= hashlist->maxcount)
		return -1;

	hashlist->count++;

	/*  link the entry to sorted order  */ 
	he->sortkey = sortkey;
	sorted_insert(&hashlist->sortedlist, he);

	/*  hash the entry  */
	hash = hashlist->hash_function(hashkey) % hashlist->bucketnum;
	list_add(&he->hashlist, &hashlist->hashtable[hash]);

	return 0;
}

/**
 * hashlist_get_ex - Get element from hashlist
 * @hashlist: hashlist
 * @hashkey: hashkey of the desired entry
 *
 * Lookup entry with @hashkey from the hash table using @compare
 * function for entry comparison.  Returns entry on success, otherwise
 * %NULL.
 **/
struct hashlist_entry *hashlist_get_ex(
	struct hashlist *hashlist, void *hashkey,
	int (*compare)(void *data, void *hashkey))
{
	struct list_head *p, *bkt;
	__u32 hash;

	hash = hashlist->hash_function(hashkey) % hashlist->bucketnum;
	bkt = &hashlist->hashtable[hash];

	/*  scan the entries within the same hashbucket  */
	list_for_each(p, bkt) {
		struct hashlist_entry *he = list_entry(p, typeof(*he), 
						       hashlist);
		if (compare(he, hashkey) == 0)
			return he;
	}

	return NULL;
}

/**
 * hashlist_get - Get element from hashlist
 * @hashlist: hashlist
 * @hashkey: hashkey of the desired entry
 *
 * Lookup entry with @hashkey from the hash table.  Returns entry on
 * success, otherwise %NULL.
 **/
struct hashlist_entry *hashlist_get(struct hashlist *hashlist, void *hashkey)
{
	return hashlist_get_ex(hashlist, hashkey, hashlist->compare);
}

/**
 * hashlist_reposition - set entry to new position in the list
 * @hashlist: hashlist
 * @he: entry to reposition
 * @sortkey: new sortkey of the entry
 *
 * If secondary order sortkey changes, entry must be repositioned in
 * the sorted list.
 **/
int hashlist_reposition(struct hashlist *hashlist, struct hashlist_entry *he,
			unsigned long sortkey)
{
	list_del(&he->sorted);
	he->sortkey = sortkey;
	sorted_insert(&hashlist->sortedlist, he);

	return 0;
}

/**
 * hashlist_delete - Delete entry from hashlist
 * @hashlist: hashlist where entry is
 * @he: entry to delete
 *
 * Deletes an entry from the hashlist and sorted list.
 **/
void hashlist_delete(struct hashlist *hashlist,
		     struct hashlist_entry *he)
{
	list_del_init(&he->hashlist);
	list_del_init(&he->sorted);

	hashlist->count--;
}

/**
 * hashlist_get_first - Get first item from sorted list
 * @hashlist: pointer to hashlist
 *
 * Returns first item in the secondary sort order.
 **/
void * hashlist_get_first(struct hashlist *hashlist)
{
	if (list_empty(&hashlist->sortedlist))
		return NULL;
	
	return list_entry(hashlist->sortedlist.next, struct hashlist_entry, sorted);
}

EXPORT_SYMBOL(hashlist_add);
EXPORT_SYMBOL(hashlist_alloc);
EXPORT_SYMBOL(hashlist_create);
EXPORT_SYMBOL(hashlist_delete);
EXPORT_SYMBOL(hashlist_destroy);
EXPORT_SYMBOL(hashlist_free);
EXPORT_SYMBOL(hashlist_get);
EXPORT_SYMBOL(hashlist_get_ex);
EXPORT_SYMBOL(hashlist_get_first);
EXPORT_SYMBOL(hashlist_iterate);
EXPORT_SYMBOL(hashlist_reposition);
