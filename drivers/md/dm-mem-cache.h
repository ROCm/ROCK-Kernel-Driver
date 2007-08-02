/*
 * Copyright (C) 2006,2007 Red Hat GmbH
 *
 * Module Author: Heinz Mauelshagen <Mauelshagen@RedHat.de>
 *
 * Allocate/free total_pages to a per client page pool.
 * Allocate/free memory objects with chunks (1..n) of pages_per_chunk pages
 * hanging off.
 *
 * This file is released under the GPL.
 */

#ifndef _DM_MEM_CACHE_H
#define _DM_MEM_CACHE_H

#define	DM_MEM_CACHE_H_VERSION	"0.1"

#include "dm.h"

static inline struct page_list *pl_elem(struct page_list *pl, unsigned int p)
{
	while (pl && p--)
		pl = pl->next;

	return pl;
}

struct dm_mem_cache_object {
	struct page_list *pl; /* Dynamically allocated array */
	void *private;	      /* Caller context reference */
};

struct dm_mem_cache_client;

/*
 * Create/destroy dm memory cache client resources.
 */
struct dm_mem_cache_client *dm_mem_cache_client_create(
	unsigned int total_pages, unsigned int objects, unsigned int chunks);
void dm_mem_cache_client_destroy(struct dm_mem_cache_client *client);

/*
 * Grow/shrink a dm memory cache client resources.
 */
int dm_mem_cache_grow(struct dm_mem_cache_client *client, unsigned int pages);
int dm_mem_cache_shrink(struct dm_mem_cache_client *client, unsigned int pages);

/*
 * Allocate/free a memory object
 */
struct dm_mem_cache_object *
dm_mem_cache_alloc(struct dm_mem_cache_client *client,
		   unsigned int pages_per_chunk);
void dm_mem_cache_free(struct dm_mem_cache_client *client,
		       struct dm_mem_cache_object *object);

#endif
